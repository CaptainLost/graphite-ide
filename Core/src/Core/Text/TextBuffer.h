#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	struct Position
	{
		std::size_t Line = 0;
		std::size_t Column = 0;

		friend bool operator==(const Position&, const Position&) = default;
	};

	class TextBuffer
	{
	public:
		TextBuffer() = default;
		explicit TextBuffer(std::string original);

		void Insert(std::size_t offset, std::string_view text);
		void Erase(std::size_t from, std::size_t to);

		// Both report where the caret belongs afterwards — the end of what was put back, or
		// the start of what was taken away — because only the history knows which part of
		// the document moved. Nothing when there was nothing to step through.
		std::optional<std::size_t> Undo();

		std::optional<std::size_t> Redo();

		void BeginTransaction();
		void EndTransaction();

		void BreakUndoGroup() { m_BreakGroup = true; }

		bool CanUndo() const { return !m_Undo.empty(); }
		bool CanRedo() const { return !m_Redo.empty(); }

		std::size_t GetRevision() const;

		std::size_t GetLength() const { return m_Length; }
		bool IsEmpty() const { return m_Length == 0; }

		std::string Substring(std::size_t from, std::size_t to) const;
		std::string Materialize() const { return Substring(0, m_Length); }

		char CharAt(std::size_t offset) const;

		std::size_t GetLineCount() const { return m_LineStarts.size(); }

		std::size_t LineStart(std::size_t line) const;

		std::size_t LineLength(std::size_t line) const;

		std::string LineText(std::size_t line) const;

		std::size_t ToOffset(Position position) const;
		Position ToPosition(std::size_t offset) const;

		bool LineIndexMatchesRebuild() const;

		std::size_t GetAppendBufferSize() const { return m_Append.size(); }

	private:
		enum class Source
		{
			Original,
			Append
		};

		struct Piece
		{
			Source source = Source::Original;
			std::size_t offset = 0;
			std::size_t length = 0;
			std::size_t start = 0;
		};

		const std::string& SourceBuffer(Source source) const;

		std::size_t PieceAt(std::size_t offset) const;

		std::size_t SplitAt(std::size_t offset);

		std::size_t PieceStartingAt(std::size_t offset) const;

		void RefreshPieceStarts(std::size_t index);

		std::vector<std::size_t> BuildLineIndex() const;

		void PatchLineIndexForInsert(std::size_t offset, std::string_view text);
		void PatchLineIndexForErase(std::size_t from, std::size_t to);

		std::size_t InsertRaw(std::size_t offset, std::string_view text);
		void EraseRaw(std::size_t from, std::size_t to);

		void InsertPieceRaw(std::size_t offset, std::size_t appendOffset, std::string_view text);

		struct Edit
		{
			std::size_t id = 0;
			std::size_t offset = 0;
			std::string text;
			bool inserted = false;

			std::size_t appendOffset = 0;
			bool hasAppendOffset = false;

			std::size_t group = 0;
		};

		void ApplyEdit(Edit& edit, bool reversed);

		bool ContinuesLastGroup(std::size_t from, std::size_t to, bool inserted) const;

		std::size_t GroupForNewEdit(std::size_t from, std::size_t to, std::string_view text,
									bool inserted);

		std::string m_Original;
		std::string m_Append;
		std::vector<Piece> m_Pieces;
		std::size_t m_Length = 0;
		std::vector<std::size_t> m_LineStarts{0};
		std::vector<Edit> m_Undo;
		std::vector<Edit> m_Redo;
		std::size_t m_NextEditId = 1;
		std::size_t m_NextGroupId = 1;
		std::size_t m_TransactionDepth = 0;
		std::size_t m_TransactionGroup = 0;

		enum class GroupKind
		{
			None,
			Insertion,
			Deletion
		};

		GroupKind m_LastGroupKind = GroupKind::None;
		std::size_t m_InsertEnd = 0;
		std::size_t m_DeleteAt = 0;
		bool m_BreakGroup = true;
	};
}