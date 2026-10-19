#pragma once

#include "Editor/Overlays/LensRows.h"
#include "Language/Api/LanguageService.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Core {

	class RowMap;
	class TextBuffer;
}

namespace Graphite {

	struct Lens
	{
		std::size_t DocumentRow = 0;

		std::size_t Column = 0;

		std::size_t NameLine = 0;
		Utf16Column NameColumn;

		std::size_t Count = 0;
		bool CountKnown = false;
	};

	struct KnownCount
	{
		std::string Name;
		DeclarationKind Kind = DeclarationKind::Other;
		std::size_t Count = 0;
	};

	// WARNING: SetDeclarations must run again whenever the wrap width changes, because
	// Core::RowMap::FirstRowOf then returns different rows for the same declarations.
	class LensModel
	{
	public:
		void SetDeclarations(const std::vector<Declaration>& declarations, const Core::RowMap& rows,
							 const Core::TextBuffer& buffer, std::size_t tabWidth);

		void SetCount(std::size_t documentRow, std::size_t count);

		void OnLinesChanged(std::size_t fromLine, std::ptrdiff_t delta, const Core::RowMap& rows);

		std::vector<std::size_t> RowsNeedingCounts(std::size_t fromRow, std::size_t toRow) const;

		std::vector<std::size_t> TakeRowsToAsk(std::size_t fromRow, std::size_t toRow);

		void ForgetAsked();

		const LensRows& GetRows() const { return m_Rows; }

		const Lens* LensAt(std::size_t documentRow) const;

		std::vector<KnownCount> KnownCounts() const;

		void SeedCounts(std::vector<KnownCount> counts);

	private:
		struct Entry
		{
			Lens LensValue;
			std::string Name;
			DeclarationKind Kind = DeclarationKind::Other;
			bool Asked = false;
		};

		void CarryOverInto(std::vector<Entry>& next);

		static bool ByDocumentRow(const Entry& left, const Entry& right);
		static bool SameDocumentRow(const Entry& left, const Entry& right);
		static bool EntryBeforeRow(const Entry& entry, std::size_t documentRow);

		std::vector<Entry> m_Entries;
		LensRows m_Rows;

		std::vector<KnownCount> m_Seeded;
	};
}
