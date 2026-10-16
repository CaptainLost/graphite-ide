#include "corepch.h"

#include "Core/Text/TextBuffer.h"

#include "Core/Base/Assert.h"

#include <algorithm>

namespace Core {

	namespace {
		constexpr std::size_t kNewlineLength = 1;
	}

	TextBuffer::TextBuffer(std::string original) : m_OriginalBuffer(std::move(original))
	{
		m_Length = m_OriginalBuffer.size();

		if (m_Length > 0)
		{
			m_Pieces.push_back(Piece{.Origin = Source::Original, .SourceOffset = 0, .Length = m_Length, .DocumentStart = 0});
		}

		m_LineStarts = BuildLineIndex();
	}

	const std::string& TextBuffer::SourceBuffer(Source source) const
	{
		return source == Source::Original ? m_OriginalBuffer : m_AppendBuffer;
	}

	std::size_t TextBuffer::IndexOfPieceCovering(std::size_t offset) const
	{
		if (offset >= m_Length)
		{
			return m_Pieces.size();
		}

		const auto searchedOffsetIsBeforePiece = [](std::size_t searchedOffset, const Piece& piece) { return searchedOffset < piece.DocumentStart; };
		const std::vector<Piece>::const_iterator firstPieceStartingAfter = std::upper_bound(m_Pieces.begin(), m_Pieces.end(), offset, searchedOffsetIsBeforePiece);

		return static_cast<std::size_t>(firstPieceStartingAfter - m_Pieces.begin()) - 1;
	}

	void TextBuffer::RefreshDocumentStartsFrom(std::size_t firstPieceIndex)
	{
		std::size_t nextDocumentStart = 0;

		if (firstPieceIndex > 0)
		{
			const Piece& previousPiece = m_Pieces[firstPieceIndex - 1];
			nextDocumentStart = previousPiece.DocumentStart + previousPiece.Length;
		}

		for (std::size_t pieceIndex = firstPieceIndex; pieceIndex < m_Pieces.size(); ++pieceIndex)
		{
			m_Pieces[pieceIndex].DocumentStart = nextDocumentStart;
			nextDocumentStart += m_Pieces[pieceIndex].Length;
		}

		m_Length = nextDocumentStart;
	}

	std::size_t TextBuffer::SplitPieceAt(std::size_t offset)
	{
		if (offset >= m_Length)
		{
			return m_Pieces.size();
		}

		const std::size_t pieceIndex = IndexOfPieceCovering(offset);
		const Piece& piece = m_Pieces[pieceIndex];

		if (piece.DocumentStart == offset)
		{
			return pieceIndex;
		}

		const std::size_t bytesKeptInHead = offset - piece.DocumentStart;

		const Piece tail{.Origin = piece.Origin,
						 .SourceOffset = piece.SourceOffset + bytesKeptInHead,
						 .Length = piece.Length - bytesKeptInHead,
						 .DocumentStart = offset};
		m_Pieces[pieceIndex].Length = bytesKeptInHead;
		m_Pieces.insert(m_Pieces.begin() + static_cast<std::ptrdiff_t>(pieceIndex) + 1, tail);

		return pieceIndex + 1;
	}

	std::size_t TextBuffer::InsertWithoutHistory(std::size_t offset, std::string_view text)
	{
		if (text.empty())
		{
			return m_AppendBuffer.size();
		}

		const std::size_t appendOffset = m_AppendBuffer.size();
		m_AppendBuffer.append(text);

		InsertAppendedPiece(offset, appendOffset, text);

		return appendOffset;
	}

	void TextBuffer::InsertAppendedPiece(std::size_t documentOffset, std::size_t appendOffset, std::string_view text)
	{
		const std::size_t clampedOffset = std::min(documentOffset, m_Length);
		const std::size_t firstLine = ToPosition(clampedOffset).Line;
		const std::size_t insertedPieceIndex = SplitPieceAt(clampedOffset);

		m_Pieces.insert(m_Pieces.begin() + static_cast<std::ptrdiff_t>(insertedPieceIndex),
						Piece{.Origin = Source::Append, .SourceOffset = appendOffset, .Length = text.size(), .DocumentStart = clampedOffset});

		RefreshDocumentStartsFrom(insertedPieceIndex);

		PatchLineIndexForInsert(clampedOffset, text);

		CORE_ASSERT(LineIndexMatchesRebuild(), "The patched line index no longer matches a rebuilt one");

		RecordChange(TextChange{.Offset = clampedOffset,
								.InsertedLength = text.size(),
								.FirstLine = firstLine,
								.InsertedLines = static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')),
								.LineCountAfter = GetLineCount()});
	}

	std::size_t TextBuffer::IndexOfFirstPieceStartingAtOrAfter(std::size_t offset) const
	{
		const auto pieceStartsBeforeSearchedOffset = [](const Piece& piece, std::size_t searchedOffset) { return piece.DocumentStart < searchedOffset; };
		const std::vector<Piece>::const_iterator firstPieceStartingAtOrAfter = std::lower_bound(m_Pieces.begin(), m_Pieces.end(), offset, pieceStartsBeforeSearchedOffset);

		return static_cast<std::size_t>(firstPieceStartingAtOrAfter - m_Pieces.begin());
	}

	void TextBuffer::EraseWithoutHistory(std::size_t from, std::size_t to)
	{
		const std::size_t clampedEnd = std::min(to, m_Length);

		if (from >= clampedEnd)
		{
			return;
		}

		const std::size_t firstLine = ToPosition(from).Line;
		const std::size_t lastLine = ToPosition(clampedEnd).Line;

		// Look both indices up after both splits rather than keeping what the splits return: splitting
		// at `from` inserts a piece ahead of the boundary at `clampedEnd` and moves it one index on.
		SplitPieceAt(clampedEnd);
		SplitPieceAt(from);

		const std::size_t firstErasedPiece = IndexOfFirstPieceStartingAtOrAfter(from);
		const std::size_t firstKeptPieceAfterErasure = IndexOfFirstPieceStartingAtOrAfter(clampedEnd);

		CORE_ASSERT(firstErasedPiece <= firstKeptPieceAfterErasure, "Erase found its bounds out of order");

		m_Pieces.erase(m_Pieces.begin() + static_cast<std::ptrdiff_t>(firstErasedPiece),
					   m_Pieces.begin() + static_cast<std::ptrdiff_t>(firstKeptPieceAfterErasure));

		RefreshDocumentStartsFrom(firstErasedPiece);

		PatchLineIndexForErase(from, clampedEnd);

		CORE_ASSERT(LineIndexMatchesRebuild(), "The patched line index no longer matches a rebuilt one");

		RecordChange(TextChange{.Offset = from,
								.RemovedLength = clampedEnd - from,
								.FirstLine = firstLine,
								.RemovedLines = lastLine - firstLine,
								.LineCountAfter = GetLineCount()});
	}

	std::string TextBuffer::Substring(std::size_t from, std::size_t to) const
	{
		const std::size_t clampedEnd = std::min(to, m_Length);

		if (from >= clampedEnd)
		{
			return {};
		}

		std::string substring;
		substring.reserve(clampedEnd - from);

		for (std::size_t pieceIndex = IndexOfPieceCovering(from); pieceIndex < m_Pieces.size(); ++pieceIndex)
		{
			const Piece& piece = m_Pieces[pieceIndex];

			if (piece.DocumentStart >= clampedEnd)
			{
				break;
			}

			const std::size_t sliceStartInPiece = from > piece.DocumentStart ? from - piece.DocumentStart : 0;
			const std::size_t sliceEndInPiece = std::min(piece.Length, clampedEnd - piece.DocumentStart);

			substring.append(SourceBuffer(piece.Origin), piece.SourceOffset + sliceStartInPiece,
							 sliceEndInPiece - sliceStartInPiece);
		}

		return substring;
	}

	char TextBuffer::CharAt(std::size_t offset) const
	{
		if (offset >= m_Length)
		{
			return '\0';
		}

		const Piece& piece = m_Pieces[IndexOfPieceCovering(offset)];

		return SourceBuffer(piece.Origin)[piece.SourceOffset + (offset - piece.DocumentStart)];
	}

	std::vector<std::size_t> TextBuffer::BuildLineIndex() const
	{
		std::vector<std::size_t> lineStarts{0};
		const std::string content = Materialize();

		for (std::size_t offset = 0; offset < content.size(); ++offset)
		{
			if (content[offset] == '\n')
			{
				lineStarts.push_back(offset + kNewlineLength);
			}
		}

		return lineStarts;
	}

	bool TextBuffer::LineIndexMatchesRebuild() const
	{
		return m_LineStarts == BuildLineIndex();
	}

	std::size_t TextBuffer::LineStart(std::size_t line) const
	{
		if (line >= m_LineStarts.size())
		{
			return m_Length;
		}

		return m_LineStarts[line];
	}

	std::size_t TextBuffer::LineLength(std::size_t line) const
	{
		if (line >= m_LineStarts.size())
		{
			return 0;
		}

		const bool isLastLine = line + 1 == m_LineStarts.size();

		if (isLastLine)
		{
			return m_Length - m_LineStarts[line];
		}

		const std::size_t newlineEndingLine = m_LineStarts[line + 1] - kNewlineLength;

		return newlineEndingLine - m_LineStarts[line];
	}

	std::string TextBuffer::LineText(std::size_t line) const
	{
		const std::size_t lineStart = LineStart(line);

		return Substring(lineStart, lineStart + LineLength(line));
	}

	std::size_t TextBuffer::ToOffset(Position position) const
	{
		if (position.Line >= m_LineStarts.size())
		{
			return m_Length;
		}

		return m_LineStarts[position.Line] + std::min(position.Column, LineLength(position.Line));
	}

	Position TextBuffer::ToPosition(std::size_t offset) const
	{
		const std::size_t clampedOffset = std::min(offset, m_Length);

		const std::vector<std::size_t>::const_iterator firstLineStartAfter = std::upper_bound(m_LineStarts.begin(), m_LineStarts.end(), clampedOffset);
		const std::size_t line = static_cast<std::size_t>(firstLineStartAfter - m_LineStarts.begin()) - 1;

		return Position{.Line = line, .Column = clampedOffset - m_LineStarts[line]};
	}

	// Patched rather than rebuilt so that an edit costs only the line starts after it, not a scan of
	// the whole document.
	void TextBuffer::PatchLineIndexForInsert(std::size_t offset, std::string_view text)
	{
		const std::vector<std::size_t>::iterator firstLineStartAfterInsertion = std::upper_bound(m_LineStarts.begin(), m_LineStarts.end(), offset);
		const std::size_t firstShiftedLine = static_cast<std::size_t>(firstLineStartAfterInsertion - m_LineStarts.begin());

		for (std::size_t line = firstShiftedLine; line < m_LineStarts.size(); ++line)
		{
			m_LineStarts[line] += text.size();
		}

		std::vector<std::size_t> insertedLineStarts;

		for (std::size_t byteInText = 0; byteInText < text.size(); ++byteInText)
		{
			if (text[byteInText] == '\n')
			{
				insertedLineStarts.push_back(offset + byteInText + kNewlineLength);
			}
		}

		m_LineStarts.insert(m_LineStarts.begin() + static_cast<std::ptrdiff_t>(firstShiftedLine),
							insertedLineStarts.begin(), insertedLineStarts.end());
	}

	void TextBuffer::PatchLineIndexForErase(std::size_t from, std::size_t to)
	{
		const std::size_t erasedLength = to - from;

		// A line start sits one byte after its '\n', so the erasure removes the starts after `from` up to
		// and including `to` and keeps a start at `from` itself: hence upper_bound at both ends.
		const std::vector<std::size_t>::iterator removedStartsBegin = std::upper_bound(m_LineStarts.begin(), m_LineStarts.end(), from);
		const std::vector<std::size_t>::iterator removedStartsEnd = std::upper_bound(m_LineStarts.begin(), m_LineStarts.end(), to);

		const std::size_t firstShiftedLine = static_cast<std::size_t>(removedStartsBegin - m_LineStarts.begin());
		m_LineStarts.erase(removedStartsBegin, removedStartsEnd);

		for (std::size_t line = firstShiftedLine; line < m_LineStarts.size(); ++line)
		{
			m_LineStarts[line] -= erasedLength;
		}
	}

	void TextBuffer::ApplyEdit(Edit& edit, EditDirection direction)
	{
		const bool recordedAsInsertion = edit.Kind == EditKind::Insertion;
		const bool inserting = direction == EditDirection::Reversed ? !recordedAsInsertion : recordedAsInsertion;

		if (inserting)
		{
			// m_AppendBuffer is only ever appended to, so the range an edit appended once stays valid for
			// every later replay of it and appending the text again would only grow the buffer.
			if (!edit.HasAppendOffset)
			{
				edit.AppendOffset = m_AppendBuffer.size();
				m_AppendBuffer.append(edit.Text);
				edit.HasAppendOffset = true;
			}

			InsertAppendedPiece(edit.DocumentOffset, edit.AppendOffset, edit.Text);
		}
		else
		{
			EraseWithoutHistory(edit.DocumentOffset, edit.DocumentOffset + edit.Text.size());
		}
	}

	void TextBuffer::BeginTransaction()
	{
		const bool opensOutermostTransaction = m_TransactionDepth == 0;

		if (opensOutermostTransaction)
		{
			m_TransactionGroupId = kNoGroupId;
		}

		++m_TransactionDepth;
	}

	void TextBuffer::EndTransaction()
	{
		if (m_TransactionDepth > 0)
		{
			--m_TransactionDepth;
		}
	}

	bool TextBuffer::ContinuesLastGroup(std::size_t from, std::size_t to, EditKind kind) const
	{
		if (m_Undo.empty())
		{
			return false;
		}

		if (kind == EditKind::Insertion)
		{
			const bool continuesTyping = from == m_LastEditEnd;

			return m_LastEditKind == EditKind::Insertion && continuesTyping;
		}

		const bool continuesBackspacing = to == m_LastEditStart;
		const bool continuesDeletingForward = from == m_LastEditStart;
		const bool continuesDeleting = continuesBackspacing || continuesDeletingForward;

		return m_LastEditKind == EditKind::Deletion && continuesDeleting;
	}

	std::size_t TextBuffer::ChooseGroupForNewEdit(std::size_t from, std::size_t to,
												  std::string_view text, EditKind kind)
	{
		const bool containsNewline = text.find('\n') != std::string_view::npos;
		const bool insideTransaction = m_TransactionDepth > 0;
		const bool joinsOpenTransactionGroup = insideTransaction && m_TransactionGroupId != kNoGroupId;

		std::size_t groupId = kNoGroupId;

		if (joinsOpenTransactionGroup)
		{
			groupId = m_TransactionGroupId;
		}
		else
		{
			const bool mayJoinLastGroup = !m_NextEditStartsGroup && !containsNewline;
			const bool continuesLastGroup = mayJoinLastGroup && ContinuesLastGroup(from, to, kind);

			if (continuesLastGroup)
			{
				groupId = m_Undo.back().GroupId;
			}
			else
			{
				groupId = m_NextGroupId;
				++m_NextGroupId;
			}

			if (insideTransaction)
			{
				m_TransactionGroupId = groupId;
			}
		}

		m_NextEditStartsGroup = containsNewline;

		return groupId;
	}

	void TextBuffer::RememberLastEdit(EditKind kind, std::size_t from, std::string_view text)
	{
		m_LastEditKind = kind;
		m_LastEditEnd = kind == EditKind::Insertion ? from + text.size() : from;
		m_LastEditStart = from;
	}

	void TextBuffer::Insert(std::size_t offset, std::string_view text)
	{
		if (text.empty())
		{
			return;
		}

		const std::size_t clampedOffset = std::min(offset, m_Length);
		const std::size_t appendOffset = InsertWithoutHistory(clampedOffset, text);

		m_Redo.clear();

		const std::size_t editId = m_NextEditId++;
		const std::size_t groupId = ChooseGroupForNewEdit(clampedOffset, clampedOffset, text, EditKind::Insertion);
		RememberLastEdit(EditKind::Insertion, clampedOffset, text);

		m_Undo.push_back(Edit{.Id = editId,
							  .DocumentOffset = clampedOffset,
							  .Text = std::string{text},
							  .Kind = EditKind::Insertion,
							  .AppendOffset = appendOffset,
							  .HasAppendOffset = true,
							  .GroupId = groupId});
	}

	void TextBuffer::Erase(std::size_t from, std::size_t to)
	{
		const std::size_t clampedEnd = std::min(to, m_Length);

		if (from >= clampedEnd)
		{
			return;
		}

		std::string erasedText = Substring(from, clampedEnd);

		EraseWithoutHistory(from, clampedEnd);

		const std::size_t groupId = ChooseGroupForNewEdit(from, clampedEnd, erasedText, EditKind::Deletion);
		RememberLastEdit(EditKind::Deletion, from, erasedText);

		m_Redo.clear();
		m_Undo.push_back(Edit{.Id = m_NextEditId++,
							  .DocumentOffset = from,
							  .Text = std::move(erasedText),
							  .Kind = EditKind::Deletion,
							  .AppendOffset = 0,
							  .HasAppendOffset = false,
							  .GroupId = groupId});
	}

	std::optional<TextBuffer::CaretOffset> TextBuffer::Undo()
	{
		if (m_Undo.empty())
		{
			return std::nullopt;
		}

		const std::size_t undoneGroupId = m_Undo.back().GroupId;
		CaretOffset caretOffset = 0;

		while (!m_Undo.empty() && m_Undo.back().GroupId == undoneGroupId)
		{
			Edit edit = std::move(m_Undo.back());
			m_Undo.pop_back();

			ApplyEdit(edit, EditDirection::Reversed);

			const std::size_t startOfRemovedInsertion = edit.DocumentOffset;
			const std::size_t endOfRestoredDeletion = edit.DocumentOffset + edit.Text.size();
			caretOffset = edit.Kind == EditKind::Insertion ? startOfRemovedInsertion : endOfRestoredDeletion;

			m_Redo.push_back(std::move(edit));
		}

		m_NextEditStartsGroup = true;
		m_LastEditKind = std::nullopt;

		return caretOffset;
	}

	std::optional<TextBuffer::CaretOffset> TextBuffer::Redo()
	{
		if (m_Redo.empty())
		{
			return std::nullopt;
		}

		const std::size_t redoneGroupId = m_Redo.back().GroupId;
		CaretOffset caretOffset = 0;

		while (!m_Redo.empty() && m_Redo.back().GroupId == redoneGroupId)
		{
			Edit edit = std::move(m_Redo.back());
			m_Redo.pop_back();

			ApplyEdit(edit, EditDirection::AsRecorded);

			const std::size_t endOfReinsertedText = edit.DocumentOffset + edit.Text.size();
			const std::size_t startOfErasedText = edit.DocumentOffset;
			caretOffset = edit.Kind == EditKind::Insertion ? endOfReinsertedText : startOfErasedText;

			m_Undo.push_back(std::move(edit));
		}

		m_NextEditStartsGroup = true;
		m_LastEditKind = std::nullopt;

		return caretOffset;
	}

	std::size_t TextBuffer::GetRevision() const
	{
		return m_Undo.empty() ? 0 : m_Undo.back().Id;
	}

	void TextBuffer::RecordChange(TextChange change)
	{
		if (!m_RecordsChanges)
		{
			return;
		}

		change.Sequence = ++m_ChangeSequence;
		m_Changes.push_back(change);

		// Trimmed by half a log at a time, so a long session pays for the erase once per
		// kChangeLogCapacity changes instead of once per change.
		if (m_Changes.size() >= 2 * kChangeLogCapacity)
		{
			m_Changes.erase(m_Changes.begin(), m_Changes.begin() + static_cast<std::ptrdiff_t>(kChangeLogCapacity));
		}
	}

	std::optional<std::span<const TextChange>> TextBuffer::ChangesSince(std::uint64_t sequence) const
	{
		if (sequence >= m_ChangeSequence)
		{
			return std::span<const TextChange>{};
		}

		if (m_Changes.empty() || sequence + 1 < m_Changes.front().Sequence)
		{
			return std::nullopt;
		}

		const std::size_t firstIndex = static_cast<std::size_t>(sequence + 1 - m_Changes.front().Sequence);

		return std::span<const TextChange>{m_Changes}.subspan(firstIndex);
	}

	void TextBuffer::ReplaceContent(std::string_view text)
	{
		const std::size_t lengthBefore = m_Length;
		const std::size_t lineCountBefore = GetLineCount();

		m_RecordsChanges = false;

		// The undo group is broken on both sides, so one Ctrl+Z reverses exactly the replacement
		// and none of the typing around it.
		BreakUndoGroup();
		BeginTransaction();
		Erase(0, m_Length);
		Insert(0, text);
		EndTransaction();
		BreakUndoGroup();

		m_RecordsChanges = true;

		RecordChange(TextChange{.Offset = 0,
								.RemovedLength = lengthBefore,
								.InsertedLength = text.size(),
								.FirstLine = 0,
								.RemovedLines = lineCountBefore - 1,
								.InsertedLines = GetLineCount() - 1,
								.LineCountAfter = GetLineCount(),
								.WholeContent = true});
	}

	std::optional<std::size_t> TextBuffer::GetTopUndoGroup() const
	{
		return m_Undo.empty() ? std::nullopt : std::optional<std::size_t>{m_Undo.back().GroupId};
	}

	std::optional<std::size_t> TextBuffer::GetTopRedoGroup() const
	{
		return m_Redo.empty() ? std::nullopt : std::optional<std::size_t>{m_Redo.back().GroupId};
	}

	bool TextBuffer::HoldsUndoGroup(std::size_t groupId) const
	{
		return std::ranges::any_of(m_Undo, [groupId](const Edit& edit) { return edit.GroupId == groupId; });
	}

	bool TextBuffer::HoldsRedoGroup(std::size_t groupId) const
	{
		return std::ranges::any_of(m_Redo, [groupId](const Edit& edit) { return edit.GroupId == groupId; });
	}
}
