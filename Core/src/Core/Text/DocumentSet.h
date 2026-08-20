#pragma once

#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextDocument.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Core {

	enum class OpenFailure
	{
		None,
		Binary,
		TooLarge,
		Missing,
		ReadFailed
	};

	inline constexpr std::size_t NoRowAim = static_cast<std::size_t>(-1);

	// What a document remembers about the find bar between one visit and the next. Lives here
	// rather than beside the editor so closing or renaming a document takes it along, which a
	// map kept next to the editor would have to be told about.
	//
	// Deliberately plain: a string, two flags, offset pairs, two indices. The matcher and its
	// query type live above Core and must stay there — a text editor remembering what you last
	// searched for is a text-editing concept, a search query carrying a file mask is not.
	struct FindState
	{
		bool Open = false;

		std::string Query;
		bool MatchCase = false;
		bool WholeWord = false;

		// Ascending document byte ranges, and which of them the caret is on.
		std::vector<std::pair<std::size_t, std::size_t>> Matches;
		std::size_t Current = 0;

		// The buffer revision Matches were computed from. Comparing it against the buffer's
		// current one is what makes a stale list detectable rather than something the editor has
		// to remember to invalidate on every edit path.
		//
		// Zero is a real revision — a buffer starts there — so it cannot double as a "never
		// computed" sentinel. An empty Query is what says the state has never been searched
		// with; Revision only says whether a list that exists is still current.
		std::size_t Revision = 0;
	};

	struct OpenItem
	{
		std::filesystem::path Path;
		std::optional<TextDocument> Document;
		OpenFailure Failure = OpenFailure::None;
		std::uint64_t Size = 0;
		Caret Caret;
		bool Preview = false;

		std::size_t WidestColumn = 0;

		std::size_t WidestLine = 0;

		RowMap Rows;

		std::size_t WrappedTo = 0;

		std::size_t RowAim = 0;
		std::size_t RowAimAt = NoRowAim;

		// Set by anything that moves the caret from outside the editor's own frame — a jump
		// requested from elsewhere. The editor only scrolls to the caret when it notices the
		// caret changed during its own processing this frame; a change made earlier in the same
		// frame, before the editor's turn, looks like no change at all from inside that
		// processing. This flag says "scroll anyway", and the editor clears it once it has.
		bool PendingScroll = false;

		// Set by every open that lands on this item, and cleared by whatever draws it once it
		// has brought its window forward. Activating an item moves nothing on screen by itself:
		// the set records which item is active, and the tab bar goes on showing whichever tab it
		// last drew. Without this, opening a file that already had a tab — a definition jumped
		// to a second time, a reference clicked in a file still open from the first — moved the
		// caret in a tab nobody could see.
		bool PendingFocus = false;

		std::vector<std::size_t> IndentDepths;

		// How many columns a tab character occupies in this document. A layout input like the
		// wrap width beside it, not a preference: Core does not know where the number comes
		// from, only that everything measuring this document must use the same one.
		std::size_t TabWidth = DefaultTabWidth;

		FindState Find;

		std::string Detail;
	};

	enum class ItemLoad
	{
		Loaded,

		NeedsConfirmation,

		Failed
	};

	ItemLoad LoadFromFile(OpenItem& item, bool confirmed);

	enum class OpenOutcome
	{
		Opened,
		Activated,
		NeedsConfirmation,
		Failed
	};

	std::string FailureMessage(const OpenItem& item);

	class DocumentSet
	{
	public:
		OpenOutcome Open(const std::filesystem::path& path, bool permanent);

		OpenOutcome OpenConfirmed(const std::filesystem::path& path, bool permanent);

		void Close(std::size_t index);
		void CloseAll();

		std::optional<std::size_t> Find(const std::filesystem::path& path) const;
		std::size_t GetCount() const { return m_Items.size(); }

		OpenItem& At(std::size_t index) { return m_Items[index]; }
		const OpenItem& At(std::size_t index) const { return m_Items[index]; }

		void Activate(std::size_t index);
		std::optional<std::size_t> GetActive() const { return m_Active; }

		void Promote(std::size_t index);

		std::optional<std::size_t> GetPreviewIndex() const;

		std::string DisplayTitle(std::size_t index) const;

		void RewritePaths(const std::filesystem::path& from, const std::filesystem::path& to);

		void MarkMissing(const std::filesystem::path& path);

		void MarkPresent(const std::filesystem::path& path);

	private:
		OpenOutcome Add(const std::filesystem::path& path, bool permanent, bool confirmed);

		bool HasOtherItemNamed(std::size_t index, const std::string& name) const;

		std::vector<OpenItem> m_Items;
		std::optional<std::size_t> m_Active;
	};
}