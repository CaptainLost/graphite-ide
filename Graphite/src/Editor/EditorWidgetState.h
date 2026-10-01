#pragma once

#include "Editor/BracketMatch.h"

#include <cstddef>
#include <limits>
#include <optional>

namespace Graphite {

	// Stepping through the history runs on its own clock rather than on the keyboard's
	// auto-repeat, which is tuned for typing: at the ~30 a second Windows delivers, a held undo
	// is a dozen changes past the one being looked for before the key comes up. A history step
	// is a whole change — a word, a line, a pasted block — so it wants to be slow enough to
	// watch and stop.
	inline constexpr double kHistoryHoldDelay = 0.4;

	inline constexpr double kHistoryFirstInterval = 0.14;

	inline constexpr double kHistoryFastestInterval = 0.06;

	// How much of the interval a second of holding takes away, so the stream tightens from the
	// first rate to the fastest over roughly three seconds.
	inline constexpr double kHistoryRampPerSecond = 0.027;

	// When the undo or redo being held down was first pressed, and when it last took a step.
	struct HistoryHold
	{
		double BeganAt = 0.0;
		double LastStepAt = 0.0;
	};

	// Whether a held undo or redo is allowed to take another step at `now`. A fresh press always
	// is; a repeat waits out kHistoryHoldDelay and then steps at an interval that tightens the
	// longer the key stays down. Pure: TakeHistoryStep is what records a step once taken.
	bool HistoryStepIsDue(const HistoryHold& hold, bool isRepeat, double now);

	// The hold after a step taken at `now`. A fresh press starts the hold over; a repeat moves
	// only the last step, so the ramp keeps measuring from the press.
	HistoryHold TakeHistoryStep(const HistoryHold& hold, bool isRepeat, double now);

	// What EditorWidgetState::LastFocusedFrame holds for a document whose caret has never been
	// drawn. ImGui counts frames up from one, so this is never the frame before any of them.
	inline constexpr int kNeverFocusedFrame = std::numeric_limits<int>::min();

	// Whether the caret's blink starts over on `frame`: true unless the document already held
	// the keyboard on the frame before. Switching documents is not a caret movement, so without
	// this the caret in the newly chosen document would inherit the phase it had when it was
	// last left and could spend its first half-second dark, exactly when the reader is looking
	// for it.
	bool CaretBlinkRestarts(int lastFocusedFrame, int frame);

	// What the last bracket search for one document was asked, and what it answered. Finding a
	// pair walks up to kBracketScanLines in each direction and re-lexes every line it reads,
	// because the syntax cache holds exactly one line's tokens: cheap for a partner a few lines
	// away, and the whole bound every time when the caret sits beside an unclosed brace — which
	// is precisely the state a file is in while it is being typed into. So the answer is kept
	// and only recomputed when the caret or the buffer has moved.
	//
	// The buffer revision is the same staleness signal RefreshFindMatchesIfStale compares
	// against, and it is the only one an edit can arrive through; the caret offset covers every
	// move that leaves the text alone.
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

	// What the editor widget remembers about one document from frame to frame. Owned by
	// whoever draws the documents and kept per document beside the syntax cache and the
	// overlays, so two editors on screen never share a blink, a held undo or a bracket answer,
	// however many of them there are.
	struct EditorWidgetState
	{
		// The blink is measured from the last caret movement.
		double CaretMovedAt = 0.0;

		// The ImGui frame on which this document last held the keyboard. See CaretBlinkRestarts.
		int LastFocusedFrame = kNeverFocusedFrame;

		HistoryHold History;

		BracketMemo Brackets;
	};
}
