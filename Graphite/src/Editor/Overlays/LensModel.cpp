#include "Editor/Overlays/LensModel.h"

#include "Core/Text/Columns.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextBuffer.h"
#include "Language/Server/LspProtocol.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>

namespace Graphite {

	bool LensModel::ByDocumentRow(const Entry& left, const Entry& right)
	{
		return left.LensValue.DocumentRow < right.LensValue.DocumentRow;
	}

	bool LensModel::SameDocumentRow(const Entry& left, const Entry& right)
	{
		return left.LensValue.DocumentRow == right.LensValue.DocumentRow;
	}

	bool LensModel::EntryBeforeRow(const Entry& entry, std::size_t documentRow)
	{
		return entry.LensValue.DocumentRow < documentRow;
	}

	namespace {

		std::string CarryOverKey(const std::string& name, DeclarationKind kind)
		{
			return name + '\0' + std::to_string(static_cast<int>(kind));
		}
	}

	void LensModel::SetDeclarations(const std::vector<Declaration>& declarations,
									const Core::RowMap& rows, const Core::TextBuffer& buffer,
									std::size_t tabWidth)
	{
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
			if (declaration.Line >= buffer.GetLineCount())
			{
				continue;
			}

			const std::string lineText = buffer.LineText(declaration.Line);
			const std::size_t byteOffset = FromUtf16Column(lineText, declaration.Column);

			Entry entry;
			entry.LensValue.DocumentRow = rows.FirstRowOf(declaration.Line);
			entry.LensValue.Column = Core::ColumnOfByte(lineText, byteOffset, tabWidth);

			entry.LensValue.NameLine = declaration.NameLine;
			entry.LensValue.NameColumn = declaration.NameColumn;

			entry.Name = declaration.Name;
			entry.Kind = declaration.Kind;
			next.push_back(std::move(entry));
		}

		std::stable_sort(next.begin(), next.end(), ByDocumentRow);
		next.erase(std::unique(next.begin(), next.end(), SameDocumentRow), next.end());

		for (const KnownCount& seeded : m_Seeded)
		{
			Lens lens;
			lens.Count = seeded.Count;
			lens.CountKnown = true;

			previousByKey.emplace(CarryOverKey(seeded.Name, seeded.Kind), lens);
		}

		m_Seeded.clear();

		for (Entry& entry : next)
		{
			const std::unordered_map<std::string, Lens>::const_iterator previous =
				previousByKey.find(CarryOverKey(entry.Name, entry.Kind));

			if (previous != previousByKey.end())
			{
				const auto& [previousKey, previousLens] = *previous;

				entry.LensValue.Count = previousLens.Count;
				entry.LensValue.CountKnown = previousLens.CountKnown;
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

	std::vector<KnownCount> LensModel::KnownCounts() const
	{
		std::vector<KnownCount> known;

		for (const Entry& entry : m_Entries)
		{
			if (entry.LensValue.CountKnown)
			{
				known.push_back({entry.Name, entry.Kind, entry.LensValue.Count});
			}
		}

		return known;
	}

	void LensModel::SeedCounts(std::vector<KnownCount> counts)
	{
		m_Seeded = std::move(counts);
	}

	void LensModel::SetCount(std::size_t documentRow, std::size_t count)
	{
		const std::vector<Entry>::iterator foundEntry =
			std::lower_bound(m_Entries.begin(), m_Entries.end(), documentRow, EntryBeforeRow);

		if (foundEntry != m_Entries.end() && foundEntry->LensValue.DocumentRow == documentRow)
		{
			foundEntry->LensValue.Count = count;
			foundEntry->LensValue.CountKnown = true;
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
		const std::vector<Entry>::const_iterator foundEntry =
			std::lower_bound(m_Entries.begin(), m_Entries.end(), documentRow, EntryBeforeRow);

		if (foundEntry != m_Entries.end() && foundEntry->LensValue.DocumentRow == documentRow)
		{
			return &foundEntry->LensValue;
		}

		return nullptr;
	}
}
