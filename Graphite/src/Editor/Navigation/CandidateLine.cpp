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

		// The definition's whole extent, not its first line alone: a grammar colours a line by what surrounds it.
		std::vector<std::string> expanded;

		for (const std::string& line : preview.Lines)
		{
			expanded.push_back(Core::ExpandTabs(line, tabWidth));
		}

		const std::vector<std::string_view> lines(expanded.begin(), expanded.end());
		std::vector<std::vector<Token>> tokens = ColourLines(LanguageOfPath(location.Path), lines);

		return CandidateLine{.Text = std::move(expanded.front()),
							 .Tokens = tokens.empty() ? std::vector<Token>{} : std::move(tokens.front())};
	}
}
