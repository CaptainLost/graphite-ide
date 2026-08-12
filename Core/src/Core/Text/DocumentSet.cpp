#include "corepch.h"

#include "Core/Text/DocumentSet.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Text/Editing.h"
#include "Core/Text/FileProbe.h"

#include <algorithm>
#include <utility>

namespace Core {

	namespace {
		std::string Megabytes(std::uint64_t bytes)
		{
			return std::to_string(bytes / (1024 * 1024)) + " MB";
		}

		std::string Kilobytes(std::uint64_t bytes)
		{
			return std::to_string(bytes / 1024) + " KB";
		}
	}

	std::string FailureMessage(const OpenItem& item)
	{
		if (!item.Detail.empty())
			return item.Detail;

		switch (item.Failure)
		{
		case OpenFailure::Binary:
			return "Binary file — " + Kilobytes(item.Size);
		case OpenFailure::TooLarge:
			return "This file is " + Megabytes(item.Size) + ", too large to open";
		case OpenFailure::Missing:
			return "This file no longer exists on disk";
		case OpenFailure::ReadFailed:
			return "This file could not be read";
		case OpenFailure::None:
			break;
		}

		return {};
	}

	std::optional<std::size_t> DocumentSet::Find(const std::filesystem::path& path) const
	{
		for (std::size_t index = 0; index < m_Items.size(); ++index)
		{
			if (m_Items[index].Path == path)
				return index;
		}

		return std::nullopt;
	}

	void DocumentSet::Activate(std::size_t index)
	{
		if (index < m_Items.size())
			m_Active = index;
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
			// NotFound is the only code that actually means "gone" — a directory, a
			// locked file and an unmeasurable one are all still there, so claiming
			// otherwise would send the user looking for a file sitting right in front
			// of them. ProbeFile's own message says which one it was.
			item.Failure = probe.GetError().Code == Core::ErrorCode::NotFound
							   ? OpenFailure::Missing
							   : OpenFailure::ReadFailed;
			item.Detail = probe.GetError().Message;

			return ItemLoad::Failed;
		}

		// The probe's encoding is deliberately not kept: the document sniffs its own, and
		// two fields both meaning "the file's encoding" are two answers waiting to differ.
		// The status bar reads the document's, which is the one the buffer was decoded
		// with; size has no such counterpart, since a failed item has no document to ask.
		item.Size = probe.GetValue().Size;

		if (probe.GetValue().Kind == FileKind::LargeText && !confirmed)
		{
			// Nothing decided and nothing touched beyond the size the asking needs. An
			// item that already carries a reason keeps it, so a question answered with
			// "no" leaves the tab saying exactly what it said before.
			return ItemLoad::NeedsConfirmation;
		}

		// Whatever the item last failed with is now out of date: this is a fresh look at
		// the file, and the switch below either loads it or says why it could not.
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

			if (permanent)
				m_Items[*existing].Preview = false;

			return OpenOutcome::Activated;
		}

		// At most one preview at a time: a single click replaces the last one rather than
		// stacking tabs up. Called right before an item is actually pushed — not any
		// earlier — so a path that adds nothing (NeedsConfirmation, answered by never
		// calling OpenConfirmed) never costs the existing preview its slot.
		const auto evictPreview = [this, permanent] {
			if (!permanent)
			{
				if (const auto preview = GetPreviewIndex())
					Close(*preview);
			}
		};

		OpenItem item;
		item.Path = path;
		item.Preview = !permanent;

		// Asking has to happen before anything is added, so answering "no" leaves no
		// trace of the question — including the preview slot, which stays exactly as it
		// was since evictPreview has not run yet.
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
			return;

		m_Items.erase(m_Items.begin() + static_cast<std::ptrdiff_t>(index));

		if (m_Items.empty())
		{
			m_Active.reset();
			return;
		}

		if (!m_Active.has_value())
			return;

		// An item before the active one shifts it down; closing the active one falls back
		// to whatever now occupies that slot, or the last item when it was the last.
		if (*m_Active > index)
			m_Active = *m_Active - 1;
		else if (*m_Active == index)
			m_Active = std::min(index, m_Items.size() - 1);
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
				return index;
		}

		return std::nullopt;
	}

	void DocumentSet::Promote(std::size_t index)
	{
		if (index < m_Items.size())
			m_Items[index].Preview = false;
	}

	std::string DocumentSet::DisplayTitle(std::size_t index) const
	{
		if (index >= m_Items.size())
			return {};

		const std::filesystem::path& path = m_Items[index].Path;
		const std::string name = ToUtf8(path.filename());

		// A file deleted while it was open keeps its window and its content, so nothing
		// else on screen would say the file is gone — the tab has to. Appended after any
		// disambiguating directory below, so the suffix always reads last.
		const std::string suffix =
			m_Items[index].Failure == OpenFailure::Missing ? " (deleted)" : "";

		for (std::size_t other = 0; other < m_Items.size(); ++other)
		{
			if (other == index)
				continue;

			if (ToUtf8(m_Items[other].Path.filename()) != name)
				continue;

			// Forward slash regardless of platform: this is a label, not a path anyone
			// will open.
			return ToUtf8(path.parent_path().filename()) + "/" + name + suffix;
		}

		return name + suffix;
	}

	void DocumentSet::RewritePaths(const std::filesystem::path& from,
								   const std::filesystem::path& to)
	{
		for (OpenItem& item : m_Items)
		{
			const auto relative = RelativeTo(item.Path, from);

			if (!relative)
				continue;

			// relative is "." when the item path equals `from` exactly — an item can be
			// the workspace root itself (opening a directory produces a ReadFailed item
			// at that path). `to / "."` would keep a trailing separator-dot that compares
			// unequal to `to` itself, breaking Find and duplicate detection, so that case
			// becomes `to` directly instead of going through the append.
			item.Path = (*relative == ".") ? to : to / *relative;

			// The document carries its own copy of the path, and that is the one a save
			// writes to. Left behind, it would put the file back where it used to be
			// without anything on screen suggesting it had.
			if (item.Document)
				item.Document->SetPath(item.Path);
		}
	}

	void DocumentSet::MarkMissing(const std::filesystem::path& path)
	{
		const auto index = Find(path);

		if (!index)
			return;

		m_Items[*index].Failure = OpenFailure::Missing;

		// Whatever detail the previous failure carried — a sharing violation, an
		// unreadable directory — no longer describes what is wrong. Left in place,
		// FailureMessage would keep preferring it over "This file no longer exists".
		m_Items[*index].Detail.clear();
	}

	void DocumentSet::MarkPresent(const std::filesystem::path& path)
	{
		const auto index = Find(path);

		if (!index)
			return;

		// Only clear the Missing failure. Binary, TooLarge, and ReadFailed carry no
		// document at all — clearing one would claim a tab holds content that was never
		// loaded, and everything downstream would then dereference an empty optional.
		if (m_Items[*index].Failure == OpenFailure::Missing)
		{
			m_Items[*index].Failure = OpenFailure::None;
			m_Items[*index].Detail.clear();
		}
	}
}