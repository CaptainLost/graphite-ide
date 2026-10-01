#include <doctest/doctest.h>

#include "Git/DeliveryInbox.h"

#include "Core/App/Redraw.h"

#include <thread>
#include <vector>

using Graphite::DeliveryInbox;

TEST_CASE("A posted delivery runs on Drain and not before")
{
	DeliveryInbox inbox;
	int ran = 0;

	inbox.Post([&ran] { ++ran; });
	CHECK(ran == 0);

	inbox.Drain();
	CHECK(ran == 1);

	inbox.Drain();
	CHECK(ran == 1);
}

TEST_CASE("Deliveries run in the order they were posted")
{
	DeliveryInbox inbox;
	std::vector<int> order;

	inbox.Post([&order] { order.push_back(1); });
	inbox.Post([&order] { order.push_back(2); });
	inbox.Post([&order] { order.push_back(3); });
	inbox.Drain();

	CHECK(order == std::vector<int>{1, 2, 3});
}

TEST_CASE("A delivery posted while draining waits for the next Drain")
{
	DeliveryInbox inbox;
	int inner = 0;

	inbox.Post([&] { inbox.Post([&inner] { ++inner; }); });
	inbox.Drain();
	CHECK(inner == 0);

	inbox.Drain();
	CHECK(inner == 1);
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
	int sum = 0;

	std::vector<std::thread> workers;

	for (int t = 0; t < 4; ++t)
	{
		workers.emplace_back([&inbox, &sum] {
			for (int i = 0; i < 100; ++i)
			{
				inbox.Post([&sum] { ++sum; });
			}
		});
	}

	for (auto& worker : workers)
	{
		worker.join();
	}

	inbox.Drain();
	CHECK(sum == 400);
}
