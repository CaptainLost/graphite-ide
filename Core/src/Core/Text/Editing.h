#pragma once

#include "Core/Text/Columns.h"
#include "Core/Text/DocumentEditing.h"
#include "Core/Text/RowMap.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace Core {

	class CancellationToken;
	struct OpenItem;
	class TextBuffer;

	struct LineCaches
	{
		std::size_t TabWidth = DefaultTabWidth;
		std::vector<std::size_t> IndentDepths;
		std::size_t WidestColumn = 0;
		std::size_t WidestLine = 0;
		RowMap Rows;
		std::uint64_t SeenChange = 0;
	};

	struct WidestLineFound
	{
		std::size_t Line = 0;
		std::size_t Column = 0;
	};

	std::optional<WidestLineFound> FindWidestLineUnlessCancelled(const TextBuffer& buffer, std::size_t tabWidth,
															const CancellationToken& cancellation);

	LineCaches ComputeLineCaches(const TextBuffer& buffer, std::size_t tabWidth, std::size_t columnsPerRow);

	std::optional<LineCaches> ComputeLineCachesUnlessCancelled(const TextBuffer& buffer, std::size_t tabWidth,
															   std::size_t columnsPerRow, const CancellationToken& cancellation);

	void AdoptLineCaches(OpenItem& item, LineCaches caches);

	bool InsertText(OpenItem& item, std::string_view utf8);

	bool DeleteBackward(OpenItem& item);

	bool DeleteForward(OpenItem& item);

	bool DeleteWordBackward(OpenItem& item);
	bool DeleteWordForward(OpenItem& item);

	bool Undo(OpenItem& item);
	bool Redo(OpenItem& item);

	void RecomputeLineCaches(OpenItem& item);

	enum class CaretFollow
	{
		// The edit that moved the text also placed the caret.
		AlreadyPlaced,

		// Someone else edited the text; the caret moves with it.
		Shift
	};

	// Brings the item's line caches, and with Shift its caret, up to its document's text.
	void CatchUpWithDocument(OpenItem& item, CaretFollow follow);

	bool InsertNewline(OpenItem& item);

	bool Indent(OpenItem& item, IndentStyle style);

	bool Outdent(OpenItem& item, IndentStyle style);

	bool ToggleLineComment(OpenItem& item, std::string_view commentPrefix);
}