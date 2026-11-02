// map_app.cpp - MAP: offline OS maps, place search, your position and mesh nodes.
//
// Screens:
//   MAP       the map in the theme colour with OS grid lines; 96 px + / - buttons and a YOU button
//             on the right; drag (or arrows, while the map has focus) to pan; + / - keys zoom,
//             C centres on you, S searches. Nodes show with their nicknames; YOU is a filled dot;
//             a searched place is marked with its name.
//   SEARCH    type a place name: towns, villages, hamlets, settlements, hills and lochs across the
//             Scottish Borders, East Lothian and Northumberland (MESHDECK/maps/places.txt).
//   POSITION  set where you are: type an OS grid reference or lat/lon, use the map centre, or use a
//             node's GPS (e.g. the Cardputer while it's with you).
//   NODES     every node that has sent a position, with distance and bearing from you.
//
// Map packs (MESHDECK/maps/*.map, made by tools/build_map.py) hold 8-bit brightness tiles on the OS
// grid at fixed scales (e.g. 400, 100, 25, 6.25 m per pixel). The view zooms in 2x steps; each
// pixel is taken from the pack level nearest the view's scale, falling back to the next nearest
// where that level has no data. Ported from MESHDECK 1.x, whose drawing takes 78-89 ms per screen
// on the Tab5 with tiles cached.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <climits>
#include <map>
#include <memory>
#include <vector>
#include "apps/apps.h"
#include "formats/map_pack.h"
#include "formats/osgrid.h"
#include "src/misc/lv_area_private.h"   // lv_area_intersect (LVGL 9.2 keeps it in a private header)
#include "services/contacts.h"
#include "services/gazetteer.h"
#include "services/mesh.h"
#include "services/position.h"
#include "ui/fonts.h"
#include "ui/shell.h"
#include "ui/widgets.h"

namespace meshdeck {
namespace {

std::string distanceText(double m) {
    char buf[24];
    if (m < 1000) snprintf(buf, sizeof buf, "%.0f M", m);
    else if (m < 10000) snprintf(buf, sizeof buf, "%.1f KM", m / 1000);
    else snprintf(buf, sizeof buf, "%.0f KM", m / 1000);
    return buf;
}

std::string latLonText(double lat, double lon) {
    char buf[48];
    snprintf(buf, sizeof buf, "%.5f %c  %.5f %c", fabs(lat), lat >= 0 ? 'N' : 'S', fabs(lon), lon >= 0 ? 'E' : 'W');
    return buf;
}

std::string gridOf(double lat, double lon, int digits) {
    double e, n;
    if (!osgrid::toGrid(lat, lon, e, n)) return "OUTSIDE OS GRID";
    return osgrid::format(e, n, digits);
}

} // namespace

class MapApp : public App {
public:
    MapApp(Shell& shell, Services& s)
        : App(shell), mesh_(s.mesh), contacts_(s.contacts), position_(s.position) {}

    const char* name() const override { return "MAP"; }
    const char* subtitle() const override { return "Offline OS maps, place search"; }

    void onOpen() override {
        view_ = View::Map;
        if (!loaded_) loadPacks();
        if (!viewSet_) centreOnBest();
    }

    void build(Frame& frame) override {
        seen_ = position_.version() + mesh_.version();
        lastDraw_ = shell_.platform().uptimeMs();
        map_ = nullptr;
        switch (view_) {
        case View::Map: buildMap(frame); break;
        case View::Search: buildSearch(frame); break;
        case View::Position: buildPosition(frame); break;
        case View::Nodes: buildNodes(frame); break;
        }
    }

    // Mesh traffic changes often: redraw the map at most every 5 s (other screens on change).
    void tick() override {
        uint32_t now = shell_.platform().uptimeMs();
        if (drawMs_ || drawReads_) {   // the map was drawn since the last pass (USB log, for tuning)
            char log[80];
            snprintf(log, sizeof log, "MAP DRAW %u MS (%u TILES READ) AT %s M/PX", (unsigned)drawMs_,
                     (unsigned)drawReads_, mppText(mpp()).c_str());
            shell_.platform().log(log);
            drawMs_ = drawReads_ = 0;
        }
        if (view_ == View::Search && searchDirty_ && now - searchAt_ > 300) {
            searchDirty_ = false;
            shell_.rebuild();
            return;
        }
        uint32_t v = position_.version() + mesh_.version();
        if (v == seen_ || view_ == View::Search || view_ == View::Position) return;
        if (view_ == View::Map && now - lastDraw_ < 5000) return;
        seen_ = v;
        shell_.rebuild();
    }

    bool onBack() override {
        if (view_ == View::Map) return false;
        view_ = View::Map;
        shell_.rebuild();
        return true;
    }

    bool onKey(uint32_t key, uint8_t) override {
        if (view_ != View::Map) return false;
        switch (key) {
        case '+': case '=': zoomBy(1); return true;
        case '-': case '_': zoomBy(-1); return true;
        case 'c': case 'C': centreOnYou(); return true;
        case 's': case 'S': go(View::Search); return true;
        default: break;
        }
        // Arrows pan a quarter screen while the map itself has focus.
        if (!map_ || lv_group_get_focused(lv_obj_get_group(map_)) != map_) return false;
        double qx = mapW_ / 4.0, qy = mapH_ / 4.0;
        switch (key) {
        case LV_KEY_LEFT: pan(-qx, 0); break;
        case LV_KEY_RIGHT: pan(qx, 0); break;
        case LV_KEY_UP: pan(0, -qy); break;
        case LV_KEY_DOWN: pan(0, qy); break;
        default: return false;
        }
        shell_.rebuild();
        return true;
    }

    const char* hint() const override {
        switch (view_) {
        case View::Map: return "+/- ZOOM  |  ARROWS OR DRAG PAN  |  C ON YOU  |  S SEARCH";
        case View::Search: return "TYPE A PLACE, THEN CHOOSE IT  |  ENTER TAKES THE FIRST";
        case View::Position: return "TYPE A GRID REF (SU 123 456) OR LAT, LON (51.10, -1.20), THEN SET";
        default: return "CHOOSE A NODE TO SHOW IT ON THE MAP";
        }
    }

private:
    enum class View { Map, Search, Position, Nodes };
    struct Level { int pack; int level; float mpp; };

    // ---- Map packs and the view -------------------------------------------------------------

    void loadPacks() {
        loaded_ = true;
        Paths p = shell_.paths();
        if (p.available()) {
            std::string dir = p.folder(Folder::Maps);
            for (const DirEntry& e : listDir(dir)) {
                if (e.isDir || e.name.size() < 5 || e.name.compare(e.name.size() - 4, 4, ".map") != 0) continue;
                auto pack = std::make_unique<MapPack>();
                std::string error;
                if (pack->open(dir + "/" + e.name, error)) {
                    if (attribution_.empty()) attribution_ = pack->attribution();
                    packs_.push_back(std::move(pack));
                } else {
                    shell_.notify("MAP " + e.name + ": " + error);
                }
            }
        }
        for (int pi = 0; pi < (int)packs_.size(); pi++)
            for (int li = 0; li < (int)packs_[pi]->levels().size(); li++) {
                float m = packs_[pi]->levels()[li].mpp;
                bool dup = false;
                for (const Level& l : levels_) dup = dup || fabsf(l.mpp - m) < 0.01f;
                if (!dup) levels_.push_back({pi, li, m});
            }
        std::sort(levels_.begin(), levels_.end(), [](const Level& a, const Level& b) { return a.mpp > b.mpp; });
        // View scales: 2x steps from the widest level to one step past the finest (pixels doubled).
        float widest = levels_.empty() ? 400.0f : levels_.front().mpp;
        float finest = levels_.empty() ? 6.25f : levels_.back().mpp;
        for (float m = widest; m >= finest / 2 * 0.99f; m /= 2) zooms_.push_back(m);
    }

    float mpp() const { return zooms_[zoom_]; }

    // The view zoom closest to a scale (m/px).
    int zoomFor(float m) const {
        int best = 0;
        for (int i = 0; i < (int)zooms_.size(); i++)
            if (fabsf(logf(zooms_[i] / m)) < fabsf(logf(zooms_[best] / m))) best = i;
        return best;
    }

    // Start on you; else on a node with a position; else the middle of the most detailed map.
    void centreOnBest() {
        viewSet_ = true;
        PositionService::Fix f = position_.own();
        if (f.valid && osgrid::toGrid(f.lat, f.lon, cE_, cN_)) { zoom_ = zoomFor(25); return; }
        for (const auto& [num, n] : mesh_.nodes())
            if (n.hasPosition && num != mesh_.myNum() && osgrid::toGrid(n.lat, n.lon, cE_, cN_)) { zoom_ = zoomFor(25); return; }
        zoom_ = 0;
        cE_ = 400000;
        cN_ = 600000;
        if (levels_.size() > 1) {
            const MapPack::Level& l = packs_[levels_.back().pack]->levels()[levels_.back().level];
            double tile = MapPack::kTile * l.mpp;
            cE_ = (l.minTx + l.maxTx + 1) * tile / 2;
            cN_ = (l.minTy + l.maxTy + 1) * tile / 2;
            zoom_ = zoomFor(25);
        }
    }

    void zoomBy(int d) {
        int z = zoom_ + d;
        if (z < 0 || z >= (int)zooms_.size()) {
            shell_.notify(d > 0 ? "MOST DETAILED ZOOM" : "WIDEST ZOOM");
            return;
        }
        zoom_ = z;
        shell_.rebuild();
    }

    void pan(double dx, double dy) {   // screen pixels; +x east, +y south
        cE_ = std::min(700000.0, std::max(0.0, cE_ + dx * mpp()));
        cN_ = std::min(1300000.0, std::max(0.0, cN_ - dy * mpp()));
    }

    void centreOnYou() {
        PositionService::Fix f = position_.own();
        if (!f.valid || !osgrid::toGrid(f.lat, f.lon, cE_, cN_)) {
            shell_.notify("YOUR POSITION ISN'T SET: USE POSITION");
            return;
        }
        view_ = View::Map;
        shell_.rebuild();
    }

    static std::string mppText(float m) {
        char b[16];
        snprintf(b, sizeof b, m >= 100 ? "%.0f" : "%.3g", m);
        return b;
    }
    static double gridStep(float m) { return m >= 200 ? 100000 : m >= 40 ? 10000 : 1000; }

    // ---- MAP --------------------------------------------------------------------------------

    void buildMap(Frame& frame) {
        const Theme& t = shell_.th();
        const int g = ui::kGap, btn = ui::kTouchBig;
        frame.action("SEARCH", [this] { go(View::Search); });
        frame.action("POSITION", [this] { go(View::Position); });
        frame.action("NODES", [this] { go(View::Nodes); });

        mapW_ = frame.w - btn - g;
        mapH_ = frame.h;
        map_ = ui::box(frame.body, 0, 0, mapW_, mapH_);
        lv_obj_add_flag(map_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_color(map_, t.line, 0);
        lv_obj_set_style_border_width(map_, 2, 0);
        lv_obj_set_style_border_color(map_, t.ink, LV_STATE_FOCUSED);
        lv_obj_set_style_border_post(map_, true, 0);   // the border drawn over the map image, not under it
        shell_.focusable(map_);
        shell_.preferFocus(map_);
        ui::onEvent(map_, LV_EVENT_PRESSING, [this] { dragging(); });
        ui::onEvent(map_, LV_EVENT_RELEASED, [this] { dragEnd(); });

        // The map itself is not an image: when LVGL draws this box, onDrawMap writes the map's
        // pixels straight into LVGL's drawing buffer (internal RAM on the Tab5), band by band, in
        // the theme's colours (a 256-entry table). The overlays (labels, markers) then draw on
        // top. Filling a 1.1 MB image in PSRAM and having LVGL copy it out again took ~600 ms per
        // screen on the Tab5; this fills the map once, in fast memory.
        const int cw = mapW_ - 4, ch = mapH_ - 4;
        for (int v = 0; v < 256; v++) {
            palette_[v] = lv_color_to_u16(lv_color_mix(t.ink, t.bg, (uint8_t)v));
            palette70_[v] = lv_color_to_u16(lv_color_mix(t.ink, t.bg, (uint8_t)std::max(v, 70)));   // grid lines
        }
        prepareView(cw, ch);
        lv_obj_add_event_cb(map_, onDrawMap, LV_EVENT_DRAW_MAIN, this);
        bool any = viewHasData();
        drawOverlays(cw, ch);

        char info[120];
        snprintf(info, sizeof info, "%s   %s M/PX   GRID %s   ZOOM %d/%d", osgrid::format(cE_, cN_, 8).c_str(),
                 mppText(mpp()).c_str(), distanceText(gridStep(mpp())).c_str(), zoom_ + 1, (int)zooms_.size());
        overlayLabel(info, 10, 8, t.ink);
        std::string foot = packs_.empty() ? "NO MAP PACK: COPY .MAP FILES TO MESHDECK/MAPS ON THE SD CARD"
                           : !any         ? "NO MAP DATA HERE AT THIS ZOOM"
                                          : attribution_;
        lv_obj_t* fl = overlayLabel(foot, 0, 0, packs_.empty() || !any ? t.warn : t.muted);
        lv_obj_align(fl, LV_ALIGN_BOTTOM_LEFT, 8, -6);

        // The big controls on the right edge.
        int x = mapW_ + g;
        lv_obj_t* zin = ui::button(shell_, frame.body, "+", [this] { zoomBy(1); }, btn, btn);
        lv_obj_set_pos(zin, x, 0);
        lv_obj_t* zout = ui::button(shell_, frame.body, "-", [this] { zoomBy(-1); }, btn, btn);
        lv_obj_set_pos(zout, x, btn + g);
        for (lv_obj_t* b : {zin, zout}) lv_obj_set_style_text_font(lv_obj_get_child(b, 0), fonts().title, 0);
        lv_obj_t* you = ui::button(shell_, frame.body, "YOU", [this] { centreOnYou(); }, btn, btn);
        lv_obj_set_pos(you, x, 2 * (btn + g));
    }

    lv_obj_t* overlayLabel(const std::string& text, int x, int y, lv_color_t color) {
        lv_obj_t* l = ui::label(map_, text, fonts().small, color);
        lv_obj_set_style_bg_color(l, shell_.th().bg, 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_70, 0);
        lv_obj_set_style_pad_hor(l, 4, 0);
        lv_obj_set_pos(l, x, y);
        return l;
    }

    uint32_t tilesRead() const {
        uint32_t n = 0;
        for (auto& p : packs_) n += p->tilesRead();
        return n;
    }

    // Works out everything about the current view that doesn't depend on the row: which map
    // level suits each pixel best (order_), and for every column, per level, which tile and which
    // pixel within it (colTx_, colPx_), plus where the vertical grid lines fall. The ESP32-P4 has
    // no double-precision FPU, so this is done once per view, never per pixel.
    void prepareView(int W, int H) {
        viewW_ = W;
        viewH_ = H;
        vm_ = mpp();
        left_ = cE_ - W / 2.0 * vm_;
        top_ = cN_ + H / 2.0 * vm_;
        step_ = gridStep(mpp());
        order_.resize(levels_.size());
        for (size_t i = 0; i < order_.size(); i++) order_[i] = (int)i;
        std::sort(order_.begin(), order_.end(), [&](int a, int b) {
            double da = fabs(log2(levels_[a].mpp / vm_)), db = fabs(log2(levels_[b].mpp / vm_));
            return fabs(da - db) > 1e-6 ? da < db : levels_[a].mpp < levels_[b].mpp;
        });
        const int L = (int)levels_.size();
        // Small types keep these tables under 16 KB each, so on the Tab5 they sit in fast internal
        // RAM (tile columns fit 16 bits: at most 700 km / (256 x 3.125 m) = 875).
        colTx_.assign(L * W, 0);
        colPx_.assign(L * W, 0);
        gridCol_.assign(W, 0);
        // Each column is a fixed step east of the last, so positions are stepped in 32.32 fixed
        // point instead of being worked out in (software) double precision for every column.
        const double e0 = left_ + 0.5 * vm_;
        for (int li = 0; li < L; li++) {
            const double lm = levels_[li].mpp;
            int64_t p = (int64_t)floor(e0 / lm * 4294967296.0);          // level pixels, 32.32
            const int64_t dp = (int64_t)llround(vm_ / lm * 4294967296.0);
            for (int x = 0; x < W; x++, p += dp) {
                int64_t pix = p >> 32;                                      // floor
                int64_t tx = pix >= 0 ? pix / MapPack::kTile : (pix - (MapPack::kTile - 1)) / MapPack::kTile;
                colTx_[li * W + x] = (int16_t)tx;
                colPx_[li * W + x] = (uint8_t)(pix - tx * MapPack::kTile);
            }
        }
        // Grid lines: a column is on one where its centre crosses into a new grid square.
        int64_t e = (int64_t)floor(e0 * 256), de = (int64_t)llround(vm_ * 256), stepFx = (int64_t)step_ * 256;
        for (int x = 0; x < W; x++, e += de)
            gridCol_[x] = e >= 0 && (e % stepFx) < de;
    }

    // True if any map level covers part of the view (checked at the centre and the corners from
    // the levels' areas, without reading tiles).
    bool viewHasData() const {
        const double pts[5][2] = {{cE_, cN_}, {left_, top_}, {left_ + viewW_ * vm_, top_},
                                  {left_, top_ - viewH_ * vm_}, {left_ + viewW_ * vm_, top_ - viewH_ * vm_}};
        for (const Level& lv : levels_) {
            const MapPack::Level& l = packs_[lv.pack]->levels()[lv.level];
            double tileM = MapPack::kTile * l.mpp;
            for (const auto& p : pts) {
                int tx = (int)floor(p[0] / tileM), ty = (int)floor(p[1] / tileM);
                if (tx >= l.minTx && tx <= l.maxTx && ty >= l.minTy && ty <= l.maxTy) return true;
            }
        }
        return false;
    }

    // Writes view rows [y0, y1), columns [x0, x1) (view pixels) into dst, one row every
    // `stride` bytes, in RGB565. Each pixel comes from the level nearest the view's scale, falling
    // back to the next nearest where that level has no data; then the OS grid lines.
    // A row is filled level by level in runs: a run is the stretch of pixels inside one tile, so
    // the tile is looked up once per run and its pixels copied in a tight loop.
    void fillRows(uint8_t* dst, int stride, int x0, int x1, int y0, int y1) {
        const int W = viewW_, n = x1 - x0;
        if (n <= 0) return;
        rowDone_.resize(W);
        struct Seen { int tx; const uint8_t* px; bool covered; };
        std::vector<Seen> seen[16];
        int seenTy[16];
        for (int i = 0; i < 16; i++) seenTy[i] = INT_MIN;
        for (int y = y0; y < y1; y++) {
            double north = top_ - (y + 0.5) * vm_;
            bool gridRow = north >= 0 && fmod(north, step_) < vm_;
            // Pixels are coloured as they are copied: through palette_, or palette70_ (brightness
            // raised to at least the grid line's) on grid lines.
            const uint16_t* lutRow = gridRow ? palette70_ : palette_;
            uint16_t* px = reinterpret_cast<uint16_t*>(dst + (size_t)(y - y0) * stride) - x0;
            uint8_t* done = rowDone_.data();
            memset(done + x0, 0, n);
            int remaining = n;
            for (int li : order_) {
                if (!remaining || li >= 16) break;
                double tileM = MapPack::kTile * levels_[li].mpp;
                int ty = (int)floor(north / tileM);
                int rowOff = std::min(255, std::max(0, (int)(((ty + 1) * tileM - north) / levels_[li].mpp))) * 256;
                if (ty != seenTy[li]) { seenTy[li] = ty; seen[li].clear(); }
                const int16_t* tx = &colTx_[li * W];
                const uint8_t* cp = &colPx_[li * W];
                bool first = remaining == n;   // nothing filled yet: no need to check done[]
                for (int x = x0; x < x1;) {
                    int t = tx[x], end = x + 1;
                    while (end < x1 && tx[end] == t) end++;
                    const Seen* s = nullptr;
                    for (const Seen& k : seen[li])
                        if (k.tx == t) { s = &k; break; }
                    if (!s) {
                        Seen k{t, nullptr, false};
                        k.px = packs_[levels_[li].pack]->tile(levels_[li].level, t, ty, k.covered);
                        seen[li].push_back(k);
                        s = &seen[li].back();
                    }
                    if (s->covered) {   // inside this level's area: its pixels (blank where no tile)
                        const uint8_t* src = s->px ? s->px + rowOff : nullptr;
                        for (int k = x; k < end; k++) {
                            if (!first && done[k]) continue;
                            uint8_t v = src ? src[cp[k]] : 0;
                            px[k] = gridCol_[k] ? palette70_[v] : lutRow[v];
                            done[k] = 1;
                        }
                        remaining -= first ? end - x : 0;
                    }
                    x = end;
                }
                if (!first) {   // count what's left after a fallback level
                    remaining = 0;
                    for (int k = x0; k < x1; k++) remaining += !done[k];
                }
            }
            // Anything no level covered: background (or a grid line).
            if (remaining)
                for (int k = x0; k < x1; k++)
                    if (!done[k]) px[k] = gridCol_[k] ? palette70_[0] : lutRow[0];
        }
    }

    // LVGL is drawing the map box into a layer (a band of the screen, or a screenshot): fill the
    // part of the map inside the layer's clip area straight into its buffer.
    static void onDrawMap(lv_event_t* e) {
        auto* self = static_cast<MapApp*>(lv_event_get_user_data(e));
        lv_layer_t* layer = lv_event_get_layer(e);
        lv_draw_buf_t* buf = layer ? layer->draw_buf : nullptr;
        if (!buf || buf->header.cf != LV_COLOR_FORMAT_RGB565 || !self->map_) return;
        lv_area_t box, view, clip;
        lv_obj_get_coords(self->map_, &box);
        view = {box.x1 + 2, box.y1 + 2, box.x1 + 2 + self->viewW_ - 1, box.y1 + 2 + self->viewH_ - 1};
        if (!lv_area_intersect(&clip, &view, &layer->_clip_area)) return;
        if (!lv_area_intersect(&clip, &clip, &layer->buf_area)) return;
        uint32_t t0 = self->shell_.platform().uptimeMs(), reads0 = self->tilesRead();
        uint8_t* dst = buf->data + (size_t)(clip.y1 - layer->buf_area.y1) * buf->header.stride +
                       (size_t)(clip.x1 - layer->buf_area.x1) * 2;
        self->fillRows(dst, buf->header.stride, clip.x1 - view.x1, clip.x2 + 1 - view.x1, clip.y1 - view.y1,
                       clip.y2 + 1 - view.y1);
        self->drawMs_ += self->shell_.platform().uptimeMs() - t0;
        self->drawReads_ += self->tilesRead() - reads0;
    }

    void dot(int x, int y, int size, bool filled) {
        const Theme& t = shell_.th();
        lv_obj_t* d = lv_obj_create(map_);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, size, size);
        lv_obj_set_pos(d, x - size / 2, y - size / 2);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, filled ? t.ink : t.bg, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(d, filled ? t.bg : t.ink, 0);
        lv_obj_set_style_border_width(d, 3, 0);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
    }

    std::string nodeLabel(uint32_t num) const {
        if (const Contacts::Contact* c = contacts_.find(num)) return c->nickname;
        return mesh_.nodeName(num);
    }

    void drawOverlays(int W, int H) {
        const Theme& t = shell_.th();
        const double vm = mpp(), left = cE_ - W / 2.0 * vm, top = cN_ + H / 2.0 * vm;
        auto onScreen = [&](double e, double n, int& x, int& y) {
            x = (int)((e - left) / vm) + 2;
            y = (int)((top - n) / vm) + 2;
            return x >= 0 && x < W && y >= 0 && y < H;
        };
        auto fromLatLon = [&](double lat, double lon, int& x, int& y) {
            double e, n;
            return osgrid::toGrid(lat, lon, e, n) && onScreen(e, n, x, y);
        };
        // Centre cross.
        for (int i = 0; i < 2; i++) {
            lv_obj_t* l = lv_obj_create(map_);
            lv_obj_remove_style_all(l);
            lv_obj_set_size(l, i ? 2 : 24, i ? 24 : 2);
            lv_obj_set_pos(l, W / 2 + 2 - (i ? 1 : 12), H / 2 + 2 - (i ? 12 : 1));
            lv_obj_set_style_bg_color(l, t.warn, 0);
            lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
            lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
        }
        int x, y;
        if (marked_ && onScreen(markE_, markN_, x, y)) {   // the place last searched for
            dot(x, y, 16, false);
            overlayLabel(markName_, x + 12, y + 6, t.warn);   // below the dot, clear of the map's own label
        }
        for (const auto& [num, n] : mesh_.nodes()) {
            if (!n.hasPosition || num == mesh_.myNum()) continue;
            GridPos& g = gridCache_[num];
            if (g.lat != n.lat || g.lon != n.lon) {   // new or moved: convert once
                g.lat = n.lat;
                g.lon = n.lon;
                g.ok = osgrid::toGrid(n.lat, n.lon, g.e, g.n);
            }
            if (!g.ok || !onScreen(g.e, g.n, x, y)) continue;
            dot(x, y, 14, false);
            overlayLabel(nodeLabel(num), x + 10, y - 14, t.ink);
        }
        PositionService::Fix own = position_.own();
        if (own.valid && fromLatLon(own.lat, own.lon, x, y)) {
            dot(x, y, 20, true);
            overlayLabel("YOU", x + 12, y - 14, t.ink);
        }
    }

    void dragging() {
        lv_indev_t* indev = lv_indev_active();
        if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) return;
        lv_point_t v;
        lv_indev_get_vect(indev, &v);
        dragX_ += v.x;
        dragY_ += v.y;
    }

    void dragEnd() {
        if (abs(dragX_) + abs(dragY_) > 8) {
            pan(-dragX_, -dragY_);
            shell_.rebuild();
        }
        dragX_ = dragY_ = 0;
    }

    // ---- SEARCH -----------------------------------------------------------------------------

    void buildSearch(Frame& frame) {
        const Theme& t = shell_.th();
        if (!placesTried_) {
            placesTried_ = true;
            places_.load(shell_.paths().file(Folder::Maps, "places.txt"));
        }
        lv_obj_t* field = ui::textField(shell_, frame.body, frame.w, "TYPE A PLACE NAME, E.G. PEEBLES", 40);
        lv_obj_set_pos(field, 0, 0);
        lv_textarea_set_text(field, query_.c_str());
        // The results follow what's typed, once typing pauses for 0.3 s: rebuilding the screen on
        // every key would make the box lag behind the keyboard.
        ui::onEvent(field, LV_EVENT_VALUE_CHANGED, [this, field] {
            query_ = lv_textarea_get_text(field);
            searchDirty_ = true;
            searchAt_ = shell_.platform().uptimeMs();
        });
        ui::onEvent(field, LV_EVENT_READY, [this] {
            results_ = places_.search(query_, 30);   // what's typed now, even if the list hasn't caught up
            if (!results_.empty()) choose(results_.front());
        });
        shell_.preferFocus(field);

        results_ = places_.search(query_, 30);
        std::vector<ui::Row> rows;
        if (!places_.loaded()) {
            rows.push_back({"NO PLACE INDEX: COPY PLACES.TXT TO MESHDECK/MAPS (TOOLS/BUILD_GAZETTEER.PY)", "", nullptr});
        } else if (query_.size() < 2) {
            rows.push_back({"TYPE AT LEAST 2 LETTERS. " + std::to_string(places_.size()) + " PLACES IN THE INDEX.", "", nullptr});
        } else if (results_.empty()) {
            rows.push_back({"NO PLACE CALLED \"" + query_ + "\" IN THE INDEX", "", nullptr});
        }
        PositionService::Fix own = position_.own();
        for (const Gazetteer::Place& p : results_) {
            std::string detail = p.type + "   " + osgrid::format(p.easting, p.northing, 6);
            double lat, lon;
            if (own.valid && osgrid::toLatLon(p.easting, p.northing, lat, lon)) {
                char b[16];
                snprintf(b, sizeof b, " %03.0f", osgrid::bearingDeg(own.lat, own.lon, lat, lon));
                detail += "   " + distanceText(osgrid::distanceM(own.lat, own.lon, lat, lon)) + b;
            }
            Gazetteer::Place place = p;
            rows.push_back({p.name, detail, [this, place] { choose(place); }});
        }
        lv_obj_t* l = ui::list(shell_, frame.body, 0, ui::kTouchMin + 24, frame.w, frame.h - ui::kTouchMin - 24, rows);
        (void)t;
        (void)l;
    }

    // Shows a chosen place on the map, at a scale that suits what it is.
    void choose(const Gazetteer::Place& p) {
        marked_ = true;
        markName_ = p.name;
        markE_ = cE_ = p.easting;
        markN_ = cN_ = p.northing;
        float scale = p.type == "CITY" ? 100 : p.type == "TOWN" ? 25 : 12.5f;
        zoom_ = zoomFor(scale);
        go(View::Map);
    }

    // ---- POSITION ---------------------------------------------------------------------------

    void buildPosition(Frame& frame) {
        const Theme& t = shell_.th();
        const int g = ui::kGap, half = (frame.w - g) / 2;
        PositionService::Fix own = position_.own();
        // Left: where you are now, and ways to set it.
        std::string now = own.valid ? gridOf(own.lat, own.lon, 10) + "\n" + latLonText(own.lat, own.lon) + "\n" +
                                          position_.sourceText() + ", " + position_.ageText(own)
                                    : "NOT SET (" + position_.sourceText() + ")";
        if (position_.source() == PositionService::Source::Node)
            now += "\nONLY RIGHT WHILE " + nodeLabel(position_.node()) + " IS WITH YOU";
        lv_obj_t* l = ui::label(frame.body, now, fonts().body, own.valid ? t.ink : t.warn);
        lv_obj_set_pos(l, 0, 0);
        lv_obj_set_width(l, half);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);

        lv_obj_t* field = ui::textField(shell_, frame.body, half - 160 - g, "SU 123 456  OR  51.10, -1.20", 40);
        lv_obj_set_pos(field, 0, 180);
        ui::onEvent(field, LV_EVENT_READY, [this, field] { setTyped(lv_textarea_get_text(field)); });
        lv_obj_t* set = ui::button(shell_, frame.body, "SET", [this, field] { setTyped(lv_textarea_get_text(field)); }, 160,
                                   ui::kTouchMin + 8);
        lv_obj_set_pos(set, half - 160, 180);
        lv_obj_t* centre = ui::button(shell_, frame.body, "USE MAP CENTRE", [this] {
            double lat, lon;
            if (osgrid::toLatLon(cE_, cN_, lat, lon) && position_.setManual(lat, lon)) {
                shell_.notify("POSITION SET TO THE MAP CENTRE: " + osgrid::format(cE_, cN_, 8));
                go(View::Map);
            }
        }, half, ui::kTouchMin + 8);
        lv_obj_set_pos(centre, 0, 270);
        lv_obj_t* clear = ui::button(shell_, frame.body, "CLEAR POSITION", [this] {
            position_.clear();
            shell_.notify("POSITION CLEARED");
            shell_.rebuild();
        }, half, ui::kTouchMin + 8);
        lv_obj_set_pos(clear, 0, 360);
        shell_.preferFocus(field);

        // Right: use a node's GPS instead.
        lv_obj_t* hdr = ui::label(frame.body, "OR USE A NODE'S GPS", fonts().small, t.muted);
        lv_obj_set_pos(hdr, half + g, 0);
        std::vector<ui::Row> rows;
        for (const auto& [num, n] : mesh_.nodes()) {
            if (!n.hasPosition || num == mesh_.myNum()) continue;
            bool mine = position_.source() == PositionService::Source::Node && position_.node() == num;
            uint32_t id = num;
            rows.push_back({(mine ? "* " : "") + nodeLabel(num), gridOf(n.lat, n.lon, 6), [this, id] {
                                position_.useNode(id);
                                shell_.notify("POSITION FROM " + nodeLabel(id) + " (ONLY RIGHT WHILE IT IS WITH YOU)");
                                go(View::Map);
                            }});
        }
        if (rows.empty()) rows.push_back({"NO NODE HAS SENT A POSITION YET", "", nullptr});
        ui::list(shell_, frame.body, half + g, 32, half, frame.h - 32, rows);
    }

    void setTyped(const std::string& text) {
        double e, n, lat, lon;
        bool ok = osgrid::parse(text, e, n) ? osgrid::toLatLon(e, n, lat, lon) : osgrid::parseLatLon(text, lat, lon);
        if (!ok || !position_.setManual(lat, lon)) {
            shell_.notify("NOT A GRID REF OR LAT, LON (E.G. SU 123 456 OR 51.10, -1.20)");
            return;
        }
        osgrid::toGrid(lat, lon, cE_, cN_);
        shell_.notify("POSITION SET: " + gridOf(lat, lon, 10));
        go(View::Map);
    }

    // ---- NODES ------------------------------------------------------------------------------

    void buildNodes(Frame& frame) {
        PositionService::Fix own = position_.own();
        std::vector<ui::Row> rows;
        for (const auto& [num, n] : mesh_.nodes()) {
            if (!n.hasPosition || num == mesh_.myNum()) continue;
            std::string detail = gridOf(n.lat, n.lon, 6);
            if (own.valid) {
                char b[16];
                snprintf(b, sizeof b, " %03.0f", osgrid::bearingDeg(own.lat, own.lon, n.lat, n.lon));
                detail += "   " + distanceText(osgrid::distanceM(own.lat, own.lon, n.lat, n.lon)) + b;
            }
            std::string text = nodeLabel(num) + (n.longName.empty() ? "" : "  " + n.longName);
            double lat = n.lat, lon = n.lon;
            rows.push_back({text, detail, [this, lat, lon] {
                                osgrid::toGrid(lat, lon, cE_, cN_);
                                zoom_ = zoomFor(25);
                                go(View::Map);
                            }});
        }
        if (rows.empty())
            rows.push_back({mesh_.link() == MeshService::Link::Ready ? "NO NODE HAS SENT A POSITION YET" : "NO RADIO", "", nullptr});
        ui::list(shell_, frame.body, 0, 0, frame.w, frame.h, rows);
    }

    void go(View v) {
        view_ = v;
        shell_.rebuild();
    }

    MeshService& mesh_;
    Contacts& contacts_;
    PositionService& position_;
    Gazetteer places_;
    bool placesTried_ = false;
    std::vector<Gazetteer::Place> results_;
    std::string query_;

    View view_ = View::Map;
    std::vector<std::unique_ptr<MapPack>> packs_;
    std::vector<Level> levels_;   // every map level in the packs, widest first
    std::vector<float> zooms_;    // view scales (m/px), widest first, 2x apart
    std::string attribution_;
    bool loaded_ = false, viewSet_ = false;
    int zoom_ = 0;
    double cE_ = 400000, cN_ = 600000;
    bool marked_ = false;
    std::string markName_;
    double markE_ = 0, markN_ = 0;
    int dragX_ = 0, dragY_ = 0;
    int mapW_ = 0, mapH_ = 0;
    lv_obj_t* map_ = nullptr;
    uint32_t seen_ = 0, lastDraw_ = 0, drawMs_ = 0, drawReads_ = 0;
    uint16_t palette_[256] = {0};      // brightness -> theme colour (RGB565)
    uint16_t palette70_[256] = {0};    // the same, at least as bright as a grid line
    // Each node's position as grid metres, kept until its lat/lon changes (the conversion is heavy
    // double-precision maths, done in software on the ESP32-P4).
    struct GridPos { double lat, lon, e, n; bool ok; };
    std::map<uint32_t, GridPos> gridCache_;
    // The current view, worked out by prepareView() for fillRows().
    int viewW_ = 0, viewH_ = 0;
    double vm_ = 1, left_ = 0, top_ = 0, step_ = 1000;
    std::vector<int> order_;
    std::vector<int16_t> colTx_;
    std::vector<uint8_t> colPx_, gridCol_, rowDone_;
    bool searchDirty_ = false;         // typed since the results were last shown
    uint32_t searchAt_ = 0;
};

std::unique_ptr<App> makeMapApp(Shell& shell, Services& services) { return std::make_unique<MapApp>(shell, services); }

} // namespace meshdeck
