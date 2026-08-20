#pragma once

#include "Git/CommitListView.h"
#include "Git/DiffView.h"
#include "Git/GitModel.h"
#include "Theme/ActiveScheme.h"

#include "Core/Git/Diff.h"
#include "Core/Git/Ref.h"
#include "Core/UI/Panel.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Graphite {

	// Which file in a diff carries this path, on either side. Matching by path rather than
	// by index is what lets a selection survive a refresh that reorders the list.
	std::optional<std::size_t> IndexOfFile(const Core::Diff& diff,
										   const std::filesystem::path& path);

	// "main", "* main  2/3" for the checked-out branch with a measured distance from its
	// upstream. A pure function of the branch, pulled out of the draw loop so it can be
	// checked without a frame to draw into.
	std::string RefLabel(const Core::Branch& branch);

	// The whole history in one panel: refs down the left, and on the right the commit list
	// above the diff of whichever file is selected under an expanded commit.
	//
	// A panel rather than a document tab: DocumentWindows is keyed by filesystem path and
	// has no way to hold a tab that is not a file on disk. Docked into the centre, it fills
	// the same area a document would.
	class GitView final : public Core::Panel
	{
	public:
		GitView(GitModel& model, const ActiveScheme& scheme);

		void OnImGuiRender() override;

	private:
		void RenderRefs(const std::vector<Core::Branch>& branches);
		void RenderCommits(const GitPalette& palette);
		void RenderDiff(const GitPalette& palette);

		GitModel& m_Model;
		const ActiveScheme& m_Scheme;

		CommitListView m_Commits;
		DiffView m_Diff;

		std::optional<std::string> m_SelectedRef;

		// Set once, the first frame the panel draws with no log snapshot yet — an empty Ref
		// means HEAD. Guards RequestLog so it fires once rather than every frame the log is
		// still in flight.
		bool m_LogRequested = false;
	};
}
