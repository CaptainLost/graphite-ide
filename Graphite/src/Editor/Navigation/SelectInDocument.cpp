#include "Editor/Navigation/SelectInDocument.h"

#include "Language/Server/LspProtocol.h"

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>
#include <optional>
#include <string>

namespace Graphite {

	namespace {
		constexpr std::size_t kMostUtf8BytesPerUtf16Unit = 3;
		constexpr std::size_t kLongestUtf8Character = 4;

		// WARNING: a column can fall inside a surrogate pair, whose character starts before the column's bytes end, so the prefix holds one whole character more.
		std::size_t PrefixReachingColumn(std::size_t lineLength, Utf16Column column)
		{
			if (column.Value >= lineLength)
			{
				return lineLength;
			}

			return std::min(lineLength, column.Value * kMostUtf8BytesPerUtf16Unit + kLongestUtf8Character);
		}

		Core::OpenItem* OpenForSelection(Core::DocumentSet& documents,
										 const std::filesystem::path& path,
										 const OpenDocument& open)
		{
			open(path);

			const std::optional<std::size_t> index = documents.Find(path);

			if (!index)
			{
				return nullptr;
			}

			Core::OpenItem& item = documents.At(*index);
			const bool holdsOrAwaitsText = item.Document != nullptr || Core::IsLoading(item);

			return holdsOrAwaitsText ? &item : nullptr;
		}

		void Select(Core::OpenItem& item, std::size_t from, std::size_t to)
		{
			Core::TextBuffer& buffer = item.Document->GetBuffer();

			item.Caret = Core::PlaceAt(buffer, item.Caret, from, Core::SelectionChange::Collapse, item.TabWidth);
			item.Caret = Core::PlaceAt(buffer, item.Caret, to, Core::SelectionChange::Extend, item.TabWidth);

			item.RowAimCaretOffset = Core::NoRowAim;

			item.PendingScroll = true;
		}

		void SelectBytesOfLine(Core::OpenItem& item, std::size_t line, std::size_t from, std::size_t to)
		{
			const Core::TextBuffer& buffer = item.Document->GetBuffer();
			const std::size_t lineStart = buffer.LineStart(line);

			Select(item, lineStart + from, lineStart + to);
		}

		void SelectUtf16ColumnsOfLine(Core::OpenItem& item, std::size_t line, Utf16Column column, std::size_t endLine,
									  Utf16Column endColumn)
		{
			const Core::TextBuffer& buffer = item.Document->GetBuffer();
			const std::size_t lineStart = buffer.LineStart(line);

			const std::size_t from = ByteColumnOfUtf16Column(buffer, line, column);
			const std::size_t to = SpansOneLine(line, column, endLine, endColumn) ? ByteColumnOfUtf16Column(buffer, line, endColumn) : from;

			Select(item, lineStart + from, lineStart + to);
		}
	}

	std::size_t ByteColumnOfUtf16Column(const Core::TextBuffer& buffer, std::size_t line, Utf16Column column)
	{
		const std::size_t lineStart = buffer.LineStart(line);
		const std::size_t prefixLength = PrefixReachingColumn(buffer.LineLength(line), column);
		const std::string prefix = buffer.Substring(lineStart, lineStart + prefixLength);

		return std::min(FromUtf16Column(prefix, column), prefix.size());
	}

	bool SpansOneLine(std::size_t line, Utf16Column column, std::size_t endLine,
					  Utf16Column endColumn)
	{
		return endLine == line && endColumn.Value > column.Value;
	}

	bool SelectInDocument(Core::DocumentSet& documents, const std::filesystem::path& path,
						  std::size_t line, std::size_t from, std::size_t to,
						  const OpenDocument& open)
	{
		Core::OpenItem* item = OpenForSelection(documents, path, open);

		if (!item)
		{
			return false;
		}

		if (Core::IsLoading(*item))
		{
			item->CaretOnLoad = Core::CaretWhenLoaded{
				.Line = line,
				.Column = to,
				.Place = [line, from, to](Core::OpenItem& loaded) { SelectBytesOfLine(loaded, line, from, to); }};
			return true;
		}

		SelectBytesOfLine(*item, line, from, to);
		return true;
	}

	bool SelectInDocument(Core::DocumentSet& documents, const std::filesystem::path& path,
						  std::size_t line, Utf16Column column, std::size_t endLine,
						  Utf16Column endColumn, const OpenDocument& open)
	{
		Core::OpenItem* item = OpenForSelection(documents, path, open);

		if (!item)
		{
			return false;
		}

		if (Core::IsLoading(*item))
		{
			const std::size_t caretColumnInUtf16Units = SpansOneLine(line, column, endLine, endColumn) ? endColumn.Value : column.Value;

			item->CaretOnLoad = Core::CaretWhenLoaded{
				.Line = line,
				.Column = caretColumnInUtf16Units,
				.ColumnIn = Core::ColumnUnit::Utf16CodeUnit,
				.Place = [line, column, endLine, endColumn](Core::OpenItem& loaded) {
					SelectUtf16ColumnsOfLine(loaded, line, column, endLine, endColumn);
				}};
			return true;
		}

		SelectUtf16ColumnsOfLine(*item, line, column, endLine, endColumn);
		return true;
	}
}
