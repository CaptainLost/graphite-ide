#include "Editor/StatusBar/StatusBar.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/IconButton.h"
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
			const std::optional<std::filesystem::path> relative = Core::RelativeTo(path, root);

			return Core::ToUtf8(relative ? *relative : path);
		}

		float CentredTextY(float barHeight)
		{
			return (barHeight - ImGui::GetTextLineHeight()) * 0.5f;
		}

		constexpr float kDotShareOfFont = 0.18f;
		constexpr float kGapAfterGitInFontSizes = 2.0f;
		constexpr float kNoBorder = 0.0f;

		float BellWidth()
		{
			return Core::IconButtonSize(Core::IconButtonFit::Line);
		}

		ImU32 ToPackedColour(const Core::Color& colour)
		{
			return ImGui::GetColorU32(ImVec4(colour.R, colour.G, colour.B, colour.A));
		}

		bool DrawNotificationBell(const std::optional<Core::Color>& missedColour, float barHeight)
		{
			ImGui::SetCursorPos(
				ImVec2(ImGui::GetWindowWidth() - BellWidth() - ImGui::GetStyle().WindowPadding.x, CentredTextY(barHeight)));

			// WARNING: Without NoFocus the press focuses the bar, and the popup the click opens hands the
			// keyboard back to the bar when it closes rather than to the editor.
			constexpr bool kNotEngaged = false;
			const Core::IconButtonOptions bellButton{.Fit = Core::IconButtonFit::Line,
													 .Flags = ImGuiButtonFlags_NoFocus | ImGuiButtonFlags_NoNavFocus};
			const bool clicked =
				Core::DrawIconButton("##NotificationBell", ICON_MDI_BELL_OUTLINE, kNotEngaged, "Notifications", bellButton);
			ImDrawList* const drawList = ImGui::GetWindowDrawList();

			if (missedColour.has_value())
			{
				const float dotRadius = ImGui::GetFontSize() * kDotShareOfFont;
				const ImVec2 dotCentre(ImGui::GetItemRectMax().x - dotRadius, ImGui::GetItemRectMin().y + dotRadius);
				drawList->AddCircleFilled(dotCentre, dotRadius, ToPackedColour(*missedColour));
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

		// WARNING: A clickable piece is an invisible button with the text drawn over it, never a text item
		// asked IsItemClicked. A text item has no id, so its press focuses the bar itself and the dock
		// node then selects the bar's tab instead of the panel the click revealed.
		bool DrawClickablePiece(const char* id, const std::string& text, bool clickable, const std::string& tooltip,
								bool dimmedWhenInert = true)
		{
			const ImVec2 size = ImGui::CalcTextSize(text.c_str());

			// WARNING: InvisibleButton refuses a zero size, so an empty piece is drawn as inert text.
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

				const bool changesClickable = true;

				if (DrawClickablePiece("##GitChanges", segment.Changes, changesClickable, segment.ChangesTooltip))
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

		constexpr ImGuiWindowFlags windowFlags =
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, kNoBorder);
		const bool barBegun = ImGui::BeginViewportSideBar(kStatusBarWindowName, viewport, ImGuiDir_Down, height, windowFlags);
		ImGui::PopStyleVar();

		if (!barBegun)
		{
			ImGui::End();
			return StatusBarClick::None;
		}

		const bool hasDocument = shown && documents.At(*shown).Document != nullptr;

		StatusBarClick clicked = StatusBarClick::None;

		if (messages.Git)
		{
			clicked = DrawGitSegment(*messages.Git, height);
		}

		if (hasDocument)
		{
			if (messages.Git)
			{
				ImGui::SameLine(0.0f, ImGui::GetFontSize() * kGapAfterGitInFontSizes);
			}

			ImGui::SetCursorPosY(CentredTextY(height));
			ImGui::TextUnformatted(DescribeDocument(documents.At(*shown), root).c_str());
		}

		const std::string separator = "    ";
		std::string beforeServer;
		std::string server;
		std::string afterServer;

		if (messages.Problems && !messages.Problems->empty())
		{
			beforeServer = *messages.Problems;
		}

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

			// WARNING: SameLine places relative to the item before it, so it needs one.
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

				const bool dimmedWhenInert = false;

				if (DrawClickablePiece("##ServerStatus", server, messages.ServerOpensSettings, serverTooltip, dimmedWhenInert))
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

		const std::optional<std::string>& centred = messages.Transient ? messages.Transient : messages.Search;

		if (centred && !centred->empty())
		{
			DrawCentred(*centred, height);
		}

		ImGui::End();

		return clicked;
	}
}