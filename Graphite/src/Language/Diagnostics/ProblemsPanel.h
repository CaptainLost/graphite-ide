#pragma once

#include "Core/UI/Panel.h"
#include "Theme/EditorPalette.h"
#include "Language/Diagnostics/DiagnosticsModel.h"
#include "Theme/ActiveScheme.h"

#include <filesystem>

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

		// Draws one row and, on a click, navigates to it through
		// SelectInDocument: opened permanently, not as a preview tab, because a problem clicked
		// from this list is somewhere the user meant to go.
		void RenderRow(const std::filesystem::path& path, const Diagnostic& diagnostic, int rowId,
					   const EditorPalette& palette);

		void OpenAndSelect(const std::filesystem::path& path, const Diagnostic& diagnostic);

		const DiagnosticsModel& m_Diagnostics;
		Core::DocumentSet& m_Documents;
		const ActiveScheme& m_Scheme;
	};
}
