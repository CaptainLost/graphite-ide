#include <doctest/doctest.h>

#include "Core/Events/EventBus.h"

#include <string>
#include <vector>

namespace {

	struct Opened
	{
		std::string Path;
	};

	struct Closed
	{
		int Code = 0;
	};

	void SubscribeCounterOfOpened(Core::EventBus& bus, int& calls)
	{
		bus.Subscribe<Opened>([&calls](const Opened&) { ++calls; });
	}
}

TEST_CASE("Publish reaches every handler subscribed to that event type")
{
	Core::EventBus bus;
	std::vector<std::string> seen;

	bus.Subscribe<Opened>([&seen](const Opened& event) { seen.push_back("a:" + event.Path); });
	bus.Subscribe<Opened>([&seen](const Opened& event) { seen.push_back("b:" + event.Path); });

	bus.Publish(Opened{.Path = "D:/repo"});

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

	bus.Publish(Closed{.Code = 3});

	CHECK(openedCalls == 0);
	CHECK(closedCalls == 1);
}

TEST_CASE("Publishing an event nobody listens for is a no-op")
{
	Core::EventBus bus;

	bus.Publish(Opened{.Path = "unused"});

	CHECK(bus.GetHandlerCount<Opened>() == 0);
}

TEST_CASE("A handler subscribed mid-dispatch starts from the next publish and the handler after its subscriber is still reached")
{
	Core::EventBus bus;
	int subscribingHandlerCalls = 0;
	int handlerAfterSubscriberCalls = 0;
	int handlerSubscribedMidDispatchCalls = 0;

	bus.Subscribe<Opened>(
		[&bus, &subscribingHandlerCalls, &handlerSubscribedMidDispatchCalls](const Opened&) {
			++subscribingHandlerCalls;
			SubscribeCounterOfOpened(bus, handlerSubscribedMidDispatchCalls);
		});

	SubscribeCounterOfOpened(bus, handlerAfterSubscriberCalls);

	REQUIRE(bus.GetHandlerCount<Opened>() == 2);

	bus.Publish(Opened{.Path = "one"});

	CHECK(subscribingHandlerCalls == 1);
	CHECK(handlerAfterSubscriberCalls == 1);
	CHECK(handlerSubscribedMidDispatchCalls == 0);
	CHECK(bus.GetHandlerCount<Opened>() == 3);

	bus.Publish(Opened{.Path = "two"});

	CHECK(subscribingHandlerCalls == 2);
	CHECK(handlerAfterSubscriberCalls == 2);
	CHECK(handlerSubscribedMidDispatchCalls == 1);
	CHECK(bus.GetHandlerCount<Opened>() == 4);
}
