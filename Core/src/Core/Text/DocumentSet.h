#pragma once

#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentStore.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextDocument.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
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

	// What a tab draws for its item. An Image item has no document: its picture is the file itself.
	enum class ItemView
	{
		Text,
		Image
	};

	inline constexpr std::size_t NoRowAim = static_cast<std::size_t>(-1);

	struct FindMatch
	{
		std::size_t From = 0;
		std::size_t To = 0;
	};

	// Kept on the item rather than in a map beside the editor, so closing or renaming a document
	// takes it along. Plain fields on purpose: the matcher and its query type belong above Core,
	// because a query carrying a file mask is a search concept, not a text-editing one.
	struct FindState
	{
		bool Open = false;

		std::string Query;
		bool MatchCase = false;
		bool WholeWord = false;

		// Document byte ranges in ascending order.
		std::vector<FindMatch> Matches;
		std::size_t CurrentMatch = 0;

		// A buffer starts at revision zero, so an empty Query, not a zero MatchesRevision, means
		// never searched.
		std::size_t MatchesRevision = 0;
	};

	struct OpenItem
	{
		std::filesystem::path Path;
		// Shared with every other view of the same file, through the set's store.
		std::shared_ptr<TextDocument> Document;
		OpenFailure Failure = OpenFailure::None;
		ItemView View = ItemView::Text;
		std::uint64_t FileSize = 0;
		Caret Caret;
		bool Preview = false;

		std::size_t WidestColumn = 0;

		std::size_t WidestLine = 0;

		RowMap Rows;

		// The last change of its document that Rows, IndentDepths and the widest line reflect.
		std::uint64_t SeenChange = 0;

		std::size_t ColumnsPerWrappedRow = 0;

		std::size_t RowAimColumn = 0;
		std::size_t RowAimCaretOffset = NoRowAim;

		// Set by a double or triple click and read by the drag that follows it; a plain press
		// puts it back to Character.
		SelectionUnit DragUnit = SelectionUnit::Character;
		OffsetRange DragOrigin;

		// The editor scrolls to the caret only when it saw the caret move during its own turn, so
		// a jump made earlier in the frame from elsewhere sets this to say scroll anyway. The
		// editor clears it once it has.
		bool PendingScroll = false;

		// Activating an item moves nothing on screen: the tab bar goes on showing the tab it last
		// drew. Every open that lands on the item sets this, and whatever draws the item clears it
		// once its window is in front.
		bool PendingFocus = false;

		std::vector<std::size_t> IndentDepths;

		// A layout input like the wrap width, not a preference: everything that measures this
		// document has to use this one value.
		std::size_t TabWidth = DefaultTabWidth;

		FindState Find;

		std::string FailureDetail;
	};

	enum class ItemLoad
	{
		Loaded,
		NeedsConfirmation,
		Failed
	};

	enum class LargeFileConfirmation
	{
		Unconfirmed,
		Confirmed
	};

	ItemLoad LoadFromFile(OpenItem& item, DocumentStore& store, LargeFileConfirmation largeFileConfirmation);

	enum class OpenOutcome
	{
		Opened,
		Activated,
		NeedsConfirmation,
		Failed
	};

	std::string FailureMessage(const OpenItem& item);

	enum class TabKind
	{
		Preview,
		Permanent
	};

	class DocumentSet
	{
	public:
		DocumentStore& GetStore() { return m_Store; }
		const DocumentStore& GetStore() const { return m_Store; }

		OpenOutcome Open(const std::filesystem::path& path, TabKind tabKind);

		OpenOutcome OpenConfirmed(const std::filesystem::path& path, TabKind tabKind);

		void Close(std::size_t index);
		void CloseAll();

		std::optional<std::size_t> Find(const std::filesystem::path& path) const;
		std::size_t GetCount() const { return m_Items.size(); }

		OpenItem& At(std::size_t index) { return m_Items[index]; }
		const OpenItem& At(std::size_t index) const { return m_Items[index]; }

		void Activate(std::size_t index);
		std::optional<std::size_t> GetActiveIndex() const { return m_ActiveIndex; }

		// Valid until the set next changes.
		const std::filesystem::path* GetActiveItemPath() const;

		void Promote(std::size_t index);

		std::optional<std::size_t> GetPreviewIndex() const;

		std::string DisplayTitle(std::size_t index) const;

		void RewritePaths(const std::filesystem::path& movedFrom, const std::filesystem::path& movedTo);

		void MarkMissing(const std::filesystem::path& path);

		void MarkPresent(const std::filesystem::path& path);

	private:
		OpenOutcome Add(const std::filesystem::path& path, TabKind tabKind, LargeFileConfirmation largeFileConfirmation);

		void ClosePreviewIfAny();

		bool HasOtherItemNamed(std::size_t index, const std::string& fileName) const;

		// Declared before the items so it outlives them.
		DocumentStore m_Store;
		std::vector<OpenItem> m_Items;
		std::optional<std::size_t> m_ActiveIndex;
	};

	// The path is matched exactly, as Find matches it, not through PathKey. The pointer is valid
	// while some view holds the document.
	const TextBuffer* BufferOfOpenDocument(const DocumentSet& documents, const std::filesystem::path& path);
}
