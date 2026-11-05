#include "Git/Lines/ReadRetry.h"

#include <algorithm>

namespace Graphite {

	double RetryDelayAfter(std::size_t failures)
	{
		if (failures == 0)
		{
			return 0.0;
		}

		double delay = kFirstRetrySeconds;

		for (std::size_t failure = 1; failure < failures && delay < kLongestRetrySeconds; ++failure)
		{
			delay *= 2.0;
		}

		return std::min(delay, kLongestRetrySeconds);
	}

	ReadRetry AfterFailedRead(const ReadRetry& retry, double now)
	{
		const std::size_t failures = retry.Failures + 1;

		return ReadRetry{.Failures = failures, .DueAt = now + RetryDelayAfter(failures)};
	}

	bool IsRetryDue(const ReadRetry& retry, double now)
	{
		return now >= retry.DueAt;
	}
}
