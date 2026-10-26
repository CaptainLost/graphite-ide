#include "Editor/Overlays/ScreenRows.h"

namespace Graphite {

	namespace {
		ScreenRows::Row FromLensRow(const LensRows::Row& row)
		{
			const ScreenRows::RowKind kind = row.IsLens ? ScreenRows::RowKind::Lens : ScreenRows::RowKind::Document;

			return ScreenRows::Row{.Kind = kind, .DocumentRow = row.DocumentRow};
		}
	}

	ScreenRows::ScreenRows(const LensRows& lenses, std::optional<PeekPlacement> peek)
		: m_Lenses(&lenses), m_Peek(peek)
	{
	}

	std::size_t ScreenRows::TotalScreenRows(std::size_t documentRowCount) const
	{
		const std::size_t peekRows = m_Peek ? m_Peek->Rows : 0;

		return m_Lenses->TotalScreenRows(documentRowCount) + peekRows;
	}

	ScreenRows::Row ScreenRows::At(std::size_t screenRow) const
	{
		const std::optional<std::size_t> peekTop = PeekTop();

		if (!peekTop || screenRow < *peekTop)
		{
			return FromLensRow(m_Lenses->At(screenRow));
		}

		const std::size_t intoPeek = screenRow - *peekTop;

		if (intoPeek < m_Peek->Rows)
		{
			return Row{.Kind = RowKind::Peek, .DocumentRow = m_Peek->BeforeDocumentRow, .PeekRow = intoPeek};
		}

		return FromLensRow(m_Lenses->At(screenRow - m_Peek->Rows));
	}

	std::size_t ScreenRows::ScreenRowOf(std::size_t documentRow) const
	{
		const bool belowPeek = m_Peek && documentRow >= m_Peek->BeforeDocumentRow;
		const std::size_t peekRows = belowPeek ? m_Peek->Rows : 0;

		return m_Lenses->ScreenRowOf(documentRow) + peekRows;
	}

	// The peek begins right under the last screen row of the line above, which is above the lens of the row it
	// sits before.
	std::optional<std::size_t> ScreenRows::PeekTop() const
	{
		if (!m_Peek)
		{
			return std::nullopt;
		}

		if (m_Peek->BeforeDocumentRow == 0)
		{
			return std::size_t{0};
		}

		return m_Lenses->ScreenRowOf(m_Peek->BeforeDocumentRow - 1) + 1;
	}
}
