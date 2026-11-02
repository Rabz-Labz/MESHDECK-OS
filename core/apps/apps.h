// apps.h - every app MESHDECK ships with, and the order of the home tiles.
#pragma once

namespace meshdeck {

class Shell;
class MeshService;
class Contacts;
class PositionService;

// The services the built-in apps share, created by each platform's main(). Contacts are shared so
// a nickname set in MESSAGES also labels that node on the MAP.
struct Services {
    MeshService& mesh;
    Contacts& contacts;
    PositionService& position;
};

// Adds the built-in apps to the shell in home-tile order (keys 1-9, 0, A):
//   1 MESSAGES  2 MAP  3 LIBRARY  4 LOGBOOK  5 FILES  6 TERMINAL  7 SETTINGS
//   8 DIAGNOSTICS  9 SECURITY  0 GAMES
// Apps whose 2.0 version isn't built yet appear as placeholders saying which milestone brings
// them (see docs/roadmap.md), so the layout and navigation can be used and tested today.
// It also wires the services into the shell: the mesh is read on every pass and its state shows
// in the status bar.
void addBuiltInApps(Shell& shell, Services& services);

} // namespace meshdeck
