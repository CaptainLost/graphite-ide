#pragma once

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Graphite {

	struct LinePeekContent;
	struct ResolvedScheme;

	// How many editor rows the peek under a line takes.
	inline constexpr std::size_t kLinePeekRows = 9;

	struct PeekAction
	{
		enum class Kind : std::uint8_t
		{
			Close,
			Select,
			ShowCommit
		};

		Kind Action = Kind::Close;
		std::size_t Entry = 0;

		friend bool operator==(const PeekAction&, const PeekAction&) = default;
	};

	// Draws the peek into the current window, whose content region is `size`: the header with a close button, the
	// list of entries on the left and the selected entry's diff on the right. The diff is set in the font size in
	// effect when this is called, which is the editor's. Returns what was clicked this frame, if anything.
	std::optional<PeekAction> DrawLinePeek(const LinePeekContent& content, std::size_t selected, ImVec2 size,
										   const ResolvedScheme& scheme);

	struct PeekFrame
	{
		std::size_t PeekLine = 0;
		std::size_t CaretLine = 0;
		bool Edited = false;
		bool CloseRequested = false;   // the close button or Escape
	};

	// True when the caret is on another line, the document was edited, or the reader asked to close the peek.
	bool PeekClosedBy(const PeekFrame& frame);
}
