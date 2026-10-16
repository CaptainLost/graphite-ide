#include <doctest/doctest.h>

#include "Core/App/Redraw.h"
#include "Core/Jobs/Mailbox.h"

#include <string>
#include <thread>
#include <vector>

TEST_CASE("A mailbox hands over what was posted, in the order posted, once")
{
	Core::Mailbox<std::string> mailbox;

	mailbox.Post("first");
	mailbox.Post("second");

	CHECK(mailbox.TakeAll() == std::vector<std::string>{"first", "second"});
	CHECK(mailbox.TakeAll().empty());
}

TEST_CASE("A post asks for a frame, so the UI thread takes it without waiting for its idle tick")
{
	Core::Mailbox<int> mailbox;
	static_cast<void>(Core::Redraw::TakeRequest());

	mailbox.Post(1);

	CHECK(Core::Redraw::TakeRequest());
}

TEST_CASE("A mailbox loses nothing posted from several threads at once")
{
	constexpr int kThreads = 4;
	constexpr int kPostsPerThread = 1000;

	Core::Mailbox<int> mailbox;
	std::vector<std::thread> posters;

	for (int threadIndex = 0; threadIndex < kThreads; ++threadIndex)
	{
		posters.emplace_back([&mailbox] {
			for (int postIndex = 0; postIndex < kPostsPerThread; ++postIndex)
			{
				mailbox.Post(postIndex);
			}
		});
	}

	for (std::thread& poster : posters)
	{
		poster.join();
	}

	CHECK(mailbox.TakeAll().size() == static_cast<std::size_t>(kThreads * kPostsPerThread));
}

TEST_CASE("A cleared mailbox hands over nothing posted before the clear")
{
	Core::Mailbox<int> mailbox;
	mailbox.Post(1);

	mailbox.Clear();

	CHECK(mailbox.TakeAll().empty());
}