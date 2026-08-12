#include <doctest/doctest.h>

#include "Core/Event/EventBus.h"

#include <string>
#include <vector>

namespace {
	struct Opened
	{
		std::string path;
	};

	struct Closed
	{
		int code = 0;
	};
}

TEST_CASE("Publish reaches every handler subscribed to that event type")
{
	Core::EventBus bus;
	std::vector<std::string> seen;

	bus.Subscribe<Opened>([&seen](const Opened& event) { seen.push_back("a:" + event.path); });
	bus.Subscribe<Opened>([&seen](const Opened& event) { seen.push_back("b:" + event.path); });

	bus.Publish(Opened{"D:/repo"});

	REQUIRE(seen.size() == 2);
	CHECK(seen[0] == "a:D:/repo");
	CHECK(seen[1] == "b:D:/repo");
}

TEST_CASE("Handlers of one event type never see another")
{
	Core::EventBus bus;
	int openedCalls = 0;
	int closedCalls = 0;

	bus.Subscribe<Opened>([&openedCalls](const Opened&) { ++openedCalls; });
	bus.Subscribe<Closed>([&closedCalls](const Closed&) { ++closedCalls; });

	bus.Publish(Closed{3});

	CHECK(openedCalls == 0);
	CHECK(closedCalls == 1);
}

TEST_CASE("Publishing an event nobody listens for is a no-op")
{
	Core::EventBus bus;

	bus.Publish(Opened{"unused"});

	CHECK(bus.GetHandlerCount<Opened>() == 0);
}

TEST_CASE("Subscribing from inside a handler does not disturb the running dispatch")
{
	Core::EventBus bus;
	int firstCalls = 0;
	int secondCalls = 0;
	int lateCalls = 0;

	// Two handlers must already be registered: with only one, the dispatch loop
	// never dereferences an iterator after the reallocation, so the copy in
	// Publish would look unnecessary even though it is not.
	bus.Subscribe<Opened>(
		[&bus, &firstCalls, &lateCalls](const Opened&) {
			++firstCalls;
			bus.Subscribe<Opened>([&lateCalls](const Opened&) { ++lateCalls; });
		});

	bus.Subscribe<Opened>([&secondCalls](const Opened&) { ++secondCalls; });

	bus.Publish(Opened{"one"});

	CHECK(firstCalls == 1);
	CHECK(secondCalls == 1); // reached even though the vector grew mid-dispatch
	CHECK(lateCalls == 0);	 // the new handler starts from the next publish
	CHECK(bus.GetHandlerCount<Opened>() == 3);

	bus.Publish(Opened{"two"});

	CHECK(firstCalls == 2);
	CHECK(secondCalls == 2);
	CHECK(lateCalls == 1);
	CHECK(bus.GetHandlerCount<Opened>() == 4);
}
