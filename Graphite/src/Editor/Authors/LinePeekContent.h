#pragma once

#include "Git/Blame/BlameModel.h"
#include "Git/Blame/LineAuthors.h"

#include "Core/Git/Diff.h"
#include "Core/Git/Oid.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
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
		bool Omitted = false;   // a muted row standing for lines of the hunk left out; its text is the ellipsis
	};

	struct PeekEntry
	{
		std::string Title;

		// The list's second line, "author · 3 months ago · e51a8f0" or "You", in two parts: a narrow list cuts the name
		// alone, so the age and the short hash after it stay whole.
		std::string MetaName;   // "author", or "You"
		std::string MetaRest;   // " · 3 months ago · e51a8f0"; empty after "You"

		std::string Byline;   // "author · 2026-07-14 16:42", or "You · not committed yet"

		// Only the shown entry's, each hunk cut to the change around the tracked line and a few lines of context.
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
		std::string Header;   // "History of line 24"
		std::string Counts;   // "3 changes · follows renames"; empty until Ready
		std::string Error;
		std::vector<PeekEntry> Entries;
		bool StopsEarly = false;
	};

	struct LinePeekInput
	{
		std::size_t Line = 0;       // zero-based buffer line
		std::size_t Selected = 0;   // the entry whose diff is built; past the end, the first
		const LineAuthors* Authors = nullptr;
		std::string_view BufferLine;
		const LineHistoryState* History = nullptr;
		std::chrono::sys_seconds Now{};
		const std::chrono::time_zone* Zone = nullptr;   // the reader's; null reads as UTC
	};

	LinePeekContent BuildLinePeekContent(const LinePeekInput& input);

	// What a peek's content is built from.
	struct LinePeekSource
	{
		Core::Ref<const LineAuthors> Authors;
		Core::Ref<const Core::LineHistory> History;
		std::optional<std::string> HistoryError;
		std::size_t Line = 0;
		std::size_t Selected = 0;

		friend bool operator==(const LinePeekSource&, const LinePeekSource&) = default;
	};

	// One peek's content, built again only when its source changes or the ages it shows are a minute old: building it
	// walks the history's hunks and HEAD's text, too much to repeat every frame.
	class LinePeekCache
	{
	public:
		static constexpr std::chrono::minutes kAgesRefresh{1};

		const LinePeekContent& Get(const LinePeekSource& source, std::chrono::sys_seconds now,
								   const std::function<LinePeekContent()>& build);

	private:
		std::optional<LinePeekSource> m_Source;
		std::chrono::sys_seconds m_BuiltAt{};
		LinePeekContent m_Content;
	};
}
