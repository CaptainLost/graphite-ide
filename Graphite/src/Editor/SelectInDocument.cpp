#include "Editor/SelectInDocument.h"

#include "Lang/Server/LspClient.h"

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>
#include <string>

namespace Graphite {

	namespace {

		// Opens the document and hands the caller the item and its buffer, or nothing when no
		// buffer exists to place a caret in.
		Core::OpenItem* OpenForSelection(Core::DocumentSet& documents,
										 const std::filesystem::path& path,
										 const OpenDocument& open)
		{
			open(path);

			const auto index = documents.Find(path);

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

			item.Caret = Core::PlaceAt(buffer, item.Caret, from, false, item.TabWidth);
			item.Caret = Core::PlaceAt(buffer, item.Caret, to, true, item.TabWidth);

			// The vertical aim was measured, if at all, against wherever the caret used to sit;
			// the next Up or Down should resolve it fresh from here.
			item.RowAimAt = Core::NoRowAim;

			// Runs during the panels' turn, before DocumentWindows renders the editor later in
			// the same frame, so by the editor's own reckoning the caret never moved this frame
			// and it would not otherwise scroll to it. See PendingScroll in DocumentSet.h.
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
