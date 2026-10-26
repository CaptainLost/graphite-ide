#include "corepch.h"

#include "Core/Text/DocumentSet.h"

#include "Core/Base/ByteSize.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Image/ImageFormat.h"
#include "Core/Text/Editing.h"
#include "Core/Text/FileProbe.h"

#include <algorithm>
#include <utility>

namespace Core {

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
		const Result<FileProbe> probeResult = ProbeFile(item.Path);

		if (!probeResult)
		{
			const Error& probeError = probeResult.GetError();
			const bool fileIsGone = probeError.Code == ErrorCode::NotFound;
			item.Failure = fileIsGone ? OpenFailure::Missing : OpenFailure::ReadFailed;
			item.FailureDetail = probeError.Message;

			return ItemLoad::Failed;
		}

		const FileProbe& probe = probeResult.GetValue();

		// The probe's encoding is not kept: the document sniffs its own, and two fields both meaning
		// the file's encoding are two answers waiting to differ.
		item.FileSize = probe.Size;

		if (probe.Kind == FileKind::LargeText && largeFileConfirmation == LargeFileConfirmation::Unconfirmed)
		{
			// Failure and FailureDetail are cleared only past this return, so a question answered
			// with no leaves the tab saying what it said before.
			return ItemLoad::NeedsConfirmation;
		}

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
				return ItemLoad::Loaded;
			}

			item.Failure = OpenFailure::Binary;
			break;
		case FileKind::TooLarge:
			item.Failure = OpenFailure::TooLarge;
			break;
		case FileKind::Text:
		case FileKind::LargeText:
		{
			Result<std::shared_ptr<TextDocument>> document = store.Acquire(item.Path);

			if (document)
			{
				item.Document = std::move(document).GetValue();
				RecomputeLineCaches(item);
			}
			else
			{
				item.Failure = OpenFailure::ReadFailed;
				item.FailureDetail = document.GetError().Message;
			}
		}
		break;
		}

		return item.Document ? ItemLoad::Loaded : ItemLoad::Failed;
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
		const ItemLoad loadOutcome = LoadFromFile(item, m_Store, largeFileConfirmation);

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

		return loadOutcome == ItemLoad::Loaded ? OpenOutcome::Opened : OpenOutcome::Failed;
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
