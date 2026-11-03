#include <doctest/doctest.h>

#include "Core/App/Redraw.h"
#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/LatestAnswer.h"

#include "DeferredJobs.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

	// Holds every job so a test can run them out of the order they were asked in, as two workers might.
	class HeldJobs
	{
	public:
		Core::JobSubmitter Submitter()
		{
			return [this](std::function<void()> job) { m_Jobs.push_back(std::move(job)); };
		}

		void Run(std::size_t jobIndex) { m_Jobs.at(jobIndex)(); }

	private:
		std::vector<std::function<void()>> m_Jobs;
	};

	Core::JobSubmitter RunWhereSubmitted()
	{
		return [](std::function<void()> job) { job(); };
	}

	Core::LatestAnswer<std::string>::Work Answering(std::string answer)
	{
		return [answer = std::move(answer)](const Core::CancellationToken&) { return answer; };
	}
}

TEST_CASE("The answer to the only question is taken once")
{
	DeferredJobs jobs;
	Core::LatestAnswer<std::string> latest{jobs.Submitter()};

	latest.Ask(Answering("listing"));
	CHECK(latest.IsInFlight());
	CHECK_FALSE(latest.TakeNewest().has_value());

	jobs.RunAll();

	CHECK(latest.TakeNewest() == std::optional<std::string>{"listing"});
	CHECK_FALSE(latest.IsInFlight());
	CHECK_FALSE(latest.TakeNewest().has_value());
}

TEST_CASE("Only the newest question's answer is taken when both land")
{
	DeferredJobs jobs;
	Core::LatestAnswer<std::string> latest{jobs.Submitter()};

	latest.Ask(Answering("old"));
	latest.Ask(Answering("new"));
	jobs.RunAll();

	CHECK(latest.TakeNewest() == std::optional<std::string>{"new"});
}

TEST_CASE("An earlier question's answer landing after the newest is dropped")
{
	HeldJobs jobs;
	Core::LatestAnswer<std::string> latest{jobs.Submitter()};

	latest.Ask(Answering("old"));
	latest.Ask(Answering("new"));

	jobs.Run(1);
	CHECK(latest.TakeNewest() == std::optional<std::string>{"new"});

	jobs.Run(0);
	CHECK_FALSE(latest.TakeNewest().has_value());
}

TEST_CASE("An answer to an earlier question is not taken while the newest is still being worked out")
{
	HeldJobs jobs;
	Core::LatestAnswer<std::string> latest{jobs.Submitter()};

	latest.Ask(Answering("old"));
	latest.Ask(Answering("new"));

	jobs.Run(0);

	CHECK_FALSE(latest.TakeNewest().has_value());
	CHECK(latest.IsInFlight());
}

TEST_CASE("A forgotten question's answer is dropped when it lands")
{
	DeferredJobs jobs;
	Core::LatestAnswer<std::string> latest{jobs.Submitter()};

	latest.Ask(Answering("unwanted"));
	latest.Forget();
	CHECK_FALSE(latest.IsInFlight());

	jobs.RunAll();

	CHECK_FALSE(latest.TakeNewest().has_value());
}

TEST_CASE("Asking again cancels the work asked before")
{
	HeldJobs jobs;
	Core::LatestAnswer<std::string> latest{jobs.Submitter()};
	std::optional<Core::CancellationToken> firstToken;

	latest.Ask([&firstToken](const Core::CancellationToken& cancellation) {
		firstToken = cancellation;
		return std::string{"first"};
	});
	jobs.Run(0);
	REQUIRE(firstToken.has_value());
	CHECK_FALSE(firstToken->IsCancelled());

	latest.Ask(Answering("second"));

	CHECK(firstToken->IsCancelled());
}

TEST_CASE("Forgetting cancels the work under way")
{
	HeldJobs jobs;
	Core::LatestAnswer<std::string> latest{jobs.Submitter()};
	std::optional<Core::CancellationToken> token;

	latest.Ask([&token](const Core::CancellationToken& cancellation) {
		token = cancellation;
		return std::string{"answer"};
	});
	jobs.Run(0);

	latest.Forget();

	REQUIRE(token.has_value());
	CHECK(token->IsCancelled());
}

TEST_CASE("Work that runs where it is asked has its answer ready as soon as the question returns")
{
	Core::LatestAnswer<std::string> latest{RunWhereSubmitted()};

	latest.Ask(Answering("at once"));

	CHECK(latest.TakeNewest() == std::optional<std::string>{"at once"});
}

TEST_CASE("Work that finishes after its asker is gone lands nowhere and harms nothing")
{
	HeldJobs jobs;

	{
		Core::LatestAnswer<std::string> latest{jobs.Submitter()};
		latest.Ask(Answering("late"));
	}

	jobs.Run(0);
}

TEST_CASE("A moved answer keeps the question in flight, and the one it was moved from cancels nothing on its way out")
{
	HeldJobs jobs;
	std::optional<Core::CancellationToken> token;
	std::optional<Core::LatestAnswer<std::string>> moved;

	{
		Core::LatestAnswer<std::string> latest{jobs.Submitter()};
		latest.Ask([&token](const Core::CancellationToken& cancellation) {
			token = cancellation;
			return std::string{"carried"};
		});

		moved.emplace(std::move(latest));
	}

	jobs.Run(0);

	REQUIRE(token.has_value());
	CHECK_FALSE(token->IsCancelled());
	CHECK(moved->IsInFlight());
	CHECK(moved->TakeNewest() == std::optional<std::string>{"carried"});
}

TEST_CASE("A landed answer asks for a frame, so it is taken without waiting for the idle tick")
{
	DeferredJobs jobs;
	Core::LatestAnswer<std::string> latest{jobs.Submitter()};

	latest.Ask(Answering("listing"));
	static_cast<void>(Core::Redraw::TakeRequest());

	jobs.RunAll();

	CHECK(Core::Redraw::TakeRequest());
}
