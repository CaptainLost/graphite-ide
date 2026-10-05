#include "corepch.h"

#include "Core/Text/DocumentSet.h"

#include "Core/Base/ByteSize.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/Editing.h"
#include "Core/Text/FileProbe.h"

#include <algorithm>
#include <utility>

namespace Core {

	std::string FailureMessage(const OpenItem& item)
	{
		if (!item.Detail.empty())
		{
			return item.Detail;
		}

		switch (item.Failure)
		{
		case OpenFailure::Binary:
			return "Binary file — " + FormatWholeKilobytes(item.Size);
		case OpenFailure::TooLarge:
			return "This file is " + FormatWholeMegabytes(item.Size) + ", too large to open";
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
		const std::optional<std::size_t> index = documents.Find(path);

		if (!index)
		{
			return nullptr;
		}

		const OpenItem& item = documents.At(*index);

		if (!item.Document)
		{
			return nullptr;
		}

		return &item.Document->GetBuffer();
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
			m_Active = index;
		}
	}

	const std::filesystem::path* DocumentSet::GetActiveItemPath() const
	{
		if (!m_Active)
		{
			return nullptr;
		}

		return &m_Items[*m_Active].Path;
	}

	OpenOutcome DocumentSet::Open(const std::filesystem::path& path, bool permanent)
	{
		return Add(path, permanent, false);
	}

	OpenOutcome DocumentSet::OpenConfirmed(const std::filesystem::path& path, bool permanent)
	{
		return Add(path, permanent, true);
	}

	ItemLoad LoadFromFile(OpenItem& item, bool confirmed)
	{
		const auto probe = ProbeFile(item.Path);

		if (!probe)
		{
			// NotFound is the only code that means "gone": a directory, a locked file and an
			// unmeasurable one are all still there, and ProbeFile's message says which.
			item.Failure = probe.GetError().Code == Core::ErrorCode::NotFound
							   ? OpenFailure::Missing
							   : OpenFailure::ReadFailed;
			item.Detail = probe.GetError().Message;

			return ItemLoad::Failed;
		}

		// The probe's encoding is deliberately not kept: the document sniffs its own, and two
		// fields both meaning "the file's encoding" are two answers waiting to differ.
		item.Size = probe.GetValue().Size;

		if (probe.GetValue().Kind == FileKind::LargeText && !confirmed)
		{
			// Nothing else touched, so an item that already carries a reason keeps it and a
			// question answered with "no" leaves the tab saying what it said before.
			return ItemLoad::NeedsConfirmation;
		}

		item.Failure = OpenFailure::None;
		item.Detail.clear();

		switch (probe.GetValue().Kind)
		{
		case FileKind::Binary:
			item.Failure = OpenFailure::Binary;
			break;
		case FileKind::TooLarge:
			item.Failure = OpenFailure::TooLarge;
			break;
		case FileKind::Text:
		case FileKind::LargeText:
		{
			auto document = TextDocument::Load(item.Path);

			if (document)
			{
				item.Document = std::move(document).GetValue();
				RefreshDerived(item);
			}
			else
			{
				item.Failure = OpenFailure::ReadFailed;
				item.Detail = document.GetError().Message;
			}
		}
		break;
		}

		return item.Document.has_value() ? ItemLoad::Loaded : ItemLoad::Failed;
	}

	OpenOutcome DocumentSet::Add(const std::filesystem::path& path, bool permanent, bool confirmed)
	{
		if (const auto existing = Find(path))
		{
			Activate(*existing);
			m_Items[*existing].PendingFocus = true;

			if (permanent)
			{
				m_Items[*existing].Preview = false;
			}

			return OpenOutcome::Activated;
		}

		// At most one preview at a time, so a single click replaces the last one rather than
		// stacking tabs up.
		const auto evictPreview = [this, permanent] {
			if (!permanent)
			{
				if (const auto preview = GetPreviewIndex())
				{
					Close(*preview);
				}
			}
		};

		OpenItem item;
		item.Path = path;
		item.Preview = !permanent;
		item.PendingFocus = true;

		// Asked before anything is added and before evictPreview runs, so answering "no"
		// leaves no trace of the question — not even the loss of the existing preview slot.
		const ItemLoad load = LoadFromFile(item, confirmed);

		if (load == ItemLoad::NeedsConfirmation)
		{
			return OpenOutcome::NeedsConfirmation;
		}

		evictPreview();
		m_Items.push_back(std::move(item));
		Activate(m_Items.size() - 1);

		return load == ItemLoad::Loaded ? OpenOutcome::Opened : OpenOutcome::Failed;
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
			m_Active.reset();
			return;
		}

		if (!m_Active.has_value())
		{
			return;
		}

		if (*m_Active > index)
		{
			m_Active = *m_Active - 1;
		}
		else if (*m_Active == index)
		{
			m_Active = std::min(index, m_Items.size() - 1);
		}
	}

	void DocumentSet::CloseAll()
	{
		m_Items.clear();
		m_Active.reset();
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

	bool DocumentSet::HasOtherItemNamed(std::size_t index, const std::string& name) const
	{
		for (std::size_t other = 0; other < m_Items.size(); ++other)
		{
			if (other != index && ToUtf8(m_Items[other].Path.filename()) == name)
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

		const std::filesystem::path& path = m_Items[index].Path;
		const std::string name = ToUtf8(path.filename());

		// A file deleted while it was open keeps its window and its content, so nothing else
		// on screen would say the file is gone.
		const std::string suffix =
			m_Items[index].Failure == OpenFailure::Missing ? " (deleted)" : "";

		if (!HasOtherItemNamed(index, name))
		{
			return name + suffix;
		}

		// Forward slash regardless of platform: this is a label, not a path anyone will open.
		return ToUtf8(path.parent_path().filename()) + "/" + name + suffix;
	}

	void DocumentSet::RewritePaths(const std::filesystem::path& from,
								   const std::filesystem::path& to)
	{
		for (OpenItem& item : m_Items)
		{
			const auto relative = RelativeTo(item.Path, from);

			if (!relative)
			{
				continue;
			}

			// `relative` is "." when the item is the moved directory itself, and `to / "."`
			// keeps a trailing separator-dot that compares unequal to `to`, breaking Find.
			item.Path = (*relative == ".") ? to : to / *relative;

			// The document's own copy of the path is the one a save writes to; left behind it
			// would put the file back where it used to be.
			if (item.Document)
			{
				item.Document->SetPath(item.Path);
			}
		}
	}

	void DocumentSet::MarkMissing(const std::filesystem::path& path)
	{
		const auto index = Find(path);

		if (!index)
		{
			return;
		}

		m_Items[*index].Failure = OpenFailure::Missing;

		// FailureMessage prefers any detail over the failure itself, and the previous one no
		// longer describes what is wrong.
		m_Items[*index].Detail.clear();
	}

	void DocumentSet::MarkPresent(const std::filesystem::path& path)
	{
		const auto index = Find(path);

		if (!index)
		{
			return;
		}

		// Only Missing is cleared: Binary, TooLarge and ReadFailed carry no document, and
		// clearing one would claim a tab holds content that was never loaded.
		if (m_Items[*index].Failure == OpenFailure::Missing)
		{
			m_Items[*index].Failure = OpenFailure::None;
			m_Items[*index].Detail.clear();
		}
	}
}