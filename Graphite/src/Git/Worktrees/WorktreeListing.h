#pragma once

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Worktree.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	class WorktreesModel;

	// The moments spec §4.2 builds the Worktrees section's order again at. A worktree added or removed is a change to the
	// repository, which WorktreeOrder::Follow finds by itself and applies at once.
	enum class WorktreeOrderMoment
	{
		None,
		Opening,        // the window drawn after a frame it was not
		SearchChanged,  // the sidebar's search
		SectionToggled  // the section opened or closed
	};

	// Which of the moments has come since the last call: an opening first, then a search, then the section toggled.
	class WorktreeMoments
	{
	public:
		// `openings` moves every time the window is drawn after a frame it was not.
		WorktreeOrderMoment Next(std::uint64_t openings, std::string_view search, bool sectionOpen);

	private:
		struct Seen
		{
			std::uint64_t Openings = 0;
			std::string Search;
			bool SectionOpen = false;
		};

		std::optional<Seen> m_Seen;
	};

	struct WorktreeListed
	{
		std::size_t Entry = 0; // index into WorktreesModel::Entries()
		bool ThisFolder = false;

		friend bool operator==(const WorktreeListed&, const WorktreeListed&) = default;
	};

	struct WorktreeListing
	{
		std::vector<WorktreeListed> Shown; // this folder first, then by last activity newest first, the name breaking a tie
		std::vector<std::size_t> Folded;   // idle kFoldAfterSeconds and more with no known changes, in the same order
		std::size_t FoldedWithChanges = 0; // folded ones found with changes since the build: said in the fold row, not moved

		// Every worktree is being counted once before the first build: the section shows one waiting row for its rows.
		bool Waiting = false;

		friend bool operator==(const WorktreeListing&, const WorktreeListing&) = default;
	};

	// The order of the Worktrees section, fixed between the moments of spec §4.2 so that no row moves while the user looks
	// at it: built at an opening, a search, the section opened or closed, a worktree added or removed, and once the first
	// count settles. Between them only FoldedWithChanges follows the counts; values on the rows fill in place.
	class WorktreeOrder
	{
	public:
		void Follow(WorktreeOrderMoment moment, const WorktreesModel& model, const std::filesystem::path& openFolder);

		// Empty while the worktrees are unread, and for a repository without a linked worktree: it has no section.
		const WorktreeListing& Listing() const { return m_Listing; }

		// Moved every time the listing changes, so the rows built from it are built again.
		std::uint64_t Version() const { return m_Version; }

		static constexpr std::int64_t kFoldAfterSeconds = 7 * 24 * 3600;

	private:
		WorktreeListing Build(const WorktreesModel& model, const Core::PathKeyText& openFolder) const;
		std::size_t FoldedWithChangesNow(const WorktreesModel& model) const;

		WorktreeListing m_Listing;
		std::uint64_t m_Version = 0;
		bool m_Built = false;
		std::vector<Core::PathKeyText> m_Paths; // the entries the listing indexes, in their order
		Core::PathKeyText m_OpenFolder;
	};

	// The last segment of the worktree's path; git's name for it when the path is unknown.
	std::string WorktreeFolderName(const Core::WorktreeEntry& entry);
}
