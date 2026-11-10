#include <doctest/doctest.h>

#include "Git/GitWorker.h"

#include <poll.h>

#include <chrono>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

	std::vector<Core::AgentReply> WaitForReplies(Agent::GitWorker& worker, std::size_t count)
	{
		std::vector<Core::AgentReply> replies;
		const auto deadline = std::chrono::steady_clock::now() + 3s;

		while (replies.size() < count && std::chrono::steady_clock::now() < deadline)
		{
			pollfd ready{.fd = worker.GetDescriptor(), .events = POLLIN, .revents = 0};
			::poll(&ready, 1, 50);

			for (Core::AgentReply& reply : worker.TakeFinished())
			{
				replies.push_back(std::move(reply));
			}
		}

		return replies;
	}
}

TEST_CASE("A git job's answer comes back as the reply to its request, and the descriptor says when")
{
	Agent::GitWorker worker;

	worker.Submit(Core::AgentRequest{.Id = 7, .Operation = "git.test"}, [](const Core::AgentRequest& request) -> Core::Result<Agent::GitAnswer> {
		return Agent::GitAnswer{.Result = {{"op", request.Operation}}, .Payload = "bytes"};
	});

	const std::vector<Core::AgentReply> replies = WaitForReplies(worker, 1);

	REQUIRE(replies.size() == 1);
	CHECK(replies[0].Id == 7);
	CHECK(replies[0].Result.at("op") == "git.test");
	CHECK(replies[0].Payload == "bytes");
}

TEST_CASE("A failed git job replies with its error")
{
	Agent::GitWorker worker;

	worker.Submit(Core::AgentRequest{.Id = 3, .Operation = "git.test"}, [](const Core::AgentRequest&) -> Core::Result<Agent::GitAnswer> {
		return Core::Error{Core::ErrorCode::NotFound, "no repository"};
	});

	const std::vector<Core::AgentReply> replies = WaitForReplies(worker, 1);

	REQUIRE(replies.size() == 1);
	REQUIRE(replies[0].Failure.has_value());
	CHECK(replies[0].Failure->Message == "no repository");
}

TEST_CASE("An answer past the frame limit is refused as too large, not sent to break the stream")
{
	Agent::GitWorker worker;

	worker.Submit(Core::AgentRequest{.Id = 5, .Operation = "git.diff"}, [](const Core::AgentRequest&) -> Core::Result<Agent::GitAnswer> {
		return Agent::GitAnswer{.Payload = std::string(Core::kMaxFramePayloadBytes + 1, 'x')};
	});

	const std::vector<Core::AgentReply> replies = WaitForReplies(worker, 1);

	REQUIRE(replies.size() == 1);
	REQUIRE(replies[0].Failure.has_value());
	CHECK(replies[0].Failure->Code == Core::ErrorCode::TooLarge);
	CHECK(replies[0].Payload.empty());
}

TEST_CASE("Git jobs run in the order they came, one at a time")
{
	Agent::GitWorker worker;
	std::vector<int> order;

	for (int index = 0; index < 3; ++index)
	{
		worker.Submit(Core::AgentRequest{.Id = static_cast<std::uint64_t>(index)}, [&order, index](const Core::AgentRequest&) -> Core::Result<Agent::GitAnswer> {
			order.push_back(index);
			return Agent::GitAnswer{};
		});
	}

	REQUIRE(WaitForReplies(worker, 3).size() == 3);
	CHECK(order == std::vector<int>{0, 1, 2});
}
