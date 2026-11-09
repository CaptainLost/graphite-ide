#include "Git/Worktrees/WorktreeListing.h"

#include "Git/Worktrees/WorktreesModel.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace Graphite {

	namespace {

		// A worktree with the time it was last active; one whose time is unknown sorts after every known one.
		struct Placed
		{
			std::size_t Entry = 0;
			std::optional<std::chrono::sys_seconds> LastActivity;
			std::string Name;
		};

		bool IsNewer(const Placed& left, const Placed& right)
		{
			if (left.LastActivity != right.LastActivity)
			{
				return left.LastActivity.has_value() && (!right.LastActivity.has_value() || *left.LastActivity > *right.LastActivity);
			}

			return left.Name < right.Name;
		}

		bool HasLinkedWorktree(const std::vector<Core::WorktreeEntry>& entries)
		{
			return std::ranges::any_of(entries, [](const Core::WorktreeEntry& entry) { return !entry.IsMain; });
		}

		// By its index into the model's entries.
		bool HasChanges(const WorktreesModel& model, std::size_t entry)
		{
			return model.CountOfKey(model.EntryKeys()[entry]).value_or(0) > 0;
		}
	}

	WorktreeOrderMoment WorktreeMoments::Next(std::uint64_t openings, std::string_view search, bool sectionOpen)
	{
		Seen now{.Openings = openings, .Search = std::string(search), .SectionOpen = sectionOpen};

		if (!m_Seen.has_value())
		{
			m_Seen = std::move(now);
			return WorktreeOrderMoment::Opening;
		}

		const Seen before = std::exchange(*m_Seen, std::move(now));

		if (before.Openings != openings)
		{
			return WorktreeOrderMoment::Opening;
		}

		if (before.Search != search)
		{
			return WorktreeOrderMoment::SearchChanged;
		}

		if (before.SectionOpen != sectionOpen)
		{
			return WorktreeOrderMoment::SectionToggled;
		}

		return WorktreeOrderMoment::None;
	}

	void WorktreeOrder::Follow(WorktreeOrderMoment moment, const WorktreesModel& model, const std::filesystem::path& openFolder)
	{
		// WARNING: Asked every frame, so nothing is keyed or copied unless the folder or the list changed.
		bool folderChanged = false;

		if (!m_Built || openFolder.native() != m_OpenFolderPath.native())
		{
			m_OpenFolderPath = openFolder;
			Core::PathKeyText folder = openFolder.empty() ? Core::PathKeyText{} : Core::PathKey(openFolder);
			folderChanged = folder != m_OpenFolder;
			m_OpenFolder = std::move(folder);
		}

		// A worktree added or removed is a change to the repository, applied at once; a list read again with the same
		// worktrees, their heads moved, is not.
		bool worktreesChanged = false;

		if (!m_Built || model.EntriesGeneration() != m_Generation)
		{
			m_Generation = model.EntriesGeneration();
			worktreesChanged = model.EntryKeys() != m_Paths;
		}

		// So is the first count settling, which replaces the waiting row once.
		const bool rebuilds = !m_Built || moment != WorktreeOrderMoment::None || worktreesChanged || folderChanged ||
							  (m_Listing.Waiting && model.IsFirstCountSettled());

		if (rebuilds)
		{
			WorktreeListing next = Build(model, m_OpenFolder);
			m_Built = true;
			m_Paths = model.EntryKeys();

			if (next != m_Listing)
			{
				m_Listing = std::move(next);
				++m_Version;
			}

			return;
		}

		// Between moments only the fold row's count follows the counts.
		const std::size_t foldedWithChanges = FoldedWithChangesNow(model);

		if (foldedWithChanges != m_Listing.FoldedWithChanges)
		{
			m_Listing.FoldedWithChanges = foldedWithChanges;
			++m_Version;
		}
	}

	WorktreeListing WorktreeOrder::Build(const WorktreesModel& model, const Core::PathKeyText& openFolder) const
	{
		const std::vector<Core::WorktreeEntry>* const entries = model.Entries();

		if (entries == nullptr || !HasLinkedWorktree(*entries))
		{
			return {};
		}

		if (!model.IsFirstCountSettled())
		{
			return WorktreeListing{.Waiting = true};
		}

		WorktreeListing listing;
		std::vector<Placed> others;
		others.reserve(entries->size());

		for (std::size_t index = 0; index < entries->size(); ++index)
		{
			const Core::WorktreeEntry& entry = (*entries)[index];
			const bool isOpenFolder = !openFolder.empty() && model.EntryKeys()[index] == openFolder;

			if (isOpenFolder && listing.Shown.empty())
			{
				listing.Shown.push_back(WorktreeListed{.Entry = index, .ThisFolder = true});
				continue;
			}

			others.push_back(Placed{.Entry = index, .LastActivity = model.LastActivity(entry.Path), .Name = WorktreeFolderName(entry)});
		}

		std::ranges::stable_sort(others, IsNewer);

		const std::chrono::sys_seconds now{std::chrono::seconds{model.NowSeconds()}};

		for (const Placed& placed : others)
		{
			// Folded only when known idle and not known to have changes: a worktree whose time is unknown stays in view.
			const bool idle = placed.LastActivity.has_value() && now - *placed.LastActivity >= std::chrono::seconds{kFoldAfterSeconds};

			if (idle && !HasChanges(model, placed.Entry))
			{
				listing.Folded.push_back(placed.Entry);
			}
			else
			{
				listing.Shown.push_back(WorktreeListed{.Entry = placed.Entry});
			}
		}

		return listing;
	}

	std::size_t WorktreeOrder::FoldedWithChangesNow(const WorktreesModel& model) const
	{
		const std::vector<Core::WorktreeEntry>* const entries = model.Entries();

		if (entries == nullptr)
		{
			return 0;
		}

		return static_cast<std::size_t>(std::ranges::count_if(m_Listing.Folded, [&](std::size_t entry) {
			return entry < entries->size() && HasChanges(model, entry);
		}));
	}

	std::string WorktreeFolderName(const Core::WorktreeEntry& entry)
	{
		if (entry.Path.empty())
		{
			return entry.Name;
		}

		return Core::ToUtf8(Core::NormalizeDirectory(entry.Path).filename());
	}
}
