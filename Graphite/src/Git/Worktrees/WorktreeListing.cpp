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

		std::vector<Core::PathKeyText> PathsOf(const std::vector<Core::WorktreeEntry>* entries)
		{
			std::vector<Core::PathKeyText> paths;

			if (entries == nullptr)
			{
				return paths;
			}

			paths.reserve(entries->size());

			for (const Core::WorktreeEntry& entry : *entries)
			{
				paths.push_back(Core::PathKey(entry.Path));
			}

			return paths;
		}

		bool HasChanges(const WorktreesModel& model, const Core::WorktreeEntry& entry)
		{
			return model.CountOf(entry.Path).value_or(0) > 0;
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
		std::vector<Core::PathKeyText> paths = PathsOf(model.Entries());
		Core::PathKeyText folder = openFolder.empty() ? Core::PathKeyText{} : Core::PathKey(openFolder);

		// A worktree added or removed is a change to the repository, applied at once; so is the first count settling,
		// which replaces the waiting row once.
		const bool rebuilds = !m_Built || moment != WorktreeOrderMoment::None || paths != m_Paths || folder != m_OpenFolder ||
							  (m_Listing.Waiting && model.IsFirstCountSettled());

		WorktreeListing next;

		if (rebuilds)
		{
			next = Build(model, folder);
			m_Built = true;
			m_Paths = std::move(paths);
			m_OpenFolder = std::move(folder);
		}
		else
		{
			next = m_Listing;
			next.FoldedWithChanges = FoldedWithChangesNow(model);
		}

		if (next != m_Listing)
		{
			m_Listing = std::move(next);
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
			const bool isOpenFolder = !openFolder.empty() && Core::PathKey(entry.Path) == openFolder;

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

			if (idle && !HasChanges(model, (*entries)[placed.Entry]))
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
			return entry < entries->size() && HasChanges(model, (*entries)[entry]);
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
