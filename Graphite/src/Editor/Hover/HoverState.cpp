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
			DefinitionPreview unreadablePreview;
			unreadablePreview.Unreadable = true;
			hover.Preview = std::move(unreadablePreview);
			return;
		}

		// WARNING: The path stays as the server spelt it. DocumentSet::Find compares paths exactly,
		// so a definition in an open document is read from its buffer only when the spellings agree.
		const Location& definition = answer.Locations.front();
		hover.Preview = LoadDefinitionPreview(documents, definition.Path, definition.Line);
	}

	const ColouredPreview& ColouredPreviewOf(HoverState& hover, std::size_t tabWidth)
	{
		const bool colouredForTabWidth = hover.PreviewColours.has_value() && hover.PreviewColours->TabWidth == tabWidth;

		if (!colouredForTabWidth)
		{
			ColouredPreview coloured{.TabWidth = tabWidth, .Dedented = DedentPreview(hover.Preview->Lines, tabWidth)};
			const std::vector<std::string_view> lineViews(coloured.Dedented.Lines.begin(), coloured.Dedented.Lines.end());
			coloured.Tokens = ColourLines(LanguageOfPath(hover.Preview->Path), lineViews);
			hover.PreviewColours = std::move(coloured);
		}

		return *hover.PreviewColours;
	}
}
