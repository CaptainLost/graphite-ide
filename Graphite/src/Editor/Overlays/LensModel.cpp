#include "Editor/Overlays/LensModel.h"

#include "Core/Text/Columns.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextBuffer.h"
#include "Language/Api/Declarations.h"
#include "Language/Server/LspProtocol.h"

#include <algorithm>
#include <optional>
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

		using IndicesByKey = std::unordered_map<std::string, std::vector<std::size_t>>;

		std::string CarryOverKey(const std::string& name, DeclarationKind kind)
		{
			return name + '\0' + std::to_string(static_cast<int>(kind));
		}

		template<typename Named>
		std::string KeyOf(const Named& named)
		{
			return CarryOverKey(named.Name, named.Kind);
		}

		template<typename Named>
		IndicesByKey IndexByKey(const std::vector<Named>& items)
		{
			IndicesByKey byKey;

			for (std::size_t index = 0; index < items.size(); ++index)
			{
				byKey[KeyOf(items[index])].push_back(index);
			}

			return byKey;
		}

		template<typename Accepts>
		std::optional<std::size_t> TakeFirst(const IndicesByKey& byKey, const std::string& key,
											 std::vector<bool>& taken, Accepts accepts)
		{
			const IndicesByKey::const_iterator found = byKey.find(key);

			if (found == byKey.end())
			{
				return std::nullopt;
			}

			for (const std::size_t index : found->second)
			{
				if (!taken[index] && accepts(index))
				{
					taken[index] = true;
					return index;
				}
			}

			return std::nullopt;
		}

		bool AnyRow(std::size_t)
		{
			return true;
		}

		void CopyCount(const Lens& from, Lens& to)
		{
			to.Count = from.Count;
			to.CountKnown = from.CountKnown;
		}
	}

	// WARNING: Overloads share a name and a kind, so a lens on the same row is matched first and the
	// rest in order. Only a same-row match keeps Asked: a count in flight answers to the row it was
	// asked for, and a lens that moved would never hear it.
	void LensModel::CarryOverInto(std::vector<Entry>& next)
	{
		const IndicesByKey previousByKey = IndexByKey(m_Entries);
		const IndicesByKey seededByKey = IndexByKey(m_Seeded);

		std::vector<bool> previousTaken(m_Entries.size(), false);
		std::vector<bool> seededTaken(m_Seeded.size(), false);
		std::vector<bool> carried(next.size(), false);

		for (std::size_t nextIndex = 0; nextIndex < next.size(); ++nextIndex)
		{
			Entry& entry = next[nextIndex];
			const auto onTheSameRow = [this, &entry](std::size_t previousIndex)
			{ return m_Entries[previousIndex].LensValue.DocumentRow == entry.LensValue.DocumentRow; };

			if (const std::optional<std::size_t> same = TakeFirst(previousByKey, KeyOf(entry), previousTaken, onTheSameRow))
			{
				CopyCount(m_Entries[*same].LensValue, entry.LensValue);
				entry.Asked = m_Entries[*same].Asked;
				carried[nextIndex] = true;
			}
		}

		for (std::size_t nextIndex = 0; nextIndex < next.size(); ++nextIndex)
		{
			Entry& entry = next[nextIndex];

			if (carried[nextIndex])
			{
				continue;
			}

			if (const std::optional<std::size_t> moved = TakeFirst(previousByKey, KeyOf(entry), previousTaken, AnyRow))
			{
				CopyCount(m_Entries[*moved].LensValue, entry.LensValue);
			}
			else if (const std::optional<std::size_t> seeded = TakeFirst(seededByKey, KeyOf(entry), seededTaken, AnyRow))
			{
				entry.LensValue.Count = m_Seeded[*seeded].Count;
				entry.LensValue.CountKnown = true;
			}
		}

		m_Seeded.clear();
	}

	void LensModel::SetDeclarations(const std::vector<Declaration>& declarations,
									const Core::RowMap& rows, const Core::TextBuffer& buffer,
									std::size_t tabWidth)
	{
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

		CarryOverInto(next);

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
			foundEntry->LensValue.CountFailed = false;
		}
	}

	void LensModel::SetCountFailed(std::size_t documentRow)
	{
		const std::vector<Entry>::iterator foundEntry =
			std::lower_bound(m_Entries.begin(), m_Entries.end(), documentRow, EntryBeforeRow);

		if (foundEntry != m_Entries.end() && foundEntry->LensValue.DocumentRow == documentRow)
		{
			foundEntry->LensValue.CountFailed = true;
		}
	}

	// WARNING: Every name line moves, not only those of lenses whose row moved: a declaration can begin
	// above the edit and name itself below it, and a count is asked at the name line.
	void LensModel::OnLinesChanged(std::size_t fromLine, std::ptrdiff_t delta, const Core::RowMap& rows)
	{
		const std::size_t fromRow = rows.FirstRowOf(fromLine);

		std::vector<Entry> shifted;
		shifted.reserve(m_Entries.size());

		for (const Entry& entry : m_Entries)
		{
			Entry next = entry;
			next.LensValue.NameLine = LineAfterEdit(entry.LensValue.NameLine, fromLine, delta);

			if (entry.LensValue.DocumentRow < fromRow)
			{
				shifted.push_back(std::move(next));
				continue;
			}

			const std::ptrdiff_t moved = static_cast<std::ptrdiff_t>(entry.LensValue.DocumentRow) + delta;

			if (moved < static_cast<std::ptrdiff_t>(fromRow))
			{
				continue;
			}

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

	std::vector<std::size_t> LensModel::TakeRowsToAsk(std::size_t fromRow, std::size_t toRow)
	{
		std::vector<std::size_t> rows;

		for (Entry& entry : m_Entries)
		{
			const bool inRange = entry.LensValue.DocumentRow >= fromRow && entry.LensValue.DocumentRow < toRow;

			if (!inRange || entry.LensValue.CountKnown || entry.Asked)
			{
				continue;
			}

			entry.Asked = true;
			rows.push_back(entry.LensValue.DocumentRow);
		}

		return rows;
	}

	void LensModel::ForgetAsked()
	{
		for (Entry& entry : m_Entries)
		{
			entry.Asked = false;
			entry.LensValue.CountFailed = false;
		}
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
