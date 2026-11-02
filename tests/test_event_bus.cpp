// test_event_bus.cpp - delivery order, unsubscribe, re-entrancy and the queue limit.
#include <vector>
#include "doctest.h"
#include "kernel/event_bus.h"

using namespace meshdeck;

TEST_CASE("events reach only subscribers of their type, in the order published") {
    EventBus bus;
    std::vector<int> got;
    bus.subscribe(EventType::BatteryChanged, [&](const Event& e) { got.push_back(e.a); });
    bus.subscribe(EventType::LockChanged, [&](const Event&) { got.push_back(-1); });
    bus.publish({EventType::BatteryChanged, 80});
    bus.publish({EventType::BatteryChanged, 79});
    CHECK(got.empty());   // nothing happens until dispatch()
    CHECK(bus.dispatch() == 2);
    CHECK(got == std::vector<int>{80, 79});
}

TEST_CASE("unsubscribe stops delivery") {
    EventBus bus;
    int n = 0;
    int id = bus.subscribe(EventType::Custom, [&](const Event&) { n++; });
    bus.publish({EventType::Custom});
    bus.dispatch();
    bus.unsubscribe(id);
    bus.publish({EventType::Custom});
    bus.dispatch();
    CHECK(n == 1);
}

TEST_CASE("an event published by a handler waits for the next dispatch (no recursion)") {
    EventBus bus;
    int depth = 0, calls = 0;
    bus.subscribe(EventType::Custom, [&](const Event& e) {
        calls++;
        depth++;
        CHECK(depth == 1);
        if (e.a < 3) bus.publish({EventType::Custom, e.a + 1});
        depth--;
    });
    bus.publish({EventType::Custom, 0});
    CHECK(bus.dispatch() == 1);
    CHECK(calls == 1);
    CHECK(bus.pending() == 1);
    bus.dispatch();
    bus.dispatch();
    bus.dispatch();
    CHECK(calls == 4);
    CHECK(bus.pending() == 0);
}

TEST_CASE("the queue is bounded: overflow is dropped and counted") {
    EventBus bus;
    for (size_t i = 0; i < EventBus::kMaxQueued; i++) CHECK(bus.publish({EventType::Custom}));
    CHECK_FALSE(bus.publish({EventType::Custom}));
    CHECK(bus.dropped() == 1);
    CHECK(bus.dispatch() == EventBus::kMaxQueued);
}
