#include "Notifications/NotificationsPopup.h"

#include "Notifications/ClockTime.h"
#include "Notifications/FolderLine.h"
#include "UI/IconButton.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/ToastStyle.h"
#include "Core/ImGui/ToastText.h"
#include "Core/ImGui/UiScale.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <format>
#include <optional>
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
			const NotificationEntry* Entry = nullptr;
			std::string Count;
			std::string Time;
			std::string FolderLine;
			std::vector<Core::ToastTextLine> Lines;
			std::size_t VisibleLines = 0;
			std::size_t HiddenLines = 0;
			float Height = 0.0f;
		};

		struct ListLayout
		{
			std::vector<EntryLayout> Rows;
			float RowWidth = 0.0f;
			float Height = 0.0f;
		};

		// The toggle and the copy are carried out only once the list is drawn: its rows point into
		// the history, and a copy confirmation shown as a toast adds to the history.
		struct ListOutcome
		{
			std::optional<std::uint64_t> ToggledToastId;
			std::optional<std::string> TextToCopy;
			bool EntryMenuOpen = false;
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

		float MeasureRegularUnrounded(std::string_view text)
		{
			return MeasureUnrounded(text, false);
		}

		std::string HiddenLinesText(std::size_t hiddenLines)
		{
			return hiddenLines == 1 ? std::string("\u2026 1 more line") : std::format("\u2026 {} more lines", hiddenLines);
		}

		EntryLayout LayOutEntry(const NotificationEntry& entry, bool expanded, float textWidth,
								const std::filesystem::path& openFolder, const std::chrono::time_zone* zone)
		{
			EntryLayout layout;
			layout.Entry = &entry;
			layout.Count = entry.Count > 1 ? std::format("(x{})", entry.Count) : std::string();
			layout.Time = FormatClockTime(entry.LastShownAt, zone);

			const bool aboutAnotherFolder = !entry.Folder.empty() && entry.Folder != openFolder;
			if (aboutAnotherFolder)
			{
				layout.FolderLine = ElideFolderLine(Core::ToUtf8(entry.Folder), textWidth, MeasureRegularUnrounded);
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

		std::vector<EntryLayout> LayOutRows(const std::vector<const NotificationEntry*>& entries, const NotificationsPopup& popup,
											float rowWidth, const std::filesystem::path& openFolder, const std::chrono::time_zone* zone)
		{
			const float textWidth = std::max(rowWidth - Core::ToastIconDiameter() - Core::Scaled(kIconGap), 1.0f);

			std::vector<EntryLayout> rows;
			rows.reserve(entries.size());
			for (const NotificationEntry* entry : entries)
			{
				rows.push_back(LayOutEntry(*entry, popup.IsExpanded(entry->ToastId), textWidth, openFolder, zone));
			}

			return rows;
		}

		float StackedHeight(const std::vector<EntryLayout>& rows)
		{
			const float spacing = ImGui::GetStyle().ItemSpacing.y;

			float height = 0.0f;
			for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
			{
				const bool belowAnotherRow = rowIndex > 0;
				if (belowAnotherRow)
				{
					height += spacing;
				}

				height += rows[rowIndex].Height;
			}

			return height;
		}

		ListLayout LayOutList(const std::vector<const NotificationEntry*>& entries, const NotificationsPopup& popup, float listWidth,
							  float highestListHeight, const std::filesystem::path& openFolder, const std::chrono::time_zone* zone)
		{
			ListLayout list;
			if (entries.empty())
			{
				list.Height = ImGui::GetTextLineHeight();
				return list;
			}

			list.RowWidth = listWidth;
			list.Rows = LayOutRows(entries, popup, list.RowWidth, openFolder, zone);
			float rowsHeight = StackedHeight(list.Rows);

			// A list cut short scrolls, and ImGui takes its scrollbar's width from the rows.
			const bool scrolls = rowsHeight > highestListHeight;
			if (scrolls)
			{
				list.RowWidth = listWidth - ImGui::GetStyle().ScrollbarSize;
				list.Rows = LayOutRows(entries, popup, list.RowWidth, openFolder, zone);
				rowsHeight = StackedHeight(list.Rows);
			}

			list.Height = std::min(rowsHeight, highestListHeight);
			return list;
		}

		ListOutcome DrawList(const ListLayout& list, const Core::ToastPalette& palette)
		{
			ListOutcome outcome;
			if (list.Rows.empty())
			{
				ImGui::TextDisabled("No notifications");
				return outcome;
			}

			for (const EntryLayout& row : list.Rows)
			{
				const NotificationEntry& entry = *row.Entry;
				const std::string rowId = std::to_string(entry.ToastId);
				ImGui::PushID(rowId.c_str());

				const bool rowClicked = ImGui::InvisibleButton("##row", ImVec2(list.RowWidth, row.Height));
				const bool canExpand = row.Lines.size() > kCollapsedLines;
				if (rowClicked && canExpand)
				{
					outcome.ToggledToastId = entry.ToastId;
				}

				DrawEntry(entry, row, palette, list.RowWidth);

				if (Core::BeginRightClickPopup("##entryMenu"))
				{
					// Raised every frame as well: the list's popup raises itself every frame, ahead of
					// this menu, and would otherwise cover it from the menu's second frame.
					ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
					outcome.EntryMenuOpen = true;

					if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
					{
						ImGui::CloseCurrentPopup();
					}

					if (ImGui::MenuItem("Copy"))
					{
						outcome.TextToCopy = CopiedText(entry);
					}

					ImGui::EndPopup();
				}

				ImGui::PopID();
			}

			return outcome;
		}
	}

	void NotificationsPopup::OpenFromBell()
	{
		m_OpenRequested = true;
	}

	void NotificationsPopup::Render(NotificationHistory& history, const Core::ToastPalette& palette,
									const std::filesystem::path& openFolder, const std::chrono::time_zone* zone)
	{
		if (m_OpenRequested)
		{
			ImGui::OpenPopup(kPopupId);
			m_OpenRequested = false;
		}

		if (!ImGui::IsPopupOpen(kPopupId))
		{
			m_Open = false;
			m_EntryMenuOpenLastFrame = false;
			return;
		}

		const ImGuiViewport* const viewport = ImGui::GetMainViewport();
		const ImGuiStyle& style = ImGui::GetStyle();
		const float popupWidth = Core::ToastCardWidth();
		const float listWidth = popupWidth - 2.0f * style.WindowPadding.x;
		const float highestListHeight = viewport->WorkSize.y * kHighestShareOfWindow;
		const std::vector<const NotificationEntry*> entries = history.Entries(m_ShowConfirmations);
		const ListLayout list = LayOutList(entries, *this, listWidth, highestListHeight, openFolder, zone);

		// Sized from this frame's rows, not left to ImGui: it fits a popup and a child to what they
		// held the frame before, so the popup would open at a stale height and lag every expand.
		const float headerHeight = ImGui::GetFrameHeight();
		const float separatorHeight = std::max(style.SeparatorSize, 1.0f);
		const float popupHeight = style.WindowPadding.y + headerHeight + style.ItemSpacing.y + separatorHeight +
								  style.ItemSpacing.y + list.Height + style.WindowPadding.y;

		const float margin = Core::ToastMargin();
		const ImVec2 bottomRight(viewport->WorkPos.x + viewport->WorkSize.x - margin,
								 viewport->WorkPos.y + viewport->WorkSize.y - margin);

		ImGui::SetNextWindowPos(bottomRight, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
		ImGui::SetNextWindowSize(ImVec2(popupWidth, popupHeight), ImGuiCond_Always);
		ImGui::SetNextWindowViewport(viewport->ID);

		if (!ImGui::BeginPopup(kPopupId, ImGuiWindowFlags_NoMove))
		{
			m_Open = false;
			m_EntryMenuOpenLastFrame = false;
			return;
		}

		// ImGui raises a popup only on the frame it appears, and puts every window created after
		// that in front of it: a toast shown while the list is open would cover its bottom rows.
		ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
		m_Open = true;
		history.MarkAllSeen();

		// Under keyboard navigation ImGui closes the entry menu on Esc before this frame is drawn,
		// so without last frame's menu the key that shut it would close the popup as well.
		const bool escapeIsForTheMenu = m_EntryMenuOpenLastFrame || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
		const bool escapeClosesThePopup = ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !escapeIsForTheMenu;
		if (escapeClosesThePopup)
		{
			ImGui::CloseCurrentPopup();
		}

		const bool clearClicked = DrawHeader();
		ImGui::Separator();

		ListOutcome outcome;
		if (ImGui::BeginChild("##entries", ImVec2(0.0f, list.Height)))
		{
			outcome = DrawList(list, palette);
		}
		ImGui::EndChild();

		ImGui::EndPopup();

		m_EntryMenuOpenLastFrame = outcome.EntryMenuOpen;

		if (outcome.ToggledToastId.has_value())
		{
			ToggleExpanded(*outcome.ToggledToastId);
		}

		if (outcome.TextToCopy.has_value())
		{
			Copy(*outcome.TextToCopy);
		}

		if (clearClicked)
		{
			history.Clear();
			m_Expanded.clear();
		}
	}

	bool NotificationsPopup::DrawHeader()
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

		return DrawIconButton("##clear", ICON_MDI_DELETE_SWEEP_OUTLINE, false, "Clear");
	}

	void NotificationsPopup::ToggleExpanded(std::uint64_t toastId)
	{
		const bool wasExpanded = m_Expanded.erase(toastId) > 0;
		if (!wasExpanded)
		{
			m_Expanded.insert(toastId);
		}
	}

	void NotificationsPopup::Copy(const std::string& copiedText)
	{
		if (OnCopied)
		{
			OnCopied(copiedText);
			return;
		}

		ImGui::SetClipboardText(copiedText.c_str());
	}
}
