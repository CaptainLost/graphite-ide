#pragma once

#include "Git/DiffRows.h"
#include "Git/GitPalette.h"

#include "Core/Git/Diff.h"

namespace Graphite {

	enum class DiffViewMode
	{
		SideBySide,
		Unified
	};

	// Draws one file's diff. Holds only the mode and the scroll position — the rows are
	// rebuilt from the file each frame, because the file comes from a snapshot that may be
	// replaced between frames and rows cached across that swap would point into a diff that
	// is no longer on screen.
	class DiffView
	{
	public:
		void Render(const Core::FileDiff& file, const GitPalette& palette);

		void SetMode(DiffViewMode mode) { m_Mode = mode; }
		DiffViewMode GetMode() const { return m_Mode; }

	private:
		void RenderToolbar(const Core::FileDiff& file);
		void RenderUnified(const Core::FileDiff& file, const GitPalette& palette);
		void RenderSideBySide(const Core::FileDiff& file, const GitPalette& palette);

		DiffViewMode m_Mode = DiffViewMode::SideBySide;
	};
}
