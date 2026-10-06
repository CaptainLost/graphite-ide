#include "Notifications/NotificationsPopup.h"

#include "Notifications/ClockTime.h"
#include "UI/IconButton.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/ToastStyle.h"
#include "Core/ImGui/ToastText.h"
#include "Core/ImGui/UiScale.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <format>
#include <string>
#include <vector>

namespace Graphite {

	namespace {
		constexpr const char* kPopupId = "##notifications";
		constexpr std::size_t kCollapsedLines = 4;
		constexpr float kHighestShareOfWindow = 0.6f;
		constexpr float kIconGap = 12.0f;
		constexpr float kRowPadding = 6.0f;
		constexpr float kRowRounding = 4.0f;

		struct EntryLayout
		{
			std::string Count;
			std::string Time;
			std::string FolderLine;
			std::vector<Core::ToastTextLine> Lines;
			std::size_t VisibleLines = 0;
			std::size_t HiddenLines = 0;
			float Height = 0.0f;
		};

		ImVec4 ToImVec4(const Core::Color& colour)
		{
			return ImVec4(colour.R, colour.G, colour.B, colour.A);
		}

		ImFont* FontFor(bool strong)
		{
			ImFont* const bold = Core::ImGuiRenderer::GetBoldFont();
			const bool boldLoaded = bold != nullptr;
			return strong && boldLoaded ? bold : ImGui::GetFont();
		}

		// CalcTextSize rounds each run up, so runs placed by its widths drift from the text.
		float MeasureUnrounded(std::string_view text, bool strong)
		{
			return FontFor(strong)->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x;
		}

		std::string HiddenLinesText(std::size_t hiddenLines)
		{
			return hiddenLines == 1 ? std::string("\u2026 1 more line") : std::format("\u2026 {} more lines", hiddenLines);
		}

		EntryLayout LayOutEntry(const NotificationEntry& entry, bool expanded, float textWidth,
								const std::filesystem::path& openFolder, const std::chrono::time_zone& zone)
		{
			EntryLayout layout;
			layout.Count = entry.Count > 1 ? std::format("(x{})", entry.Count) : std::string();
			layout.Time = FormatClockTime(entry.LastShownAt, zone);

			const bool aboutAnotherFolder = !entry.Folder.empty() && entry.Folder != openFolder;
			if (aboutAnotherFolder)
			{
				layout.FolderLine = Core::ToUtf8(entry.Folder);
			}

			layout.Lines = Core::LayoutToastText(entry.Message, textWidth, MeasureUnrounded);
			const bool collapsed = layout.Lines.size() > kCollapsedLines && !expanded;
			layout.VisibleLines = collapsed ? kCollapsedLines : layout.Lines.size();
			layout.HiddenLines = layout.Lines.size() - layout.VisibleLines;

			const std::size_t titleLines = 1;
			const std::size_t folderLines = layout.FolderLine.empty() ? 0 : 1;
			const std::size_t moreLines = layout.HiddenLines > 0 ? 1 : 0;
			const std::size_t textLines = titleLines + folderLines + layout.VisibleLines + moreLines;

			const float textHeight = static_cast<float>(textLines) * ImGui::GetFontSize();
			layout.Height = std::max(textHeight, Core::ToastIconDiameter()) + 2.0f * Core::Scaled(kRowPadding);
			return layout;
		}

		void DrawEntry(const NotificationEntry& entry, const EntryLayout& layout, const Core::ToastPalette& palette, float rowWidth)
		{
			ImDrawList* const draw = ImGui::GetWindowDrawList();
			const ImVec2 rowMin = ImGui::GetItemRectMin();
			const ImVec2 rowMax = ImGui::GetItemRectMax();
			const float lineHeight = ImGui::GetFontSize();
			const float iconDiameter = Core::ToastIconDiameter();
			const float spacing = ImGui::GetStyle().ItemSpacing.x;

			if (ImGui::IsItemHovered())
			{
				draw->AddRectFilled(rowMin, rowMax, ImGui::GetColorU32(ImGuiCol_HeaderHovered), Core::Scaled(kRowRounding));
			}

			const float firstLineY = rowMin.y + Core::Scaled(kRowPadding);
			const ImVec2 iconCentre(rowMin.x + iconDiameter * 0.5f, firstLineY + lineHeight * 0.5f);
			Core::DrawToastIcon(*draw, entry.Kind, iconCentre, palette);

			const float textX = rowMin.x + iconDiameter + Core::Scaled(kIconGap);
			const ImU32 textColour = ImGui::GetColorU32(ToImVec4(palette.Text));
			const ImU32 dimColour = ImGui::GetColorU32(ToImVec4(palette.TextDim));
			const ImU32 messageColour = ImGui::GetColorU32(ToImVec4(Core::ToastMessageColour(palette)));

			const float timeX = rowMin.x + rowWidth - MeasureUnrounded(layout.Time, false);
			draw->AddText(FontFor(false), lineHeight, ImVec2(timeX, firstLineY), dimColour, layout.Time.c_str());

			const ImVec4 headingClip(textX, firstLineY, timeX - spacing, firstLineY + lineHeight);
			draw->AddText(FontFor(true), lineHeight, ImVec2(textX, firstLineY), textColour, entry.Title.c_str(), nullptr, 0.0f, &headingClip);

			if (!layout.Count.empty())
			{
				const float countX = textX + MeasureUnrounded(entry.Title, true) + spacing;
				draw->AddText(FontFor(false), lineHeight, ImVec2(countX, firstLineY), dimColour, layout.Count.c_str(), nullptr, 0.0f, &headingClip);
			}

			float lineY = firstLineY + lineHeight;
			const ImVec4 textClip(textX, rowMin.y, rowMax.x, rowMax.y);

			if (!layout.FolderLine.empty())
			{
				draw->AddText(FontFor(false), lineHeight, ImVec2(textX, lineY), dimColour, layout.FolderLine.c_str(), nullptr, 0.0f, &textClip);
				lineY += lineHeight;
			}

			for (std::size_t lineIndex = 0; lineIndex < layout.VisibleLines; ++lineIndex)
			{
				for (const Core::ToastTextRun& run : layout.Lines[lineIndex].Runs)
				{
					const bool strong = run.Style == Core::ToastTextStyle::Strong;
					draw->AddText(FontFor(strong), lineHeight, ImVec2(textX + run.X, lineY), strong ? textColour : messageColour,
								  run.Text.data(), run.Text.data() + run.Text.size());
				}

				lineY += lineHeight;
			}

			if (layout.HiddenLines > 0)
			{
				const std::string more = HiddenLinesText(layout.HiddenLines);
				draw->AddText(FontFor(false), lineHeight, ImVec2(textX, lineY), dimColour, more.c_str());
			}
		}
	}

	void NotificationsPopup::OpenFromBell()
	{
		m_OpenRequested = true;
	}

	void NotificationsPopup::Render(NotificationHistory& history, const Core::ToastPalette& palette,
									const std::filesystem::path& openFolder, const std::chrono::time_zone& zone)
	{
		if (m_OpenRequested)
		{
			ImGui::OpenPopup(kPopupId);
			m_OpenRequested = false;
		}

		const ImGuiViewport* const viewport = ImGui::GetMainViewport();
		const float margin = Core::ToastMargin();
		const ImVec2 bottomRight(viewport->WorkPos.x + viewport->WorkSize.x - margin,
								 viewport->WorkPos.y + viewport->WorkSize.y - margin);

		ImGui::SetNextWindowPos(bottomRight, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
		ImGui::SetNextWindowSize(ImVec2(Core::ToastCardWidth(), 0.0f), ImGuiCond_Always);
		ImGui::SetNextWindowViewport(viewport->ID);

		if (!ImGui::BeginPopup(kPopupId, ImGuiWindowFlags_NoMove))
		{
			return;
		}

		history.MarkAllSeen();

		const bool entryMenuOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
		const bool escapeClosesThePopup = ImGui::IsKeyPressed(ImGuiKey_Escape) && !entryMenuOpen;
		if (escapeClosesThePopup)
		{
			ImGui::CloseCurrentPopup();
		}

		DrawHeader(history);
		ImGui::Separator();

		ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, viewport->WorkSize.y * kHighestShareOfWindow));
		if (ImGui::BeginChild("##entries", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY))
		{
			DrawEntries(history, palette, openFolder, zone);
		}
		ImGui::EndChild();

		ImGui::EndPopup();
	}

	void NotificationsPopup::DrawHeader(NotificationHistory& history)
	{
		const float lineStartX = ImGui::GetCursorPosX();
		const float lineWidth = ImGui::GetContentRegionAvail().x;

		ImFont* const bold = Core::ImGuiRenderer::GetBoldFont();
		ImGui::AlignTextToFramePadding();
		if (bold != nullptr)
		{
			ImGui::PushFont(bold, 0.0f);
		}
		ImGui::TextUnformatted("Notifications");
		if (bold != nullptr)
		{
			ImGui::PopFont();
		}

		const char* const filterLabel = "Show confirmations";
		const ImGuiStyle& style = ImGui::GetStyle();
		const float checkboxWidth = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(filterLabel).x;
		const float controlsWidth = checkboxWidth + style.ItemSpacing.x + IconButtonSize();

		ImGui::SameLine(lineStartX + lineWidth - controlsWidth);
		ImGui::Checkbox(filterLabel, &m_ShowConfirmations);
		ImGui::SameLine();

		if (DrawIconButton("##clear", ICON_MDI_DELETE_SWEEP_OUTLINE, false, "Clear"))
		{
			history.Clear();
			m_Expanded.clear();
		}
	}

	void NotificationsPopup::DrawEntries(const NotificationHistory& history, const Core::ToastPalette& palette,
										 const std::filesystem::path& openFolder, const std::chrono::time_zone& zone)
	{
		const std::vector<const NotificationEntry*> entries = history.Entries(m_ShowConfirmations);
		if (entries.empty())
		{
			ImGui::TextDisabled("No notifications");
			return;
		}

		const float rowWidth = ImGui::GetContentRegionAvail().x;
		const float textWidth = std::max(rowWidth - Core::ToastIconDiameter() - Core::Scaled(kIconGap), 1.0f);

		for (const NotificationEntry* entry : entries)
		{
			const std::string rowId = std::to_string(entry->ToastId);
			ImGui::PushID(rowId.c_str());

			const EntryLayout layout = LayOutEntry(*entry, IsExpanded(entry->ToastId), textWidth, openFolder, zone);
			const bool rowClicked = ImGui::InvisibleButton("##row", ImVec2(rowWidth, layout.Height));
			const bool canExpand = layout.Lines.size() > kCollapsedLines;
			if (rowClicked && canExpand)
			{
				ToggleExpanded(entry->ToastId);
			}

			DrawEntry(*entry, layout, palette, rowWidth);

			if (Core::BeginRightClickPopup("##entryMenu"))
			{
				if (ImGui::IsKeyPressed(ImGuiKey_Escape))
				{
					ImGui::CloseCurrentPopup();
				}

				if (ImGui::MenuItem("Copy"))
				{
					Copy(*entry);
				}

				ImGui::EndPopup();
			}

			ImGui::PopID();
		}
	}

	void NotificationsPopup::ToggleExpanded(std::uint64_t toastId)
	{
		const bool wasExpanded = m_Expanded.erase(toastId) > 0;
		if (!wasExpanded)
		{
			m_Expanded.insert(toastId);
		}
	}

	void NotificationsPopup::Copy(const NotificationEntry& entry)
	{
		const std::string copied = CopiedText(entry);
		if (OnCopied)
		{
			OnCopied(copied);
			return;
		}

		ImGui::SetClipboardText(copied.c_str());
	}
}
