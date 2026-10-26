#pragma once

#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobSubmitter.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentStore.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextDocument.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
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

		// Set while a worker reads the document; until it lands the item holds none.
		std::optional<std::uint64_t> LoadTicket;

		// Run once a document read on a worker lands, so a caret placed while it was read lands in its text. A later
		// placement replaces an earlier one.
		std::function<void(OpenItem&)> PlaceCaretOnLoad;
	};

	bool IsLoading(const OpenItem& item);

	std::string LoadingMessage(const OpenItem& item);

	// A read that failed may succeed when asked again; a file refused for what it is will not.
	bool CanRetryLoad(const OpenItem& item);

	enum class ItemLoad
	{
		Loaded,
		Loading,
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
		Loading,
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

		// Files above BackgroundLoadThreshold are then read through `submit` while their tab waits. Without it every
		// file is read where it is opened.
		void LoadLargeFilesWith(JobSubmitter submit);

		// WARNING: An item measured at a width other than the editor's is measured again, on the UI thread, when it is
		// first drawn; this is the editor's width, so a load measures once.
		void SetTabWidthForLoads(std::size_t tabWidth) { m_TabWidthForLoads = tabWidth; }

		// For an item that holds no document and is not being read: after a failure, or when its file came back.
		ItemLoad LoadAgain(std::size_t index, LargeFileConfirmation largeFileConfirmation);

		// Gives each item a worker finished reading its document or its failure; what was read for an item closed since
		// is dropped. Answers the indices of the items that got a document.
		std::vector<std::size_t> TakeFinishedLoads();

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
		struct BackgroundLoad
		{
			std::uint64_t Ticket = 0;
			CancellationToken Cancellation;
		};

		struct FinishedLoads;

		OpenOutcome Add(const std::filesystem::path& path, TabKind tabKind, LargeFileConfirmation largeFileConfirmation);

		ItemLoad LoadInto(OpenItem& item, LargeFileConfirmation largeFileConfirmation);

		bool ReadsInBackground(const OpenItem& item) const;

		void StartBackgroundLoad(OpenItem& item);

		void StopBackgroundLoad(const OpenItem& item);

		std::optional<std::size_t> FindLoading(std::uint64_t ticket) const;

		void ClosePreviewIfAny();

		bool HasOtherItemNamed(std::size_t index, const std::string& fileName) const;

		// Declared before the items so it outlives them.
		DocumentStore m_Store;
		std::vector<OpenItem> m_Items;
		std::optional<std::size_t> m_ActiveIndex;

		JobSubmitter m_SubmitLoad;
		std::size_t m_TabWidthForLoads = DefaultTabWidth;
		std::uint64_t m_LastLoadTicket = 0;
		std::vector<BackgroundLoad> m_BackgroundLoads;

		// WARNING: Shared with the workers, so a read that finishes after the set is gone still has somewhere to post.
		std::shared_ptr<FinishedLoads> m_FinishedLoads;
	};

	// The path is matched exactly, as Find matches it, not through PathKey. The pointer is valid
	// while some view holds the document.
	const TextBuffer* BufferOfOpenDocument(const DocumentSet& documents, const std::filesystem::path& path);
}
