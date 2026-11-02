// event_bus.cpp - see event_bus.h.
#include "event_bus.h"
#include <algorithm>

namespace meshdeck {

int EventBus::subscribe(EventType type, Handler handler) {
    subs_.push_back({nextId_, type, std::move(handler)});
    return nextId_++;
}

void EventBus::unsubscribe(int id) {
    subs_.erase(std::remove_if(subs_.begin(), subs_.end(), [id](const Sub& s) { return s.id == id; }), subs_.end());
}

bool EventBus::publish(Event e) {
    if (queue_.size() >= kMaxQueued) {
        dropped_++;
        return false;
    }
    queue_.push_back(std::move(e));
    return true;
}

size_t EventBus::dispatch() {
    // Take only the events already queued: anything published by a handler waits for the next
    // pass. The subscriber list is copied too, so a handler may (un)subscribe safely.
    std::deque<Event> batch;
    batch.swap(queue_);
    const std::vector<Sub> subs = subs_;
    for (const Event& e : batch)
        for (const Sub& s : subs)
            if (s.type == e.type && s.handler) s.handler(e);
    return batch.size();
}

} // namespace meshdeck
