#include "Editor/StatusBar/ClipboardCopy.h"

#include "UI/ToastEvent.h"

#include "Core/Events/EventBus.h"

#include <imgui.h>

namespace Graphite {

	namespace {
		constexpr std::size_t kShownBytes = 80;

		// The single-character ellipsis (U+2026) as the three bytes its UTF-8 encoding takes.
		constexpr std::string_view kEllipsis = "\xE2\x80\xA6";

		bool IsContinuationByte(char byte)
		{
			return (static_cast<unsigned char>(byte) & 0xC0) == 0x80;
		}
	}

	std::string CopiedSummary(std::string_view what, std::string_view text)
	{
		const std::size_t lineEnd = text.find_first_of("\r\n");
		std::string_view shown = text.substr(0, lineEnd);
		bool cut = lineEnd != std::string_view::npos;

		if (shown.size() > kShownBytes)
		{
			std::size_t end = kShownBytes;

			// Back to the start of a character: a split one is drawn as a replacement glyph.
			while (end > 0 && IsContinuationByte(shown[end]))
			{
				--end;
			}

			shown = shown.substr(0, end);
			cut = true;
		}

		std::string message(what);

		if (!shown.empty())
		{
			message += ": ";
			message += shown;

			if (cut)
			{
				message += kEllipsis;
			}
		}

		return message;
	}

	void CopyToClipboard(Core::EventBus& events, std::string_view what, const std::string& text)
	{
		ImGui::SetClipboardText(text.c_str());
		ReportCopied(events, what, text);
	}

	void ReportCopied(Core::EventBus& events, std::string_view what, std::string_view text)
	{
		events.Publish(ToastEvent{.Kind = Core::ToastKind::Success,
								  .Title = "Copied",
								  .Message = CopiedSummary(what, text),
								  .Scope = ToastScope::Application});
	}
}
