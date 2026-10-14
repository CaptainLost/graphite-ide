#include "Editor/Navigation/CandidateLine.h"

#include "Editor/Navigation/DefinitionPreview.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/ColourLines.h"

#include "Core/Text/Columns.h"

#include <string_view>
#include <utility>

namespace Graphite {

	CandidateLine LoadCandidateLine(const Core::DocumentSet& documents, const Location& location, std::size_t tabWidth)
	{
		const DefinitionPreview preview = LoadDefinitionPreview(documents, location.Path, location.Line);

		if (preview.Lines.empty())
		{
			return {};
		}

		std::vector<std::string> expandedLines;

		for (const std::string& line : preview.Lines)
		{
			expandedLines.push_back(Core::ExpandTabs(line, tabWidth));
		}

		const std::vector<std::string_view> expandedLineViews(expandedLines.begin(), expandedLines.end());
		std::vector<std::vector<Token>> tokensPerLine = ColourLines(LanguageOfPath(location.Path), expandedLineViews);

		std::vector<Token> firstLineTokens;

		if (!tokensPerLine.empty())
		{
			firstLineTokens = std::move(tokensPerLine.front());
		}

		return CandidateLine{.Text = std::move(expandedLines.front()), .Tokens = std::move(firstLineTokens)};
	}
}
