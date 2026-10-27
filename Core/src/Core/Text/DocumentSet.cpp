#include "corepch.h"

#include "Core/Text/DocumentSet.h"

#include "Core/Base/ByteSize.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Image/ImageFormat.h"
#include "Core/Jobs/Mailbox.h"
#include "Core/Text/Editing.h"
#include "Core/Text/FileProbe.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <utility>

namespace Core {

	namespace {

		enum class ProbeVerdict
		{
			ReadText,
			ShowImage,
			NeedsConfirmation,
			Refused
		};

		ProbeVerdict ProbeForLoad(OpenItem& item, LargeFileConfirmation largeFileConfirmation)
		{
			const Result<FileProbe> probeResult = ProbeFile(item.Path);

			if (!probeResult)
			{
				const Error& probeError = probeResult.GetError();
				const bool fileIsGone = probeError.Code == ErrorCode::NotFound;
				item.Failure = fileIsGone ? OpenFailure::Missing : OpenFailure::ReadFailed;
				item.FailureDetail = probeError.Message;

				return ProbeVerdict::Refused;
			}

			const FileProbe& probe = probeResult.GetValue();

			if (probe.Kind == FileKind::LargeText && largeFileConfirmation == LargeFileConfirmation::Unconfirmed)
			{
				// WARNING: Nothing is recorded before this return, so a recorded size above the question was accepted.
				return ProbeVerdict::NeedsConfirmation;
			}

			item.FileSize = probe.Size;
			item.Failure = OpenFailure::None;
			item.FailureDetail.clear();
			item.View = ItemView::Text;

			switch (probe.Kind)
			{
			case FileKind::Binary:
				if (ImageFormatOfPath(item.Path))
				{
					// A text file named .png, such as a Git LFS pointer, never gets here: only a NUL makes it Binary.
					item.View = ItemView::Image;
					item.Document.reset();
					return ProbeVerdict::ShowImage;
				}

				item.Failure = OpenFailure::Binary;
				return ProbeVerdict::Refused;
			case FileKind::TooLarge:
				item.Failure = OpenFailure::TooLarge;
				return ProbeVerdict::Refused;
			case FileKind::Text:
			case FileKind::LargeText:
				break;
			}

			return ProbeVerdict::ReadText;
		}

		void PlacePendingCaret(OpenItem& item)
		{
			if (!item.CaretOnLoad)
			{
				return;
			}

			const CaretWhenLoaded pending = std::move(*item.CaretOnLoad);
			item.CaretOnLoad.reset();
			pending.Place(item);
		}

		void RecordReadFailure(OpenItem& item, const Error& readError)
		{
			const bool fileIsGone = readError.Code == ErrorCode::NotFound;
			item.Failure = fileIsGone ? OpenFailure::Missing : OpenFailure::ReadFailed;
			item.FailureDetail = readError.Message;
		}

		ItemLoad ReadNow(OpenItem& item, DocumentStore& store)
		{
			Result<std::shared_ptr<TextDocument>> document = store.Acquire(item.Path);

			if (!document)
			{
				item.Failure = OpenFailure::ReadFailed;
				item.FailureDetail = document.GetError().Message;

				return ItemLoad::Failed;
			}

			item.Document = std::move(document).GetValue();
			RecomputeLineCaches(item);
			PlacePendingCaret(item);

			return ItemLoad::Loaded;
		}

		struct LoadedDocument
		{
			std::shared_ptr<TextDocument> Document;
			LineCaches Caches;
		};

		struct FinishedLoad
		{
			std::uint64_t Ticket = 0;
			std::filesystem::path ReadPath;
			Result<LoadedDocument> Loaded;
		};

		struct BackgroundRead
		{
			std::filesystem::path Path;
			std::size_t TabWidth = DefaultTabWidth;
			std::uint64_t Ticket = 0;
			CancellationToken Cancellation;
		};

		Error ReadGivenUp(const std::filesystem::path& path)
		{
			return Error{.Code = ErrorCode::Cancelled, .Message = "Reading was given up: " + ToUtf8(path)};
		}
	}

	struct DocumentReadsShared
	{
		Mailbox<FinishedLoad> Finished;

		std::mutex WrapMutex;
		WrapColumnsForLineCount WrapColumns;
		std::map<std::uint64_t, WrapColumnsForLineCount> WrapColumnsOfRead;

		std::size_t WrapColumnsFor(std::uint64_t ticket, std::size_t lineCount)
		{
			WrapColumnsForLineCount wrapColumns;

			{
				const std::lock_guard<std::mutex> lock(WrapMutex);
				const std::map<std::uint64_t, WrapColumnsForLineCount>::const_iterator ownWrap = WrapColumnsOfRead.find(ticket);
				wrapColumns = ownWrap != WrapColumnsOfRead.end() ? ownWrap->second : WrapColumns;
			}

			return wrapColumns ? wrapColumns(lineCount) : RowMap::kNoWrap;
		}

		void ForgetWrapOfRead(std::uint64_t ticket)
		{
			const std::lock_guard<std::mutex> lock(WrapMutex);
			WrapColumnsOfRead.erase(ticket);
		}
	};

	namespace {

		// WARNING: Everything opening does that needs only the file belongs here, so the UI thread only hands it to the item.
		Result<LoadedDocument> ReadWithLineCaches(const BackgroundRead& read, DocumentReadsShared& shared)
		{
			Result<TextDocument> loaded = TextDocument::Load(read.Path, read.Cancellation);

			if (!loaded)
			{
				return loaded.GetError();
			}

			std::shared_ptr<TextDocument> document = std::make_shared<TextDocument>(std::move(loaded).GetValue());
			const TextBuffer& buffer = document->GetBuffer();
			const std::size_t columnsPerRow = shared.WrapColumnsFor(read.Ticket, buffer.GetLineCount());

			std::optional<LineCaches> caches = ComputeLineCachesUnlessCancelled(buffer, read.TabWidth, columnsPerRow, read.Cancellation);

			if (!caches)
			{
				return ReadGivenUp(read.Path);
			}

			return LoadedDocument{.Document = std::move(document), .Caches = std::move(*caches)};
		}

		// WARNING: The second check is not redundant: what is read for a tab closed meanwhile is let go of here, on the worker.
		void ReadInBackground(const BackgroundRead& read, DocumentReadsShared& shared)
		{
			if (read.Cancellation.IsCancelled())
			{
				return;
			}

			Result<LoadedDocument> loaded = ReadWithLineCaches(read, shared);

			if (read.Cancellation.IsCancelled())
			{
				return;
			}

			shared.Finished.Post(FinishedLoad{.Ticket = read.Ticket, .ReadPath = read.Path, .Loaded = std::move(loaded)});
		}

		void LandDocument(OpenItem& item, LoadedDocument loaded, DocumentStore& store)
		{
			if (std::shared_ptr<TextDocument> registered = store.Find(item.Path))
			{
				item.Document = std::move(registered);
				RecomputeLineCaches(item);
			}
			else
			{
				// WARNING: The tab may have moved with a renamed folder while its file was read.
				loaded.Document->SetPath(item.Path);
				store.Adopt(item.Path, loaded.Document);
				item.Document = std::move(loaded.Document);
				AdoptLineCaches(item, std::move(loaded.Caches));
			}

			// WARNING: Without this the editor rebuilds every row again on its first draw.
			item.ColumnsPerWrappedRow = item.Rows.GetColumnsPerRow();

			const bool fileWentMissingWhileRead = item.Failure == OpenFailure::Missing;

			if (!fileWentMissingWhileRead)
			{
				item.Failure = OpenFailure::None;
				item.FailureDetail.clear();
			}

			PlacePendingCaret(item);
		}
	}

	DocumentSet::DocumentSet() : m_Shared(std::make_shared<DocumentReadsShared>())
	{
	}

	DocumentSet::~DocumentSet()
	{
		CancelAllReads();
	}

	bool IsLoading(const OpenItem& item)
	{
		return item.LoadTicket.has_value();
	}

	std::string LoadingMessage(const OpenItem& item)
	{
		return "Opening " + ToUtf8(item.Path.filename()) + " (" + FormatWholeMegabytes(item.FileSize) + ")";
	}

	bool CanRetryLoad(const OpenItem& item)
	{
		return !item.Document && !IsLoading(item) && item.Failure == OpenFailure::ReadFailed;
	}

	std::string FailureMessage(const OpenItem& item)
	{
		if (!item.FailureDetail.empty())
		{
			return item.FailureDetail;
		}

		switch (item.Failure)
		{
		case OpenFailure::Binary:
			return "Binary file — " + FormatWholeKilobytes(item.FileSize);
		case OpenFailure::TooLarge:
			return "This file is " + FormatWholeMegabytes(item.FileSize) + ", too large to open";
		case OpenFailure::Missing:
			return "This file no longer exists on disk";
		case OpenFailure::ReadFailed:
			return "This file could not be read";
		case OpenFailure::None:
			break;
		}

		return {};
	}

	const TextBuffer* BufferOfOpenDocument(const DocumentSet& documents, const std::filesystem::path& path)
	{
		const std::shared_ptr<TextDocument> document = documents.GetStore().Find(path);

		return document ? &document->GetBuffer() : nullptr;
	}

	std::optional<std::size_t> DocumentSet::Find(const std::filesystem::path& path) const
	{
		for (std::size_t index = 0; index < m_Items.size(); ++index)
		{
			if (m_Items[index].Path == path)
			{
				return index;
			}
		}

		return std::nullopt;
	}

	void DocumentSet::Activate(std::size_t index)
	{
		if (index < m_Items.size())
		{
			m_ActiveIndex = index;
		}
	}

	const std::filesystem::path* DocumentSet::GetActiveItemPath() const
	{
		if (!m_ActiveIndex)
		{
			return nullptr;
		}

		return &m_Items[*m_ActiveIndex].Path;
	}

	OpenOutcome DocumentSet::Open(const std::filesystem::path& path, TabKind tabKind)
	{
		return Add(path, tabKind, LargeFileConfirmation::Unconfirmed);
	}

	OpenOutcome DocumentSet::OpenConfirmed(const std::filesystem::path& path, TabKind tabKind)
	{
		return Add(path, tabKind, LargeFileConfirmation::Confirmed);
	}

	ItemLoad LoadFromFile(OpenItem& item, DocumentStore& store, LargeFileConfirmation largeFileConfirmation)
	{
		switch (ProbeForLoad(item, largeFileConfirmation))
		{
		case ProbeVerdict::NeedsConfirmation:
			return ItemLoad::NeedsConfirmation;
		case ProbeVerdict::Refused:
			return ItemLoad::Failed;
		case ProbeVerdict::ShowImage:
			return ItemLoad::Loaded;
		case ProbeVerdict::ReadText:
			break;
		}

		return ReadNow(item, store);
	}

	void DocumentSet::LoadLargeFilesWith(JobSubmitter submit)
	{
		m_SubmitLoad = std::move(submit);
	}

	void DocumentSet::SetWrapForLoads(WrapColumnsForLineCount wrapColumns)
	{
		const std::lock_guard<std::mutex> lock(m_Shared->WrapMutex);
		m_Shared->WrapColumns = std::move(wrapColumns);
	}

	void DocumentSet::SetWrapForLoad(std::size_t index, WrapColumnsForLineCount wrapColumns)
	{
		if (index >= m_Items.size() || !m_Items[index].LoadTicket)
		{
			return;
		}

		const std::lock_guard<std::mutex> lock(m_Shared->WrapMutex);
		m_Shared->WrapColumnsOfRead[*m_Items[index].LoadTicket] = std::move(wrapColumns);
	}

	void DocumentSet::CancelAllReads()
	{
		for (const BackgroundLoad& load : m_BackgroundLoads)
		{
			load.Cancellation.Cancel();
			m_Shared->ForgetWrapOfRead(load.Ticket);
		}

		m_BackgroundLoads.clear();
	}

	ItemLoad DocumentSet::LoadInto(OpenItem& item, LargeFileConfirmation largeFileConfirmation)
	{
		item.TabWidth = m_TabWidthForLoads;

		switch (ProbeForLoad(item, largeFileConfirmation))
		{
		case ProbeVerdict::NeedsConfirmation:
			return ItemLoad::NeedsConfirmation;
		case ProbeVerdict::Refused:
			return ItemLoad::Failed;
		case ProbeVerdict::ShowImage:
			return ItemLoad::Loaded;
		case ProbeVerdict::ReadText:
			break;
		}

		if (ReadsInBackground(item))
		{
			StartBackgroundLoad(item);
			return ItemLoad::Loading;
		}

		return ReadNow(item, m_Store);
	}

	bool DocumentSet::ReadsInBackground(const OpenItem& item) const
	{
		const bool canSubmit = static_cast<bool>(m_SubmitLoad);
		const bool alreadyRead = m_Store.Find(item.Path) != nullptr;

		return canSubmit && LoadsInBackground(item.FileSize) && !alreadyRead;
	}

	void DocumentSet::StartBackgroundLoad(OpenItem& item)
	{
		const std::uint64_t ticket = ++m_LastLoadTicket;
		const BackgroundLoad load{.Ticket = ticket};

		m_BackgroundLoads.push_back(load);
		item.LoadTicket = ticket;

		const BackgroundRead read{.Path = item.Path, .TabWidth = item.TabWidth, .Ticket = ticket, .Cancellation = load.Cancellation};
		const std::shared_ptr<DocumentReadsShared> shared = m_Shared;

		m_SubmitLoad([read, shared] { ReadInBackground(read, *shared); });
	}

	void DocumentSet::StopBackgroundLoad(const OpenItem& item)
	{
		if (!item.LoadTicket)
		{
			return;
		}

		for (std::size_t loadIndex = 0; loadIndex < m_BackgroundLoads.size(); ++loadIndex)
		{
			if (m_BackgroundLoads[loadIndex].Ticket == *item.LoadTicket)
			{
				m_BackgroundLoads[loadIndex].Cancellation.Cancel();
				m_Shared->ForgetWrapOfRead(*item.LoadTicket);
				m_BackgroundLoads.erase(m_BackgroundLoads.begin() + static_cast<std::ptrdiff_t>(loadIndex));
				return;
			}
		}
	}

	std::optional<std::size_t> DocumentSet::FindLoading(std::uint64_t ticket) const
	{
		for (std::size_t index = 0; index < m_Items.size(); ++index)
		{
			if (m_Items[index].LoadTicket == ticket)
			{
				return index;
			}
		}

		return std::nullopt;
	}

	ItemLoad DocumentSet::LoadAgain(std::size_t index, LargeFileConfirmation largeFileConfirmation)
	{
		if (index >= m_Items.size())
		{
			return ItemLoad::Failed;
		}

		OpenItem& item = m_Items[index];

		if (item.Document)
		{
			return ItemLoad::Loaded;
		}

		if (IsLoading(item))
		{
			return ItemLoad::Loading;
		}

		return LoadInto(item, largeFileConfirmation);
	}

	ItemLoad DocumentSet::RetryLoad(std::size_t index)
	{
		if (index >= m_Items.size())
		{
			return ItemLoad::Failed;
		}

		const bool sizeAboveTheQuestionWasAccepted = m_Items[index].FileSize > LargeFileThreshold;
		const LargeFileConfirmation confirmation =
			sizeAboveTheQuestionWasAccepted ? LargeFileConfirmation::Confirmed : LargeFileConfirmation::Unconfirmed;

		return LoadAgain(index, confirmation);
	}

	std::vector<std::size_t> DocumentSet::TakeFinishedLoads()
	{
		std::vector<std::size_t> landedIndices;

		for (FinishedLoad& finished : m_Shared->Finished.TakeAll())
		{
			const std::optional<std::size_t> index = FindLoading(finished.Ticket);

			if (!index)
			{
				continue;
			}

			OpenItem& item = m_Items[*index];
			StopBackgroundLoad(item);
			item.LoadTicket.reset();

			if (finished.Loaded)
			{
				LandDocument(item, std::move(finished.Loaded).GetValue(), m_Store);
				landedIndices.push_back(*index);
				continue;
			}

			// WARNING: A folder renamed before the worker opened the file fails the read at the old path; the tab has the new one.
			const bool tabMovedWhileRead = finished.ReadPath != item.Path;

			if (!tabMovedWhileRead)
			{
				RecordReadFailure(item, finished.Loaded.GetError());
				continue;
			}

			if (LoadInto(item, LargeFileConfirmation::Confirmed) == ItemLoad::Loaded)
			{
				landedIndices.push_back(*index);
			}
		}

		return landedIndices;
	}

	OpenOutcome DocumentSet::Add(const std::filesystem::path& path, TabKind tabKind, LargeFileConfirmation largeFileConfirmation)
	{
		const std::optional<std::size_t> existingIndex = Find(path);

		if (existingIndex.has_value())
		{
			Activate(*existingIndex);
			OpenItem& existingItem = m_Items[*existingIndex];
			existingItem.PendingFocus = true;

			if (tabKind == TabKind::Permanent)
			{
				existingItem.Preview = false;
			}

			return OpenOutcome::Activated;
		}

		const bool opensAsPreview = tabKind == TabKind::Preview;

		OpenItem item;
		item.Path = path;
		item.Preview = opensAsPreview;
		item.PendingFocus = true;

		// Asked before anything is added or the preview closed, so a question answered with no
		// leaves no trace, not even the loss of the existing preview.
		const ItemLoad loadOutcome = LoadInto(item, largeFileConfirmation);

		if (loadOutcome == ItemLoad::NeedsConfirmation)
		{
			return OpenOutcome::NeedsConfirmation;
		}

		if (opensAsPreview)
		{
			ClosePreviewIfAny();
		}

		m_Items.push_back(std::move(item));
		const std::size_t addedIndex = m_Items.size() - 1;
		Activate(addedIndex);

		switch (loadOutcome)
		{
		case ItemLoad::Loaded:
			return OpenOutcome::Opened;
		case ItemLoad::Loading:
			return OpenOutcome::Loading;
		case ItemLoad::NeedsConfirmation:
		case ItemLoad::Failed:
			break;
		}

		return OpenOutcome::Failed;
	}

	void DocumentSet::ClosePreviewIfAny()
	{
		const std::optional<std::size_t> previewIndex = GetPreviewIndex();

		if (previewIndex.has_value())
		{
			Close(*previewIndex);
		}
	}

	void DocumentSet::Close(std::size_t index)
	{
		if (index >= m_Items.size())
		{
			return;
		}

		StopBackgroundLoad(m_Items[index]);
		m_Items.erase(m_Items.begin() + static_cast<std::ptrdiff_t>(index));

		if (m_Items.empty())
		{
			m_ActiveIndex.reset();
			return;
		}

		if (!m_ActiveIndex.has_value())
		{
			return;
		}

		if (*m_ActiveIndex > index)
		{
			m_ActiveIndex = *m_ActiveIndex - 1;
		}
		else if (*m_ActiveIndex == index)
		{
			const std::size_t lastIndex = m_Items.size() - 1;
			m_ActiveIndex = std::min(index, lastIndex);
		}
	}

	void DocumentSet::CloseAll()
	{
		CancelAllReads();
		m_Items.clear();
		m_ActiveIndex.reset();
	}

	std::optional<std::size_t> DocumentSet::GetPreviewIndex() const
	{
		for (std::size_t index = 0; index < m_Items.size(); ++index)
		{
			if (m_Items[index].Preview)
			{
				return index;
			}
		}

		return std::nullopt;
	}

	void DocumentSet::Promote(std::size_t index)
	{
		if (index < m_Items.size())
		{
			m_Items[index].Preview = false;
		}
	}

	bool DocumentSet::HasOtherItemNamed(std::size_t index, const std::string& fileName) const
	{
		for (std::size_t otherIndex = 0; otherIndex < m_Items.size(); ++otherIndex)
		{
			if (otherIndex != index && ToUtf8(m_Items[otherIndex].Path.filename()) == fileName)
			{
				return true;
			}
		}

		return false;
	}

	std::string DocumentSet::DisplayTitle(std::size_t index) const
	{
		if (index >= m_Items.size())
		{
			return {};
		}

		const OpenItem& item = m_Items[index];
		const std::string fileName = ToUtf8(item.Path.filename());

		const bool fileIsDeleted = item.Failure == OpenFailure::Missing;
		const std::string deletedMark = fileIsDeleted ? " (deleted)" : "";

		if (!HasOtherItemNamed(index, fileName))
		{
			return fileName + deletedMark;
		}

		// A label, not a path anyone will open, so the separator is a forward slash on every platform.
		return ToUtf8(item.Path.parent_path().filename()) + "/" + fileName + deletedMark;
	}

	void DocumentSet::RewritePaths(const std::filesystem::path& movedFrom, const std::filesystem::path& movedTo)
	{
		m_Store.RewritePaths(movedFrom, movedTo);

		for (OpenItem& item : m_Items)
		{
			const std::optional<std::filesystem::path> relativePath = RelativeTo(item.Path, movedFrom);

			if (!relativePath)
			{
				continue;
			}

			// `movedTo / "."` keeps a trailing dot that compares unequal to `movedTo`, and Find would
			// then miss the item.
			const bool itemIsTheMovedPath = *relativePath == ".";
			item.Path = itemIsTheMovedPath ? movedTo : movedTo / *relativePath;
		}
	}

	void DocumentSet::MarkMissing(const std::filesystem::path& path)
	{
		const std::optional<std::size_t> index = Find(path);

		if (!index)
		{
			return;
		}

		OpenItem& item = m_Items[*index];
		item.Failure = OpenFailure::Missing;
		item.FailureDetail.clear();
	}

	void DocumentSet::MarkPresent(const std::filesystem::path& path)
	{
		const std::optional<std::size_t> index = Find(path);

		if (!index)
		{
			return;
		}

		OpenItem& item = m_Items[*index];

		if (item.Failure == OpenFailure::Missing)
		{
			item.Failure = OpenFailure::None;
			item.FailureDetail.clear();
		}
	}
}
