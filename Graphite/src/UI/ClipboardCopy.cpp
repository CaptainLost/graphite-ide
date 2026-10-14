#include "UI/ClipboardCopy.h"

#include "UI/ToastEvent.h"
#include "UI/TypographicMarks.h"

#include "Core/Events/EventBus.h"
#include "Core/Text/Encoding.h"

#include <imgui.h>

namespace Graphite {

	namespace {
		constexpr std::size_t kShownBytes = 80;
	}

	std::string CopiedSummary(std::string_view copiedThing, std::string_view text)
	{
		const std::size_t lineEnd = text.find_first_of("\r\n");
		std::string_view shown = text.substr(0, lineEnd);
		bool wasCut = lineEnd != std::string_view::npos;

		if (shown.size() > kShownBytes)
		{
			const std::size_t shownEnd = Core::CharacterBoundaryAtOrBefore(shown, kShownBytes);

			shown = shown.substr(0, shownEnd);
			wasCut = true;
		}

		std::string message(copiedThing);

		if (!shown.empty())
		{
			message += ": ";
			message += shown;

			if (wasCut)
			{
				message += kEllipsis;
			}
		}

		return message;
	}

	void CopyToClipboard(Core::EventBus& events, std::string_view copiedThing, const std::string& text)
	{
		ImGui::SetClipboardText(text.c_str());
		ReportCopied(events, copiedThing, text);
	}

	void ReportCopied(Core::EventBus& events, std::string_view copiedThing, std::string_view text)
	{
		events.Publish(ToastEvent{.Kind = Core::ToastKind::Success,
								  .Title = "Copied",
								  .Message = CopiedSummary(copiedThing, text),
								  .Scope = ToastScope::Application});
	}
}
