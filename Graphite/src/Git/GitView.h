#pragma once

#include "Git/CommitListView.h"
#include "Git/DiffView.h"
#include "Git/GitModel.h"
#include "Theme/ActiveScheme.h"

#include "Core/Git/Diff.h"
#include "Core/UI/Panel.h"

#include <filesystem>
#include <optional>

namespace Core {

	class CommandRegistry;

}

namespace Graphite {

	// Which file in a diff carries this path, on either side. Matching by path rather than
	// by index is what lets a selection survive a refresh that reorders the list.
	std::optional<std::size_t> IndexOfFile(const Core::Diff& diff,
										   const std::filesystem::path& path);

	// The whole history in one panel: refs down the left, commits and their graph in the
	// middle, the selected commit's files and diff on the right.
	//
	// A panel rather than a document tab: DocumentWindows is keyed by filesystem path and
	// has no way to hold a tab that is not a file on disk. Docked into the centre, it fills
	// the same area a document would.
	class GitView final : public Core::Panel
	{
	public:
		GitView(GitModel& model, const ActiveScheme& scheme);

		void OnImGuiRender() override;

		void RegisterCommands(Core::CommandRegistry& commands);

	private:
		void RenderRefs(const std::vector<Core::Branch>& branches);
		void RenderCommits();
		void RenderDetails();

		void RequestDiffForSelection();

		GitModel& m_Model;
		const ActiveScheme& m_Scheme;

		CommitListView m_Commits;
		DiffView m_Diff;

		// The commit the diff on screen was asked for. Compared against the list's current
		// selection each frame, so moving the selection asks for a new diff exactly once
		// rather than every frame it stays put.
		std::optional<Core::Oid> m_DiffFor;

		std::optional<std::filesystem::path> m_SelectedFile;

		std::optional<std::string> m_SelectedRef;
	};
}
