#pragma once

#include "Core/UI/Panel.h"
#include "Theme/EditorPalette.h"
#include "Language/Diagnostics/DiagnosticsModel.h"
#include "Theme/ActiveScheme.h"

#include "Language/Diagnostics/ProblemIdentity.h"

#include <filesystem>
#include <optional>

namespace Core {

	class DocumentSet;
}

namespace Graphite {

	// Every open document's problems, grouped by file. Reads DiagnosticsModel as const — there is
	// nothing on the model to call and no path from it to a language server, which is what makes
	// it structurally impossible for this panel to drain a client the way LanguageServices.h:135
	// warns against, rather than merely a rule this panel happens to follow.
	//
	// Lists a file only when Core::DocumentSet::Find answers for it. The model also holds entries
	// for files opened by preview panes and by the language layer's warm-up, neither of which has
	// a tab; that filter is what keeps them out of a list that says it is about open documents,
	// and it is also what makes a group disappear on the frame its tab closes rather than a frame
	// later. No preview pane: unlike ReferencesPanel this has nothing to preview into — a row's
	// own file is already open, so a click can jump straight there.
	class ProblemsPanel final : public Core::Panel
	{
	public:
		ProblemsPanel(const DiagnosticsModel& diagnostics, Core::DocumentSet& documents,
					  const ActiveScheme& scheme);

		void OnImGuiRender() override;

	private:
		// One file's header and its rows. `entry` is the model's own record for `path` and has
		// already been checked non-empty and open by the caller. `palette` is read once a frame
		// by OnImGuiRender and threaded down, rather than re-read per row.
		void RenderFile(const std::filesystem::path& path, const DiagnosticsModel::Entry& entry,
						const EditorPalette& palette);

		// Draws one row and, on a click, navigates to it through SelectInDocument. The same
		// click model as the file tree, References and Find in Files: a single click selects the
		// row and opens it as a preview, a double click or Enter opens it permanently.
		void RenderRow(const std::filesystem::path& path, const Diagnostic& diagnostic, int rowId,
					   const EditorPalette& palette);

		void OpenAndSelect(const std::filesystem::path& path, const Diagnostic& diagnostic, bool permanent);

		const DiagnosticsModel& m_Diagnostics;
		Core::DocumentSet& m_Documents;
		const ActiveScheme& m_Scheme;

		// The problem last clicked or moved to, by identity rather than list position so a
		// refresh cannot move the highlight onto another problem. Dropped on a frame where no row
		// matches it; m_SelectionSeen is that frame's tally.
		std::optional<ProblemIdentity> m_Selected;
		bool m_SelectionSeen = false;
	};
}
