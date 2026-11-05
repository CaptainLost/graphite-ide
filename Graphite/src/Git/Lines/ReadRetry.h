#pragma once

#include <cstddef>

namespace Graphite {

	// When a line read that failed is asked again: the first time a second after the failure, then twice as long after
	// each failure in a row, at most a minute, so a read that keeps failing is not read again on every refresh.
	struct ReadRetry
	{
		std::size_t Failures = 0; // reads failed in a row since the back-off was last reset
		double DueAt = 0.0;       // the clock time, in seconds, from which the read may be asked again
	};

	inline constexpr double kFirstRetrySeconds = 1.0;
	inline constexpr double kLongestRetrySeconds = 60.0;

	// 0 before any failure; 1, 2, 4, … seconds after the first, second, third failure in a row; never more than a minute.
	double RetryDelayAfter(std::size_t failures);

	ReadRetry AfterFailedRead(const ReadRetry& retry, double now);

	bool IsRetryDue(const ReadRetry& retry, double now);
}
