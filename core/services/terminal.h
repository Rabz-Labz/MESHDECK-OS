// terminal.h - the TERMINAL's commands: the same reports the USB serial port gives, typed on the
// screen. A command is one line of text in and a few lines out. The commands only read; the two
// that touch the screen (CLEAR and SCREENSHOT) are returned as an Action for the app to carry out.
#pragma once
#include <stdint.h>
#include <functional>
#include <string>
#include <vector>

namespace meshdeck {

class MeshService;
class Platform;
class PowerMonitor;

class Terminal {
public:
    // What the screen must do after a command: CLEAR empties the output, SCREENSHOT saves one.
    enum class Action { None, Clear, Screenshot };
    struct Result {
        std::vector<std::string> lines;
        Action action = Action::None;
    };
    struct Context {
        Platform& platform;
        MeshService& mesh;
        PowerMonitor& power;
        int capacityMah;                  // the battery's capacity, for the time-left estimate
        std::function<int64_t()> clock;   // UTC seconds, 0 when the clock isn't set
    };
    struct Command {
        std::string name, help;
    };

    // Every command, in the order HELP lists them.
    static const std::vector<Command>& commands();
    // Runs one typed line. Command names are not case-sensitive; a blank line gives nothing.
    static Result run(const std::string& line, Context& ctx);
};

} // namespace meshdeck
