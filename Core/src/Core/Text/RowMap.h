#pragma once

#include <cstddef>
#include <vector>

namespace Core {

	class TextBuffer;

	struct RowRef
	{
		std::size_t Line = 0;
		std::size_t SubRow = 0;
	};

	class RowMap
	{
	public:
		static constexpr std::size_t kNoWrap = static_cast<std::size_t>(-1);
		static constexpr std::size_t kNoRows = 0;

		void Rebuild(const TextBuffer& buffer, std::size_t columnsPerRow, std::size_t tabWidth);

		void UpdateLine(const TextBuffer& buffer, std::size_t line);

		void InsertLines(const TextBuffer& buffer, std::size_t firstNewLine, std::size_t newLineCount);

		void RemoveLines(std::size_t firstRemovedLine, std::size_t removedLineCount);

		// Sets only the line's row count and leaves its breaks as they are, so RowsOf and BreaksOf can
		// disagree for the line until UpdateLine or Rebuild wraps it again.
		void SetRowsOf(std::size_t line, std::size_t rowCount);

		std::size_t GetTotalRows() const { return m_TotalRows; }
		std::size_t GetLineCount() const { return m_RowsPerLine.size(); }

		std::size_t GetColumnsPerRow() const { return m_ColumnsPerRow; }

		RowRef RowAt(std::size_t row) const;

		std::size_t FirstRowOf(std::size_t line) const;

		std::size_t RowsOf(std::size_t line) const;

		const std::vector<std::size_t>& BreaksOf(std::size_t line) const;

	private:
		void SetRowCount(std::size_t line, std::size_t rowCount);
		void RebuildTree();
		std::size_t CountRowsBefore(std::size_t line) const;

		std::vector<std::size_t> m_RowsPerLine;
		std::vector<std::vector<std::size_t>> m_Breaks;
		// One-based, with node 0 unused: stepping by the lowest set bit would never move off node 0.
		std::vector<std::size_t> m_FenwickTree;
		std::size_t m_TotalRows = 0;
		std::size_t m_ColumnsPerRow = kNoWrap;
		std::size_t m_TabWidth = 0;
	};
}