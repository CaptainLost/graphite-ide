#pragma once

#include "Git/Blame/BlameModel.h"
#include "Git/Blame/LineAuthors.h"

#include "Core/Git/Diff.h"
#include "Core/Git/Oid.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	struct PeekDiffLine
	{
		Core::DiffLineKind Kind = Core::DiffLineKind::Context;
		std::string Text;   // without its line end
		bool Tracked = false;
	};

	struct PeekEntry
	{
		std::string Title;
		std::string Meta;     // "author · 3 months ago · e51a8f0", or "You"
		std::string Byline;   // "author · 14 Jul 2026, 16:42", or "You · not committed yet"
		std::vector<PeekDiffLine> Lines;
		std::optional<Core::Oid> Commit;   // empty for the uncommitted entry
		std::filesystem::path Path;        // the file's path in that commit
	};

	enum class PeekState : std::uint8_t
	{
		Loading,
		Failed,
		Ready
	};

	struct LinePeekContent
	{
		PeekState State = PeekState::Loading;
		std::string Header;   // "History of line 24 · 3 changes · follows renames"
		std::string Error;
		std::vector<PeekEntry> Entries;
		bool StopsEarly = false;
	};

	struct LinePeekInput
	{
		std::size_t Line = 0;   // zero-based buffer line
		const LineAuthors* Authors = nullptr;
		std::string_view BufferLine;
		const LineHistoryState* History = nullptr;
		std::chrono::sys_seconds Now{};
		std::chrono::minutes Offset{};
	};

	LinePeekContent BuildLinePeekContent(const LinePeekInput& input);
}
