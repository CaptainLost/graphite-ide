#include "Editor/Hover/HoverState.h"

#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/ColourLines.h"

#include <string_view>
#include <utility>

namespace Graphite {

	void HoldHoverAnswer(HoverState& hover, const HoverQuestion& question,
						 const LanguageServices::Answer& answer)
	{
		if (answer.Failed)
		{
			hover.InfoFailedFor = question;
			return;
		}

		hover.InfoFailedFor.reset();
		hover.InfoFor = question;
		hover.Info = answer.Hover;
	}

	void HoldPreviewAnswer(HoverState& hover, const HoverQuestion& question,
						   const LanguageServices::Answer& answer,
						   const Core::DocumentSet& documents)
	{
		if (answer.Failed)
		{
			hover.PreviewFailedFor = question;
			return;
		}

		hover.PreviewFailedFor.reset();
		hover.PreviewFor = question;
		hover.PreviewColours.reset();

		if (answer.Locations.empty())
		{
			DefinitionPreview none;
			none.Unreadable = true;
			hover.Preview = std::move(none);
			return;
		}

		// The path as the server spelt it. DocumentSet::Find compares paths exactly, so a
		// definition in an open document is read from its buffer only when the spellings agree —
		// the same terms F12's own preview of a definition is shown on.
		const Location& where = answer.Locations.front();
		hover.Preview = LoadDefinitionPreview(documents, where.Path, where.Line);
	}

	const ColouredPreview& ColouredPreviewOf(HoverState& hover, std::size_t tabWidth)
	{
		const bool current = hover.PreviewColours.has_value() && hover.PreviewColours->TabWidth == tabWidth;

		if (!current)
		{
			ColouredPreview coloured{.TabWidth = tabWidth, .Dedented = DedentPreview(hover.Preview->Lines, tabWidth)};
			const std::vector<std::string_view> lines(coloured.Dedented.Lines.begin(), coloured.Dedented.Lines.end());
			coloured.Tokens = ColourLines(LanguageOfPath(hover.Preview->Path), lines);
			hover.PreviewColours = std::move(coloured);
		}

		return *hover.PreviewColours;
	}
}
