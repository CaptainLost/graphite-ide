#pragma once

#include "Git/ChangesRows.h"
#include "Git/DiffWindows.h"
#include "Git/GitModel.h"
#include "Git/GitPalette.h"
#include "Theme/ActiveScheme.h"

#include "Core/UI/Panel.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Core {
	class DocumentSet;
}

namespace Graphite {

	// Not used by this task, forward-declared rather than included: DocumentWindows.h pulls in
	// most of the editor, and ChangesPanel only needs a reference to hand to Task 10, which
	// wires the discard confirmation to it.
	class DocumentWindows;

	// The working tree: what is conflicted, what is staged, what is not, and the box that
	// turns the staged half into a commit.
	//
	// A panel docked beside the explorer rather than a section of GitView, because the two
	// answer different questions. GitView is a reader's view of history — which commit,
	// which file, which branch — and this is a writer's view of right now. They are wanted
	// on screen at the same time.
	class ChangesPanel final : public Core::Panel
	{
	public:
		ChangesPanel(GitModel& model, const Core::DocumentSet& documents,
					DocumentWindows& documentWindows, const ActiveScheme& scheme,
					DiffWindows& diffWindows);

		void OnImGuiRender() override;

	private:
		void RenderSection(const char* heading, const std::vector<std::size_t>& rows,
						   const Core::WorkingTreeStatus& status, const GitPalette& palette);

		// One row, and everything a row can do: click to open its diff, the contextual
		// stage or unstage button, and the context menu that holds discard.
		void RenderRow(std::size_t entry, const Core::WorkingTreeStatus& status,
					   const GitPalette& palette);

		void RenderCommitBox();

		// The error a write left behind, with a way to dismiss it. Drawn above the commit
		// box rather than as a modal: a failed stage is information, not a question.
		void RenderWriteError();

		// Which side of the file this row's diff shows. A staged row compares the index
		// against HEAD; every other row compares the disk against the index.
		Core::DiffTarget TargetForSection() const;

		// The colour the gutter gives this kind of change, so one concept is not given two
		// colour schemes.
		unsigned int ColourOfLetter(char letter, const GitPalette& palette) const;

		GitModel& m_Model;
		const Core::DocumentSet& m_Documents;

		// Held for Task 10, which tells the editor about files a confirmed discard just
		// touched. Not read by this task.
		DocumentWindows& m_DocumentWindows;

		const ActiveScheme& m_Scheme;
		DiffWindows& m_DiffWindows;

		// Which section RenderRow is currently drawing, so a row knows whether its button
		// stages or unstages without every call site passing it down.
		enum class Section
		{
			Conflicted,
			Staged,
			Unstaged
		};

		Section m_Section = Section::Unstaged;

		std::string m_Message;
		bool m_Amend = false;
	};
}
