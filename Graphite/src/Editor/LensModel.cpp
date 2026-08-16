#include "Editor/LensModel.h"

#include "Core/Text/Columns.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextBuffer.h"
#include "Lang/Server/LspClient.h"

#include <algorithm>

namespace Graphite {

	namespace {

		bool ByDocumentRow(const Lens& a, const Lens& b)
		{
			return a.DocumentRow < b.DocumentRow;
		}
	}

	void LensModel::SetDeclarations(const std::vector<Declaration>& declarations,
									const Core::RowMap& rows, const Core::TextBuffer& buffer,
									std::size_t tabWidth)
	{
		std::vector<Lens> next;
		next.reserve(declarations.size());

		for (const Declaration& declaration : declarations)
		{
			const std::string lineText = buffer.LineText(declaration.Line);
			const std::size_t byteOffset = FromUtf16Column(lineText, declaration.Column);

			Lens lens;
			lens.DocumentRow = rows.FirstRowOf(declaration.Line);
			lens.Column = Core::ColumnOfByte(lineText, byteOffset, tabWidth);
			next.push_back(lens);
		}

		std::sort(next.begin(), next.end(), ByDocumentRow);
		next.erase(std::unique(next.begin(), next.end(),
								[](const Lens& a, const Lens& b) { return a.DocumentRow == b.DocumentRow; }),
				  next.end());

		// A reply arrives for every declaration every time, so without this a lens whose row
		// survived the rebuild would still flash back to "asking" before the next answer fills it
		// in again. m_Lenses is still the pre-rebuild set here, so LensAt looks up the old count.
		for (Lens& lens : next)
		{
			if (const Lens* previous = LensAt(lens.DocumentRow); previous != nullptr)
			{
				lens.Count = previous->Count;
				lens.CountKnown = previous->CountKnown;
			}
		}

		m_Lenses = std::move(next);

		std::vector<std::size_t> anchors;
		anchors.reserve(m_Lenses.size());

		for (const Lens& lens : m_Lenses)
		{
			anchors.push_back(lens.DocumentRow);
		}

		m_Rows.SetAnchors(std::move(anchors));
	}

	void LensModel::SetCount(std::size_t documentRow, std::size_t count)
	{
		const auto it = std::lower_bound(m_Lenses.begin(), m_Lenses.end(), documentRow,
										 [](const Lens& lens, std::size_t row) { return lens.DocumentRow < row; });

		if (it != m_Lenses.end() && it->DocumentRow == documentRow)
		{
			it->Count = count;
			it->CountKnown = true;
		}
	}

	void LensModel::OnLinesChanged(std::size_t fromLine, std::ptrdiff_t delta, const Core::RowMap& rows)
	{
		const std::size_t fromRow = rows.FirstRowOf(fromLine);

		std::vector<Lens> shifted;
		shifted.reserve(m_Lenses.size());

		for (const Lens& lens : m_Lenses)
		{
			if (lens.DocumentRow < fromRow)
			{
				shifted.push_back(lens);
				continue;
			}

			const std::ptrdiff_t moved = static_cast<std::ptrdiff_t>(lens.DocumentRow) + delta;

			// Mirrors LensRows::ShiftAnchors: a negative shift can push an anchor at or below
			// fromRow up past it, and that anchor no longer corresponds to anything sensible until
			// SetDeclarations replaces this stopgap with exact anchors, so it is dropped.
			if (moved < static_cast<std::ptrdiff_t>(fromRow))
			{
				continue;
			}

			Lens next = lens;
			next.DocumentRow = static_cast<std::size_t>(moved);
			shifted.push_back(next);
		}

		std::sort(shifted.begin(), shifted.end(), ByDocumentRow);
		m_Lenses = std::move(shifted);

		m_Rows.ShiftAnchors(delta, fromRow);
	}

	std::vector<std::size_t> LensModel::RowsNeedingCounts(std::size_t fromRow, std::size_t toRow) const
	{
		std::vector<std::size_t> result;

		for (const Lens& lens : m_Lenses)
		{
			if (lens.CountKnown)
			{
				continue;
			}

			if (lens.DocumentRow < fromRow || lens.DocumentRow >= toRow)
			{
				continue;
			}

			result.push_back(lens.DocumentRow);
		}

		return result;
	}

	const Lens* LensModel::LensAt(std::size_t documentRow) const
	{
		const auto it = std::lower_bound(m_Lenses.begin(), m_Lenses.end(), documentRow,
										 [](const Lens& lens, std::size_t row) { return lens.DocumentRow < row; });

		if (it != m_Lenses.end() && it->DocumentRow == documentRow)
		{
			return &*it;
		}

		return nullptr;
	}
}
