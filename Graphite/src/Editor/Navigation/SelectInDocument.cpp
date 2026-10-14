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

			return item.Document ? &item : nullptr;
		}

		void Select(Core::OpenItem& item, std::size_t from, std::size_t to)
		{
			Core::TextBuffer& buffer = item.Document->GetBuffer();

			item.Caret = Core::PlaceAt(buffer, item.Caret, from, Core::SelectionChange::Collapse, item.TabWidth);
			item.Caret = Core::PlaceAt(buffer, item.Caret, to, Core::SelectionChange::Extend, item.TabWidth);

			item.RowAimCaretOffset = Core::NoRowAim;

			item.PendingScroll = true;
		}
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

		const Core::TextBuffer& buffer = item->Document->GetBuffer();
		const std::size_t lineStart = buffer.LineStart(line);

		Select(*item, lineStart + from, lineStart + to);
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

		const Core::TextBuffer& buffer = item->Document->GetBuffer();
		const std::string lineText = buffer.LineText(line);
		const std::size_t lineStart = buffer.LineStart(line);

		const std::size_t from = std::min(FromUtf16Column(lineText, column), lineText.size());
		const std::size_t to =
			SpansOneLine(line, column, endLine, endColumn)
				? std::min(FromUtf16Column(lineText, endColumn), lineText.size())
				: from;

		Select(*item, lineStart + from, lineStart + to);
		return true;
	}
}
