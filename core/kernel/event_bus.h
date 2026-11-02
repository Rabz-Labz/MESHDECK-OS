// event_bus.h - how services tell apps that something happened, without knowing who listens.
//
// A service publishes an Event (a message arrived, the battery level changed, the deck locked).
// Apps and other services subscribe to the event types they care about. Events are queued and
// delivered by dispatch(), which the main loop calls once per pass, so:
//   - handlers always run on the main loop, never inside a driver or an interrupt;
//   - a handler may publish more events; they are delivered on the next dispatch, never
//     recursively, so one event can't cause an endless chain within a single pass;
//   - the queue is bounded (kMaxQueued). When full, new events are dropped and counted rather
//     than growing memory without limit.
#pragma once
#include <stdint.h>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace meshdeck {

enum class EventType : uint16_t {
    MessageReceived,   // a: channel, b: 0, text: "from|text"
    MessageState,      // a: message id (low 31 bits), b: new state
    PositionChanged,   // text: source
    BatteryChanged,    // a: percent (-1 unknown), b: milliamps out of the battery
    LockChanged,       // a: 1 locked, 0 unlocked
    SettingsChanged,   // text: key
    StorageChanged,    // a: 1 card present, 0 removed
    PowerProfile,      // a: profile number
    Custom,            // for apps; text says what
};

struct Event {
    Event(EventType type, int32_t a = 0, int32_t b = 0, std::string text = "")
        : type(type), a(a), b(b), text(std::move(text)) {}
    EventType type;
    int32_t a;
    int32_t b;
    std::string text;
};

class EventBus {
public:
    using Handler = std::function<void(const Event&)>;
    static constexpr size_t kMaxQueued = 64;

    // Registers a handler for one event type. Returns an id for unsubscribe().
    int subscribe(EventType type, Handler handler);
    void unsubscribe(int id);

    // Queues an event for the next dispatch(). False (and counted) if the queue is full.
    bool publish(Event e);
    // Delivers the events queued before this call, oldest first, to every subscriber of their
    // type. Returns how many events were delivered.
    size_t dispatch();

    size_t pending() const { return queue_.size(); }
    uint32_t dropped() const { return dropped_; }

private:
    struct Sub {
        int id;
        EventType type;
        Handler handler;
    };
    std::vector<Sub> subs_;
    std::deque<Event> queue_;
    int nextId_ = 1;
    uint32_t dropped_ = 0;
};

} // namespace meshdeck
