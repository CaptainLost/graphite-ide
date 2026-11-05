#include <doctest/doctest.h>

#include "Git/Lines/ReadRetry.h"

using namespace Graphite;

TEST_CASE("Git window: a failed line read is retried after 1 then 2 then 4 seconds and so on")
{
	CHECK(RetryDelayAfter(0) == 0.0);
	CHECK(RetryDelayAfter(1) == 1.0);
	CHECK(RetryDelayAfter(2) == 2.0);
	CHECK(RetryDelayAfter(3) == 4.0);
	CHECK(RetryDelayAfter(4) == 8.0);
	CHECK(RetryDelayAfter(5) == 16.0);
	CHECK(RetryDelayAfter(6) == 32.0);
}

TEST_CASE("Git window: the wait before retrying a failed line read is at most a minute")
{
	CHECK(RetryDelayAfter(7) == 60.0);
	CHECK(RetryDelayAfter(8) == 60.0);
	CHECK(RetryDelayAfter(1'000) == 60.0);
}

TEST_CASE("Git window: each failed line read in a row moves the retry further off")
{
	ReadRetry retry;
	CHECK(IsRetryDue(retry, 0.0));

	retry = AfterFailedRead(retry, 10.0);
	CHECK(retry.Failures == 1);
	CHECK_FALSE(IsRetryDue(retry, 10.5));
	CHECK(IsRetryDue(retry, 11.0));

	retry = AfterFailedRead(retry, 11.0);
	CHECK_FALSE(IsRetryDue(retry, 12.5));
	CHECK(IsRetryDue(retry, 13.0));

	retry = AfterFailedRead(retry, 13.0);
	CHECK_FALSE(IsRetryDue(retry, 16.5));
	CHECK(IsRetryDue(retry, 17.0));
}

TEST_CASE("Git window: a reset back-off retries a failed line read after a second again")
{
	ReadRetry retry;

	for (int failure = 0; failure < 5; ++failure)
	{
		retry = AfterFailedRead(retry, 100.0);
	}

	CHECK(retry.DueAt == 116.0);

	retry = ReadRetry{};
	CHECK(IsRetryDue(retry, 100.0));

	retry = AfterFailedRead(retry, 100.0);
	CHECK(retry.Failures == 1);
	CHECK(retry.DueAt == 101.0);
}
