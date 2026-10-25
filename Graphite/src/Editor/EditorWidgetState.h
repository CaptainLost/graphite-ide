#pragma once

#include "Editor/BracketMatch.h"
#include "Editor/EditorCommands.h"
#include "Editor/Find/SelectionOccurrences.h"

#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

namespace Graphite {

	struct PendingEditorAction
	{
		EditorAction Action = EditorAction::Copy;
		bool FromMenu = false;
	};

	inline constexpr double kHistoryHoldDelay = 0.4;

	inline constexpr double kHistoryFirstInterval = 0.14;

	inline constexpr double kHistoryFastestInterval = 0.06;

	inline constexpr double kHistoryRampPerSecond = 0.027;

	inline constexpr double kHistoryHoldLapse = 1.5;

	struct HistoryHold
	{
		double BeganAt = 0.0;
		double LastStepAt = 0.0;
	};

	bool HistoryStepIsDue(const HistoryHold& hold, bool isRepeat, double now);

	HistoryHold TakeHistoryStep(const HistoryHold& hold, bool isRepeat, double now);

	inline constexpr int kNeverFocusedFrame = std::numeric_limits<int>::min();

	bool CaretBlinkRestarts(int lastFocusedFrame, int frame);

	struct BracketMemo
	{
		std::size_t Caret = 0;
		std::size_t Revision = 0;
		bool Filled = false;
		std::optional<BracketPair> Pair;

		bool Holds(std::size_t caret, std::size_t revision) const
		{
			return Filled && Caret == caret && Revision == revision;
		}

		void Fill(std::size_t caret, std::size_t revision, std::optional<BracketPair> pair)
		{
			Caret = caret;
			Revision = revision;
			Filled = true;
			Pair = pair;
		}
	};

	struct EditorWidgetState
	{
		double CaretMovedAt = 0.0;

		int LastFocusedFrame = kNeverFocusedFrame;

		HistoryHold History;

		BracketMemo Brackets;

		OccurrenceMemo Occurrences;

		std::vector<PendingEditorAction> PendingActions;
	};
}
