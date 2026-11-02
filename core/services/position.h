// position.h - where the deck is.
//
// The Tab5 has no GPS, so the position is either typed in (an OS grid reference or lat/lon), or
// taken from a mesh node's GPS (e.g. the Cardputer, while it is with you). Either way it always
// carries its source and age, and the screens say so: a node's position is only right while that
// node is with the deck. The choice is kept in MESHDECK/position.txt so it survives a restart:
//     MANUAL <lat> <lon> <utc>     or     NODE <node number>     or     NONE
// Ported from MESHDECK 1.x.
#pragma once
#include <stdint.h>
#include <functional>
#include <string>
#include "platform/platform.h"
#include "services/mesh.h"

namespace meshdeck {

class PositionService {
public:
    enum class Source { None, Manual, Node };
    struct Fix {
        bool valid = false;
        double lat = 0, lon = 0;   // WGS84 degrees
        int64_t utc = 0;           // when it was set (0 = unknown)
        uint32_t atMs = 0;         // our uptime when it arrived (0 = not this session)
    };

    PositionService(Platform& platform, MeshService& mesh, std::function<int64_t()> clock);
    void begin(const std::string& path);   // "" = don't keep it

    Source source() const { return source_; }
    uint32_t node() const { return node_; }
    Fix own() const;
    std::string sourceText() const;            // "TYPED IN" / "TS01 GPS (VIA MESH)" / "NOT SET"
    std::string ageText(const Fix& f) const;   // "4 MIN AGO", "SET 2 H AGO", "AGE UNKNOWN"
    uint32_t version() const { return version_; }

    bool setManual(double lat, double lon);
    bool useNode(uint32_t num);
    void clear();

private:
    void save();
    uint32_t ageSeconds(const Fix& f, bool& known) const;

    Platform& platform_;
    MeshService& mesh_;
    std::function<int64_t()> clock_;
    std::string path_;
    Source source_ = Source::None;
    Fix manual_;
    uint32_t node_ = 0;
    uint32_t version_ = 1;
};

} // namespace meshdeck
