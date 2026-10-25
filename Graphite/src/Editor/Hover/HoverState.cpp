#include "Editor/Hover/HoverState.h"

#include "Language/Registry/LanguageRegistry.h"
#include "Language/Server/ServerWaitingText.h"
#include "Language/Syntax/ColourLines.h"
#include "UI/TypographicMarks.h"

#include <string_view>
#include <utility>

namespace Graphite {

	std::string HoverFailureText(const LanguageServices::Answer& answer)
	{
		const bool serverCouldNotBeAsked = answer.FailedBecause == ReplyFailure::ServerUnavailable;

		if (!answer.Failed || !serverCouldNotBeAsked || HasNoServerToWaitFor(answer.ServerAtFailure))
		{
			return {};
		}

		return AsSentence(ReplyFailureText(answer), answer.ServerAtFailure);
	}

	std::string HoverLoadingText(const ServerStatus& status)
	{
		if (AvailabilityOf(status) == ServerAvailability::ComingUp)
		{
			return AsSentence(ServerWaitingText(status), status);
		}

		return "Loading" + std::string{kEllipsis};
	}

	void HoldHoverAnswer(HoverState& hover, const HoverQuestion& question,
						 const LanguageServices::Answer& answer)
	{
		if (answer.Failed)
		{
			hover.InfoFailedFor = question;
			hover.InfoFailureText = HoverFailureText(answer);
			return;
		}

		hover.InfoFailedFor.reset();
		hover.InfoFailureText.clear();
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
			hover.PreviewFailureText = HoverFailureText(answer);
			return;
		}

		hover.PreviewFailedFor.reset();
		hover.PreviewFailureText.clear();
		hover.PreviewFor = question;
		hover.PreviewColours.reset();
		hover.NoDefinitionFound = answer.Locations.empty();

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
