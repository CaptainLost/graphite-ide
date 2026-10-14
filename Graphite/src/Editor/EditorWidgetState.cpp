#include "Editor/EditorWidgetState.h"

#include <algorithm>

namespace Graphite {
	namespace {
		bool IsFreshPress(const HistoryHold& hold, bool isRepeat, double now)
		{
			return !isRepeat || now - hold.LastStepAt > kHistoryHoldLapse;
		}
	}

	bool HistoryStepIsDue(const HistoryHold& hold, bool isRepeat, double now)
	{
		if (IsFreshPress(hold, isRepeat, now))
		{
			return true;
		}

		const double held = now - hold.BeganAt;

		if (held < kHistoryHoldDelay)
		{
			return false;
		}

		const double interval =
			std::max(kHistoryFastestInterval,
					 kHistoryFirstInterval - (held - kHistoryHoldDelay) * kHistoryRampPerSecond);

		return now - hold.LastStepAt >= interval;
	}

	HistoryHold TakeHistoryStep(const HistoryHold& hold, bool isRepeat, double now)
	{
		return HistoryHold{.BeganAt = IsFreshPress(hold, isRepeat, now) ? now : hold.BeganAt,
						   .LastStepAt = now};
	}

	bool CaretBlinkRestarts(int lastFocusedFrame, int frame)
	{
		return lastFocusedFrame != frame - 1;
	}
}
