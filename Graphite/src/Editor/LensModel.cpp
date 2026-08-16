#include "Editor/LensModel.h"

#include "Core/Text/Columns.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextBuffer.h"
#include "Lang/Server/LspClient.h"

#include <algorithm>
#include <unordered_map>

namespace Graphite {

	bool LensModel::ByDocumentRow(const Entry& a, const Entry& b)
	{
		return a.LensValue.DocumentRow < b.LensValue.DocumentRow;
	}

	namespace {

		// Name and kind together are the identity a carried-over count is keyed on. Two
		// declarations that share both - an overload set - share one carry-over entry; that is
		// accepted rather than disambiguated further, because clangd counts references by symbol
		// and this model has nothing finer-grained than name and kind to tell overloads apart with.
		std::string CarryOverKey(const std::string& name, DeclarationKind kind)
		{
			return name + '\0' + std::to_string(static_cast<int>(kind));
		}
	}

	void LensModel::SetDeclarations(const std::vector<Declaration>& declarations,
									const Core::RowMap& rows, const Core::TextBuffer& buffer,
									std::size_t tabWidth)
	{
		// Keyed on the declaration's identity, not its row: a row surviving the rebuild does not
		// mean the same declaration still sits on it. A reorder, or a delete and insert landing on
		// the vacated line, would otherwise hand the old row's count to whatever declaration ended
		// up there - a plausible number displayed confidently over the wrong function, with no way
		// for the reader to tell. Keying on identity instead means the common case - a function
		// whose line moved because of an edit above it - keeps its count across the reply instead
		// of losing it, while a genuinely different declaration that lands on the same row starts
		// out unknown, as it should.
		std::unordered_map<std::string, Lens> previousByKey;
		previousByKey.reserve(m_Entries.size());

		for (const Entry& entry : m_Entries)
		{
			previousByKey.emplace(CarryOverKey(entry.Name, entry.Kind), entry.LensValue);
		}

		std::vector<Entry> next;
		next.reserve(declarations.size());

		for (const Declaration& declaration : declarations)
		{
			// A reply describes the text as the server last saw it, and the buffer may have moved
			// on since - an edit that removed lines from the end leaves declarations naming lines
			// that are no longer there. Both RowMap and TextBuffer clamp rather than fail, so
			// keeping one would not crash; it would anchor a lens past the last line of the file,
			// which is a row drawn where no row belongs. Filtered here rather than where the reply
			// arrives because this runs again on every rewrap, against a buffer that may have
			// shrunk since the reply did arrive.
			if (declaration.Line >= buffer.GetLineCount())
			{
				continue;
			}

			const std::string lineText = buffer.LineText(declaration.Line);
			const std::size_t byteOffset = FromUtf16Column(lineText, declaration.Column);

			Entry entry;
			entry.LensValue.DocumentRow = rows.FirstRowOf(declaration.Line);
			entry.LensValue.Column = Core::ColumnOfByte(lineText, byteOffset, tabWidth);
			entry.Name = declaration.Name;
			entry.Kind = declaration.Kind;
			next.push_back(std::move(entry));
		}

		// Stable, not sort: when two declarations land on the same anchor row - two short
		// declarations sharing a source line - std::unique below keeps whichever of them comes
		// first in `declarations`, and that choice needs to be deterministic rather than whatever
		// an unstable sort happens to leave adjacent.
		std::stable_sort(next.begin(), next.end(), ByDocumentRow);
		next.erase(std::unique(next.begin(), next.end(),
								[](const Entry& a, const Entry& b)
								{
									return a.LensValue.DocumentRow == b.LensValue.DocumentRow;
								}),
				  next.end());

		for (Entry& entry : next)
		{
			const auto it = previousByKey.find(CarryOverKey(entry.Name, entry.Kind));

			if (it != previousByKey.end())
			{
				entry.LensValue.Count = it->second.Count;
				entry.LensValue.CountKnown = it->second.CountKnown;
			}
		}

		m_Entries = std::move(next);

		std::vector<std::size_t> anchors;
		anchors.reserve(m_Entries.size());

		for (const Entry& entry : m_Entries)
		{
			anchors.push_back(entry.LensValue.DocumentRow);
		}

		m_Rows.SetAnchors(std::move(anchors));
	}

	void LensModel::SetCount(std::size_t documentRow, std::size_t count)
	{
		const auto it = std::lower_bound(m_Entries.begin(), m_Entries.end(), documentRow,
										 [](const Entry& entry, std::size_t row)
										 {
											 return entry.LensValue.DocumentRow < row;
										 });

		if (it != m_Entries.end() && it->LensValue.DocumentRow == documentRow)
		{
			it->LensValue.Count = count;
			it->LensValue.CountKnown = true;
		}
	}

	void LensModel::OnLinesChanged(std::size_t fromLine, std::ptrdiff_t delta, const Core::RowMap& rows)
	{
		const std::size_t fromRow = rows.FirstRowOf(fromLine);

		std::vector<Entry> shifted;
		shifted.reserve(m_Entries.size());

		for (const Entry& entry : m_Entries)
		{
			if (entry.LensValue.DocumentRow < fromRow)
			{
				shifted.push_back(entry);
				continue;
			}

			const std::ptrdiff_t moved = static_cast<std::ptrdiff_t>(entry.LensValue.DocumentRow) + delta;

			// Mirrors LensRows::ShiftAnchors: a negative shift can push an anchor at or below
			// fromRow up past it, and that anchor no longer corresponds to anything sensible until
			// SetDeclarations replaces this stopgap with exact anchors, so it is dropped.
			if (moved < static_cast<std::ptrdiff_t>(fromRow))
			{
				continue;
			}

			Entry next = entry;
			next.LensValue.DocumentRow = static_cast<std::size_t>(moved);
			shifted.push_back(std::move(next));
		}

		std::sort(shifted.begin(), shifted.end(), ByDocumentRow);
		m_Entries = std::move(shifted);

		m_Rows.ShiftAnchors(delta, fromRow);
	}

	std::vector<std::size_t> LensModel::RowsNeedingCounts(std::size_t fromRow, std::size_t toRow) const
	{
		std::vector<std::size_t> result;

		for (const Entry& entry : m_Entries)
		{
			if (entry.LensValue.CountKnown)
			{
				continue;
			}

			if (entry.LensValue.DocumentRow < fromRow || entry.LensValue.DocumentRow >= toRow)
			{
				continue;
			}

			result.push_back(entry.LensValue.DocumentRow);
		}

		return result;
	}

	const Lens* LensModel::LensAt(std::size_t documentRow) const
	{
		const auto it = std::lower_bound(m_Entries.begin(), m_Entries.end(), documentRow,
										 [](const Entry& entry, std::size_t row)
										 {
											 return entry.LensValue.DocumentRow < row;
										 });

		if (it != m_Entries.end() && it->LensValue.DocumentRow == documentRow)
		{
			return &it->LensValue;
		}

		return nullptr;
	}
}
