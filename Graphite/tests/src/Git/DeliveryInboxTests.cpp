#include <doctest/doctest.h>

#include "Git/DeliveryInbox.h"

#include "Core/App/Redraw.h"

#include <thread>
#include <vector>

using Graphite::DeliveryInbox;

TEST_CASE("A posted delivery runs on Drain and not before")
{
	DeliveryInbox inbox;
	int runCount = 0;

	inbox.Post([&runCount] { ++runCount; });
	CHECK(runCount == 0);

	inbox.Drain();
	CHECK(runCount == 1);

	inbox.Drain();
	CHECK(runCount == 1);
}

TEST_CASE("Deliveries run in the order they were posted")
{
	DeliveryInbox inbox;
	std::vector<int> runOrder;

	inbox.Post([&runOrder] { runOrder.push_back(1); });
	inbox.Post([&runOrder] { runOrder.push_back(2); });
	inbox.Post([&runOrder] { runOrder.push_back(3); });
	inbox.Drain();

	CHECK(runOrder == std::vector<int>{1, 2, 3});
}

TEST_CASE("A delivery posted while draining waits for the next Drain")
{
	DeliveryInbox inbox;
	int nestedRunCount = 0;

	inbox.Post([&] { inbox.Post([&nestedRunCount] { ++nestedRunCount; }); });
	inbox.Drain();
	CHECK(nestedRunCount == 0);

	inbox.Drain();
	CHECK(nestedRunCount == 1);
}

TEST_CASE("Posting wakes the frame loop")
{
	Core::Redraw::TakeRequest();

	DeliveryInbox inbox;
	inbox.Post([] {});

	CHECK(Core::Redraw::TakeRequest());
}

TEST_CASE("Deliveries posted from other threads are all applied")
{
	DeliveryInbox inbox;
	int totalRunCount = 0;

	std::vector<std::thread> workers;

	for (int workerIndex = 0; workerIndex < 4; ++workerIndex)
	{
		workers.emplace_back([&inbox, &totalRunCount] {
			for (int postIndex = 0; postIndex < 100; ++postIndex)
			{
				inbox.Post([&totalRunCount] { ++totalRunCount; });
			}
		});
	}

	for (std::thread& worker : workers)
	{
		worker.join();
	}

	inbox.Drain();
	CHECK(totalRunCount == 400);
}
