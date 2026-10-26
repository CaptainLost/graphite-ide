#pragma once

#include "Core/Text/TextChange.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
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
		using CaretOffset = std::size_t;

		TextBuffer() = default;
		explicit TextBuffer(std::string original);

		// WARNING: The starts must be the line index of the text: 0, then the offset after every '\n'.
		static TextBuffer FromIndexedText(std::string original, std::vector<std::size_t> lineStarts);

		void Insert(std::size_t offset, std::string_view text);
		void Erase(std::size_t from, std::size_t to);

		std::optional<CaretOffset> Undo();

		std::optional<CaretOffset> Redo();

		void BeginTransaction();
		void EndTransaction();

		void BreakUndoGroup() { m_NextEditStartsGroup = true; }

		bool CanUndo() const { return !m_Undo.empty(); }
		bool CanRedo() const { return !m_Redo.empty(); }

		std::size_t GetRevision() const;

		// Rises by one with every change to the text, undo and redo included, and never goes back.
		std::uint64_t GetChangeSequence() const { return m_ChangeSequence; }

		// The changes after `sequence`, oldest first; nullopt when the log no longer reaches back
		// that far. WARNING: The span is invalidated by the next change to the buffer.
		std::optional<std::span<const TextChange>> ChangesSince(std::uint64_t sequence) const;

		// One undo step and one change marked WholeContent, so readers recompute instead of
		// mapping old offsets into text that has nothing to do with them.
		void ReplaceContent(std::string_view text);

		std::optional<std::size_t> GetTopUndoGroup() const;
		std::optional<std::size_t> GetTopRedoGroup() const;

		bool HoldsUndoGroup(std::size_t groupId) const;
		bool HoldsRedoGroup(std::size_t groupId) const;

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

		std::size_t GetAppendBufferSize() const { return m_AppendBuffer.size(); }

	private:
		enum class Source
		{
			Original,
			Append
		};

		struct Piece
		{
			Source Origin = Source::Original;
			std::size_t SourceOffset = 0;
			std::size_t Length = 0;
			std::size_t DocumentStart = 0;
		};

		const std::string& SourceBuffer(Source source) const;

		std::size_t IndexOfPieceCovering(std::size_t offset) const;

		std::size_t SplitPieceAt(std::size_t offset);

		std::size_t IndexOfFirstPieceStartingAtOrAfter(std::size_t offset) const;

		void RefreshDocumentStartsFrom(std::size_t firstPieceIndex);

		std::vector<std::size_t> BuildLineIndex() const;

		void PatchLineIndexForInsert(std::size_t offset, std::string_view text);
		void PatchLineIndexForErase(std::size_t from, std::size_t to);

		std::size_t InsertWithoutHistory(std::size_t offset, std::string_view text);
		void EraseWithoutHistory(std::size_t from, std::size_t to);

		void InsertAppendedPiece(std::size_t documentOffset, std::size_t appendOffset, std::string_view text);

		enum class EditKind
		{
			Insertion,
			Deletion
		};

		enum class EditDirection
		{
			AsRecorded,
			Reversed
		};

		static constexpr std::size_t kNoGroupId = 0;

		struct Edit
		{
			std::size_t Id = 0;
			std::size_t DocumentOffset = 0;
			std::string Text;
			EditKind Kind = EditKind::Deletion;

			std::size_t AppendOffset = 0;
			bool HasAppendOffset = false;

			std::size_t GroupId = kNoGroupId;
		};

		void ApplyEdit(Edit& edit, EditDirection direction);

		bool ContinuesLastGroup(std::size_t from, std::size_t to, EditKind kind) const;

		std::size_t ChooseGroupForNewEdit(std::size_t from, std::size_t to, std::string_view text,
										  EditKind kind);

		void RememberLastEdit(EditKind kind, std::size_t from, std::string_view text);

		std::string m_OriginalBuffer;
		std::string m_AppendBuffer;
		std::vector<Piece> m_Pieces;
		std::size_t m_Length = 0;
		std::vector<std::size_t> m_LineStarts{0};
		std::vector<Edit> m_Undo;
		std::vector<Edit> m_Redo;
		std::size_t m_NextEditId = 1;
		std::size_t m_NextGroupId = kNoGroupId + 1;
		std::size_t m_TransactionDepth = 0;
		std::size_t m_TransactionGroupId = kNoGroupId;

		std::optional<EditKind> m_LastEditKind;
		std::size_t m_LastEditEnd = 0;
		std::size_t m_LastEditStart = 0;
		bool m_NextEditStartsGroup = true;

		void RecordChange(TextChange change);

		std::vector<TextChange> m_Changes;
		std::uint64_t m_ChangeSequence = 0;
		bool m_RecordsChanges = true;
	};
}
