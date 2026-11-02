// apps.cpp - see apps.h.
#include "apps/apps.h"
#include <stdio.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "services/mesh.h"
#include "ui/shell.h"

namespace meshdeck {
namespace {

// The home screen's free cell: the radio and the signals it hears, and the Tab5 itself.
std::vector<std::string> homeInfoLines(MeshService& mesh, Shell& shell) {
    const uint32_t now = shell.platform().uptimeMs();
    const uint32_t kRecentMs = 2 * 60 * 60 * 1000u;   // "heard" means heard in the last two hours

    // Nodes heard recently, strongest signal (SNR) first.
    std::vector<const MeshService::Node*> heard;
    for (const auto& [num, n] : mesh.nodes()) {
        if (n.heardMs && now - n.heardMs < kRecentMs) heard.push_back(&n);
    }
    std::sort(heard.begin(), heard.end(), [](const MeshService::Node* a, const MeshService::Node* b) { return a->snr > b->snr; });

    const char* link = mesh.link() == MeshService::Link::Ready        ? "MESH OK"
                       : mesh.link() == MeshService::Link::Connecting ? "MESH CONNECTING"
                                                                       : "NO RADIO";
    std::vector<std::string> out;
    out.push_back(std::string(link) + "   " + std::to_string(heard.size()) + " HEARD IN 2 H");
    for (size_t i = 0; i < heard.size() && i < 2; i++) {
        char line[64];
        snprintf(line, sizeof line, "%s   SNR %+.1f DB", mesh.nodeName(heard[i]->num).c_str(), heard[i]->snr);
        out.push_back(line);
    }
    if (heard.empty()) out.push_back("NO NODES IN 2 H");

    // Tab5: battery and free memory, then uptime, channels and the SD card.
    char tab5[64], heap[24];
    uint32_t freeBytes = 0;
    if (shell.platform().freeHeapBytes(freeBytes)) snprintf(heap, sizeof heap, "HEAP %uK", (unsigned)(freeBytes / 1024));
    else snprintf(heap, sizeof heap, "HEAP --");
    int pct = shell.power().percent();
    if (pct >= 0) snprintf(tab5, sizeof tab5, "TAB5  BAT %d%%  %s", pct, heap);
    else snprintf(tab5, sizeof tab5, "TAB5  %s", heap);
    out.push_back(tab5);
    const int up = (int)(now / 1000);
    char uptime[48];
    snprintf(uptime, sizeof uptime, "UP %dH %02dM  CH %d", up / 3600, up / 60 % 60, (int)mesh.channels().size());
    out.push_back(uptime);
    return out;
}

} // namespace

std::unique_ptr<App> makeLogbookApp(Shell& shell, Services& services);
std::unique_ptr<App> makeFilesApp(Shell& shell);
std::unique_ptr<App> makeSettingsApp(Shell& shell);
std::unique_ptr<App> makeDiagnosticsApp(Shell& shell, Services& services);
std::unique_ptr<App> makeSecurityApp(Shell& shell, Services& services);
std::unique_ptr<App> makeMessagesApp(Shell& shell, MeshService& mesh, Contacts& contacts);
std::unique_ptr<App> makeMapApp(Shell& shell, Services& services);
std::unique_ptr<App> makeLibraryApp(Shell& shell);
std::unique_ptr<App> makeTerminalApp(Shell& shell, MeshService& mesh);

std::unique_ptr<App> makeGamesApp(Shell& shell);
std::unique_ptr<App> makeToolsApp(Shell& shell);

void addBuiltInApps(Shell& shell, Services& services) {
    MeshService& mesh = services.mesh;
    mesh.setEmcon((MeshService::Emcon)shell.emconLevel());   // the level kept from the last run
    shell.addBackgroundTask([&mesh] { mesh.tick(); });
    shell.setHomeInfo([&mesh, &shell] { return homeInfoLines(mesh, shell); });
    shell.addStatusSource([&mesh] {
        switch (mesh.link()) {
        case MeshService::Link::Ready: return std::string("MESH OK");
        case MeshService::Link::Connecting: return std::string("MESH...");
        default: return std::string("NO RADIO");
        }
    });

    shell.addApp(makeMessagesApp(shell, mesh, services.contacts));
    shell.addApp(makeMapApp(shell, services));
    shell.addApp(makeLibraryApp(shell));
    shell.addApp(makeLogbookApp(shell, services));
    shell.addApp(makeFilesApp(shell));
    shell.addApp(makeTerminalApp(shell, mesh));
    shell.addApp(makeSettingsApp(shell));
    shell.addApp(makeDiagnosticsApp(shell, services));
    shell.addApp(makeSecurityApp(shell, services));
    shell.addApp(makeGamesApp(shell));
    shell.addApp(makeToolsApp(shell));
}

} // namespace meshdeck
