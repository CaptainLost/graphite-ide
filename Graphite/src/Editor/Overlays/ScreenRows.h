#pragma once

#include "Editor/Overlays/LensRows.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Graphite {

	struct PeekPlacement
	{
		// The first row of the line below the peek; the row count when the peek is under the last line.
		std::size_t BeforeDocumentRow = 0;

		std::size_t Rows = 0;
	};

	// Document rows with the rows that are not document rows between them: a lens row above each anchored row and at
	// most one peek block. On the document row a peek sits before, the peek's rows come first, then that row's lens,
	// then the row itself.
	//
	// WARNING: The lens rows are held by address and read on every call, so an edit that shifts their anchors
	// mid-frame is seen at once. Build one per frame and never keep it past the LensRows it was given.
	class ScreenRows
	{
	public:
		enum class RowKind : std::uint8_t
		{
			Document,
			Lens,
			Peek
		};

		struct Row
		{
			RowKind Kind = RowKind::Document;

			// For a lens or a peek row, the document row it sits above.
			std::size_t DocumentRow = 0;

			std::size_t PeekRow = 0;
		};

		ScreenRows(const LensRows& lenses, std::optional<PeekPlacement> peek);
		ScreenRows(LensRows&& lenses, std::optional<PeekPlacement> peek) = delete;

		std::size_t TotalScreenRows(std::size_t documentRowCount) const;

		// WARNING: A screenRow at or past TotalScreenRows is answered with a document row past the end, never a lens
		// or a peek row, as LensRows::At answers. The mouse hit test relies on that.
		Row At(std::size_t screenRow) const;

		std::size_t ScreenRowOf(std::size_t documentRow) const;

		std::optional<std::size_t> PeekTop() const;

	private:
		const LensRows* m_Lenses = nullptr;

		std::optional<PeekPlacement> m_Peek;
	};
}
