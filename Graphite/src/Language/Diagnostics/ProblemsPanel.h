#pragma once

#include "Core/Text/DocumentSet.h"
#include "Core/UI/Panel.h"
#include "Theme/EditorPalette.h"
#include "Language/Diagnostics/DiagnosticsModel.h"
#include "Theme/ActiveScheme.h"

#include "Language/Diagnostics/ProblemIdentity.h"

#include <filesystem>
#include <optional>

namespace Graphite {

	class ProblemsPanel final : public Core::Panel
	{
	public:
		ProblemsPanel(const DiagnosticsModel& diagnostics, Core::DocumentSet& documents,
					  const ActiveScheme& scheme);

		void OnImGuiRender() override;

	private:
		void RenderFile(const std::filesystem::path& path, const DiagnosticsModel::Entry& entry,
						const EditorPalette& palette);

		void RenderRow(const std::filesystem::path& path, const Diagnostic& diagnostic, int rowId,
					   const EditorPalette& palette);

		void OpenAndSelect(const std::filesystem::path& path, const Diagnostic& diagnostic, Core::TabKind tabKind);

		bool IsOpenDocument(const std::filesystem::path& path) const;

		const DiagnosticsModel& m_Diagnostics;
		Core::DocumentSet& m_Documents;
		const ActiveScheme& m_Scheme;

		std::optional<ProblemIdentity> m_Selected;
		bool m_SelectionSeen = false;
	};
}
