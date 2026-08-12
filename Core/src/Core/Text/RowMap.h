#pragma once

#include <cstddef>
#include <vector>

namespace Core {

	class TextBuffer;

	struct RowRef
	{
		std::size_t line = 0;
		std::size_t subRow = 0;
	};

	class RowMap
	{
	public:
		static constexpr std::size_t kNoWrap = static_cast<std::size_t>(-1);

		void Rebuild(const TextBuffer& buffer, std::size_t columnsPerRow, std::size_t tabWidth);

		void UpdateLine(const TextBuffer& buffer, std::size_t line);

		void InsertLines(const TextBuffer& buffer, std::size_t at, std::size_t count);

		void RemoveLines(std::size_t at, std::size_t count);

		void SetRowsOf(std::size_t line, std::size_t rows);

		std::size_t TotalRows() const { return m_TotalRows; }
		std::size_t GetLineCount() const { return m_RowsPerLine.size(); }

		std::size_t GetColumnsPerRow() const { return m_ColumnsPerRow; }

		RowRef RowAt(std::size_t row) const;

		std::size_t FirstRowOf(std::size_t line) const;

		std::size_t RowsOf(std::size_t line) const;

		const std::vector<std::size_t>& BreaksOf(std::size_t line) const;

	private:
		void SetCount(std::size_t line, std::size_t rows);
		void RebuildTree();
		std::size_t PrefixSum(std::size_t line) const;

		std::vector<std::size_t> m_RowsPerLine;
		std::vector<std::vector<std::size_t>> m_Breaks;
		std::vector<std::size_t> m_Tree; // Fenwick, one-based internally
		std::size_t m_TotalRows = 0;
		std::size_t m_ColumnsPerRow = kNoWrap;
		std::size_t m_TabWidth = 0;
	};
}