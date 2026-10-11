#include "Editor/StatusBar/StatusBar.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <IconsMaterialDesignIcons.h>

#include <string>

namespace Graphite {

	namespace {
		const char* LineEndingName(Core::LineEnding ending, bool mixed)
		{
			if (ending == Core::LineEnding::Crlf)
			{
				return mixed ? "CRLF (mixed)" : "CRLF";
			}

			return mixed ? "LF (mixed)" : "LF";
		}

		std::string RelativePath(const std::filesystem::path& path,
								 const std::filesystem::path& root)
		{
			const auto relative = Core::RelativeTo(path, root);

			return Core::ToUtf8(relative ? *relative : path);
		}

		float CentredTextY(float barHeight)
		{
			// The bar is one frame tall — FontSize + 2 * FramePadding.y — and a line of text
			// is FontSize tall, both measured from the window's own top edge.
			return (barHeight - ImGui::GetTextLineHeight()) * 0.5f;
		}

		constexpr float kDotShareOfFont = 0.18f;

		float BellWidth()
		{
			return ImGui::CalcTextSize(ICON_MDI_BELL_OUTLINE).x;
		}

		ImU32 ToU32(const Core::Color& colour)
		{
			return ImGui::GetColorU32(ImVec4(colour.R, colour.G, colour.B, colour.A));
		}

		// A button rather than a text item, for the reason DrawClickablePiece gives.
		bool DrawNotificationBell(const std::optional<Core::Color>& missedColour, float barHeight)
		{
			const ImVec2 glyphSize = ImGui::CalcTextSize(ICON_MDI_BELL_OUTLINE);
			ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - glyphSize.x - ImGui::GetStyle().WindowPadding.x,
									   CentredTextY(barHeight)));

			// Without NoFocus the press focuses the bar, and the popup the click opens hands the
			// keyboard back to the bar when it closes rather than to the editor.
			const ImGuiButtonFlags keepFocusWhereItIs = ImGuiButtonFlags_NoFocus | ImGuiButtonFlags_NoNavFocus;
			const bool clicked = ImGui::InvisibleButton("##NotificationBell", glyphSize, keepFocusWhereItIs);
			const bool hovered = ImGui::IsItemHovered();
			const ImGuiCol glyphColour = hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled;
			ImDrawList* const draw = ImGui::GetWindowDrawList();
			draw->AddText(ImGui::GetItemRectMin(), ImGui::GetColorU32(glyphColour), ICON_MDI_BELL_OUTLINE);

			if (missedColour.has_value())
			{
				const float dotRadius = ImGui::GetFontSize() * kDotShareOfFont;
				const ImVec2 dotCentre(ImGui::GetItemRectMax().x - dotRadius, ImGui::GetItemRectMin().y + dotRadius);
				draw->AddCircleFilled(dotCentre, dotRadius, ToU32(*missedColour));
			}

			if (hovered)
			{
				ImGui::SetTooltip("Notifications");
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			}

			return clicked;
		}

		void DrawCentred(const std::string& text, float barHeight)
		{
			const float width = ImGui::CalcTextSize(text.c_str()).x;

			ImGui::SetCursorPos(
				ImVec2((ImGui::GetWindowWidth() - width) * 0.5f, CentredTextY(barHeight)));
			ImGui::TextUnformatted(text.c_str());
		}

		// One piece of the bar that may take a click: a git piece, or the server's words. Clickable pieces show a hand and take a click; the
		// greyed "No repository" is inert.
		//
		// A clickable piece is an invisible button with the text drawn over it, not a text item
		// asked IsItemClicked. A text item has no id, so a press on it is a press on the bar's
		// empty space: at the end of that frame ImGui focuses the bar itself, as it does for any
		// click on a window's background. That lands after the panel revealed by the click took
		// focus in the same frame, and the dock node then selects the tab of whatever window has
		// focus - the bar - so the panel stayed buried. A button owns the press, so the bar is
		// never refocused, and it reports the click on release, after the press has settled.
		// Not a Selectable: its hover highlight would fill the bar's height around one word.
		bool DrawClickablePiece(const char* id, const std::string& text, bool clickable, const std::string& tooltip,
								bool dimmedWhenInert = true)
		{
			const ImVec2 size = ImGui::CalcTextSize(text.c_str());

			// InvisibleButton refuses a zero size, so an empty piece is drawn as inert text.
			if (!clickable || size.x <= 0.0f)
			{
				if (dimmedWhenInert)
				{
					ImGui::TextDisabled("%s", text.c_str());
				}
				else
				{
					ImGui::TextUnformatted(text.c_str());
				}

				if (ImGui::IsItemHovered() && !tooltip.empty())
				{
					ImGui::SetTooltip("%s", tooltip.c_str());
				}

				return false;
			}

			// Sized exactly as TextUnformatted sizes the same string, so the pieces after it
			// land where they did when this was a text item.
			const bool clicked = ImGui::InvisibleButton(id, size);

			ImGui::GetWindowDrawList()->AddText(ImGui::GetItemRectMin(),
												ImGui::GetColorU32(ImGuiCol_Text), text.c_str());

			if (!ImGui::IsItemHovered())
			{
				return clicked;
			}

			if (!tooltip.empty())
			{
				ImGui::SetTooltip("%s", tooltip.c_str());
			}

			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			return clicked;
		}

		// Laid out left to right from the bar's own left edge, each piece a separate item so
		// each has its own hover and click. Returns the piece clicked, if any.
		StatusBarClick DrawGitSegment(const GitStatusSegment& segment, float barHeight)
		{
			StatusBarClick clicked = StatusBarClick::None;

			ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, CentredTextY(barHeight)));

			if (DrawClickablePiece("##GitBranch", segment.Branch, segment.HasRepository, segment.BranchTooltip))
			{
				clicked = StatusBarClick::GitBranch;
			}

			if (!segment.Distance.empty())
			{
				ImGui::SameLine();
				ImGui::TextUnformatted(segment.Distance.c_str());

				if (ImGui::IsItemHovered() && !segment.DistanceTooltip.empty())
				{
					ImGui::SetTooltip("%s", segment.DistanceTooltip.c_str());
				}
			}

			if (!segment.Changes.empty())
			{
				ImGui::SameLine(0.0f, ImGui::GetFontSize());

				if (DrawClickablePiece("##GitChanges", segment.Changes, true, segment.ChangesTooltip))
				{
					clicked = StatusBarClick::GitChanges;
				}
			}

			return clicked;
		}

		std::string DescribeDocument(const Core::OpenItem& item, const std::filesystem::path& root)
		{
			std::string description = RelativePath(item.Path, root);

			if (item.Document->IsModified())
			{
				description = "* " + description;
			}

			if (item.Document->IsReadOnly())
			{
				description += "  (read-only: not valid text for its encoding)";
			}

			return description;
		}

		std::string DescribeCaret(const Core::OpenItem& item)
		{
			const Core::TextBuffer& buffer = item.Document->GetBuffer();
			const Core::Position where = buffer.ToPosition(item.Caret.Offset);
			const std::size_t column = Core::ColumnOfByte(buffer.LineText(where.Line), where.Column,
														  item.TabWidth);

			std::string description =
				"Ln " + std::to_string(where.Line + 1) + ", Col " + std::to_string(column + 1);

			const auto [from, to] = Core::SelectionRange(item.Caret);

			if (from != to)
			{
				description += " (" + std::to_string(to - from) + " selected)";
			}

			return description;
		}

		std::string DescribeTextFormat(const Core::OpenItem& item)
		{
			const std::string encoding{Core::EncodingName(item.Document->GetFileEncoding())};

			return encoding + "    " +
				   LineEndingName(item.Document->GetDocumentLineEnding(), item.Document->HasMixedLineEndings());
		}
	}

	StatusBarClick DrawStatusBar(const Core::DocumentSet& documents,
								 std::optional<std::size_t> shown,
								 const std::filesystem::path& root,
								 const StatusMessages& messages)
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float height = ImGui::GetFrameHeight();

		constexpr ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;

		if (!ImGui::BeginViewportSideBar(kStatusBarWindowName, viewport, ImGuiDir_Down, height, flags))
		{
			ImGui::End();
			return StatusBarClick::None;
		}

		const bool hasDocument = shown && documents.At(*shown).Document.has_value();

		StatusBarClick clicked = StatusBarClick::None;

		if (messages.Git)
		{
			clicked = DrawGitSegment(*messages.Git, height);
		}

		if (hasDocument)
		{
			// Beside the git segment when there is one, a font-size gap apart; at the bar's
			// left edge otherwise, where it has always been.
			if (messages.Git)
			{
				ImGui::SameLine(0.0f, ImGui::GetFontSize() * 2.0f);
			}

			ImGui::SetCursorPosY(CentredTextY(height));
			ImGui::TextUnformatted(DescribeDocument(documents.At(*shown), root).c_str());
		}

		const std::string separator = "    ";
		std::string beforeServer;
		std::string server;
		std::string afterServer;

		// Problems first, then the server, then the background pass, then the caret: see each field's own comment.
		if (messages.Problems && !messages.Problems->empty())
		{
			beforeServer = *messages.Problems;
		}

		// Outside the has-a-document branch deliberately: a server is started when the folder
		// opens, before anything has been opened from it, so the frames where its indexing most
		// wants reporting are exactly the ones with no document to hang the report off.
		if (messages.Server && !messages.Server->empty())
		{
			server = *messages.Server;
		}

		if (messages.Background && !messages.Background->empty())
		{
			afterServer = *messages.Background;
		}

		if (hasDocument)
		{
			const Core::OpenItem& item = documents.At(*shown);

			if (!afterServer.empty())
			{
				afterServer += separator;
			}

			afterServer += DescribeCaret(item) + separator + DescribeTextFormat(item);
		}

		if (!server.empty())
		{
			if (!beforeServer.empty())
			{
				beforeServer += separator;
			}

			if (!afterServer.empty())
			{
				afterServer = separator + afterServer;
			}
		}
		else if (!beforeServer.empty() && !afterServer.empty())
		{
			beforeServer += separator;
		}

		const float width = ImGui::CalcTextSize(beforeServer.c_str()).x + ImGui::CalcTextSize(server.c_str()).x +
							ImGui::CalcTextSize(afterServer.c_str()).x;

		if (width > 0.0f)
		{
			ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - width - ImGui::GetStyle().WindowPadding.x - BellWidth() - ImGui::GetFontSize(),
									   CentredTextY(height)));

			bool placed = false;

			if (!beforeServer.empty())
			{
				ImGui::TextUnformatted(beforeServer.c_str());
				placed = true;
			}

			if (!server.empty())
			{
				if (placed)
				{
					ImGui::SameLine(0.0f, 0.0f);
				}

				const std::string serverTooltip = messages.ServerDetail.value_or(std::string{});

				if (DrawClickablePiece("##ServerStatus", server, messages.ServerOpensSettings, serverTooltip, false))
				{
					clicked = StatusBarClick::Server;
				}

				placed = true;
			}

			if (!afterServer.empty())
			{
				if (placed)
				{
					ImGui::SameLine(0.0f, 0.0f);
				}

				ImGui::TextUnformatted(afterServer.c_str());

				if (ImGui::IsItemHovered() && messages.BackgroundDetail && !messages.BackgroundDetail->empty())
				{
					ImGui::SetTooltip("%s", messages.BackgroundDetail->c_str());
				}
			}
		}

		if (DrawNotificationBell(messages.MissedNotificationColour, height))
		{
			clicked = StatusBarClick::Notifications;
		}

		// A transient message outranks a running search: it answers something the user just did
		// and is gone in a few seconds, so losing it loses the only confirmation there was.
		// Search progress is regenerated every frame, already appears in the panel's own summary
		// line, and comes right back once the transient message clears.
		const std::optional<std::string>& centred = messages.Transient ? messages.Transient : messages.Search;

		if (centred && !centred->empty())
		{
			DrawCentred(*centred, height);
		}

		ImGui::End();

		return clicked;
	}
}