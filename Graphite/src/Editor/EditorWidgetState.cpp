#include "Editor/EditorWidgetState.h"

#include <algorithm>

namespace Graphite {

	bool HistoryStepIsDue(const HistoryHold& hold, bool isRepeat, double now)
	{
		if (!isRepeat)
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
		return HistoryHold{.BeganAt = isRepeat ? hold.BeganAt : now, .LastStepAt = now};
	}

	bool CaretBlinkRestarts(int lastFocusedFrame, int frame)
	{
		return lastFocusedFrame != frame - 1;
	}
}
