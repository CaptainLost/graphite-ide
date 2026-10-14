#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Graphite {

	// WARNING: pageRows in TextEditorWidget.cpp deliberately does not consult this class, so
	// PageUp/PageDown overshoot by the lenses in the range crossed.
	class LensRows
	{
	public:
		struct Row
		{
			bool IsLens = false;

			std::size_t DocumentRow = 0;
		};

		void SetAnchors(std::vector<std::size_t> anchors);

		std::size_t TotalScreenRows(std::size_t documentRowCount) const;

		// WARNING: A screenRow at or past TotalScreenRows is answered with a document row past the
		// end, never a lens. The mouse hit test relies on that.
		Row At(std::size_t screenRow) const;

		std::size_t ScreenRowOf(std::size_t documentRow) const;

		void ShiftAnchors(std::ptrdiff_t delta, std::size_t fromRow);

	private:
		std::vector<std::size_t> m_Anchors;
	};
}
