#include "Editor/Hover/HoverState.h"

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
}
