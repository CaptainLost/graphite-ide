#include "Editor/DefinitionNavigation.h"

#include "Lang/Server/LspClient.h"

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>

namespace Graphite {

	namespace {

		bool SameSpot(const Location& location, const FilePosition& from)
		{
			return location.Path == from.Path && location.Line == from.Line &&
				   location.Column == from.Column;
		}

		bool SameSpot(const Location& a, const Location& b)
		{
			return a.Path == b.Path && a.Line == b.Line && a.Column == b.Column;
		}
	}

	DefinitionOutcome ResolveDefinition(const std::vector<Location>& locations,
										const FilePosition& from,
										std::vector<Location>& candidates)
	{
		candidates.clear();

		for (const Location& location : locations)
		{
			// Where the caret already is offers nowhere new to go.
			if (SameSpot(location, from))
			{
				continue;
			}

			const bool alreadyHave =
				std::any_of(candidates.begin(), candidates.end(),
							[&location](const Location& existing)
							{ return SameSpot(existing, location); });

			if (!alreadyHave)
			{
				candidates.push_back(location);
			}
		}

		if (candidates.empty())
		{
			return DefinitionOutcome::None;
		}

		if (candidates.size() == 1)
		{
			return DefinitionOutcome::Jumped;
		}

		return DefinitionOutcome::NeedsChoice;
	}

	void Jump(Core::DocumentSet& documents, const Location& location)
	{
		documents.Open(location.Path, true);

		const auto index = documents.Find(location.Path);

		if (!index)
		{
			return;
		}

		Core::OpenItem& item = documents.At(*index);

		if (!item.Document)
		{
			// A large-file question is pending, or the read failed outright - either way there
			// is no buffer yet to place a caret in.
			return;
		}

		const Core::TextBuffer& buffer = item.Document->GetBuffer();

		// A location answered before an edit has since shortened the file is stale, but landing
		// on the last line beats reading past the end of it.
		const std::size_t line = std::min(location.Line, buffer.GetLineCount() - 1);
		const std::string lineText = buffer.LineText(line);
		const std::size_t byteColumn = FromUtf16Column(lineText, location.Column);
		const std::size_t offset = buffer.LineStart(line) + byteColumn;

		item.Caret = Core::PlaceAt(buffer, item.Caret, offset, false, item.TabWidth);
		item.PendingScroll = true;
	}
}
