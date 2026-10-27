#include "Editor/Breadcrumb.h"

#include "Language/Api/Declarations.h"
#include "UI/TypographicMarks.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/TextMetrics.h"
#include "Core/ImGui/UiScale.h"

#include <IconsLucide.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace Graphite {

	namespace {
		constexpr float kBreadcrumbRightPadding = kBreadcrumbLeftPadding;
		constexpr float kChevronSideGap = 2.0f;
		constexpr float kGlyphToNameGap = 4.0f;
		constexpr float kChevronSides = 2.0f;
		constexpr float kChevronAlpha = 0.7f;
		constexpr float kHalf = 0.5f;
		constexpr float kNoItemSpacing = 0.0f;
		constexpr bool kClipWithinTheWindow = true;

		constexpr const char* kChevronGlyph = ICON_LC_CHEVRON_RIGHT;
		// U+0192, LATIN SMALL LETTER F WITH HOOK, which JetBrains Mono carries.
		constexpr std::string_view kFunctionMark = "\xC6\x92";
		constexpr std::string_view kTypeMark = "{}";
		constexpr ImU32 kDarkMarkColour = IM_COL32(0xB3, 0x92, 0xE8, 0xFF);
		constexpr ImU32 kLightMarkColour = IM_COL32(0x7A, 0x4F, 0xC4, 0xFF);
		constexpr const char* kRowId = "##Breadcrumb";

		struct Segment
		{
			std::optional<FileKind> Badge;
			std::string_view Mark;
			ImU32 MarkColour = 0;
			std::string_view Text;
			ImU32 TextColour = 0;
		};

		bool IsOnlyASeparator(const std::filesystem::path& part)
		{
			return !part.has_root_name() && !part.has_relative_path();
		}

		std::vector<std::string> FolderNamesOf(const std::filesystem::path& folder)
		{
			std::vector<std::string> names;

			for (const std::filesystem::path& part : folder)
			{
				if (!IsOnlyASeparator(part))
				{
					names.push_back(Core::ToUtf8(part));
				}
			}

			return names;
		}

		std::filesystem::path FoldersShownFor(const std::filesystem::path& root, const std::filesystem::path& path)
		{
			if (const std::optional<std::filesystem::path> relative = Core::RelativeTo(path, root))
			{
				return relative->parent_path();
			}

			return Core::NormalizePath(path).parent_path();
		}

		std::string_view SymbolMarkOf(DeclarationKind kind)
		{
			return kind == DeclarationKind::Function ? kFunctionMark : kTypeMark;
		}

		ImU32 MarkColour()
		{
			return IsLightScheme() ? kLightMarkColour : kDarkMarkColour;
		}

		ImU32 DimColour()
		{
			return ImGui::GetColorU32(ImGuiCol_TextDisabled);
		}

		ImU32 ChevronColour()
		{
			return Core::WithAlphaScaled(DimColour(), kChevronAlpha);
		}

		ImU32 TextColour()
		{
			return ImGui::GetColorU32(ImGuiCol_Text);
		}

		std::vector<Segment> SegmentsOf(const BreadcrumbModel& model, std::size_t firstShownFolder)
		{
			std::vector<Segment> segments;

			if (firstShownFolder > 0)
			{
				segments.push_back(Segment{.Text = kEllipsis, .TextColour = DimColour()});
			}

			for (std::size_t folder = firstShownFolder; folder < model.Folders.size(); ++folder)
			{
				segments.push_back(Segment{.Text = model.Folders[folder], .TextColour = DimColour()});
			}

			segments.push_back(Segment{.Badge = model.Kind, .Text = model.FileName, .TextColour = TextColour()});

			for (const BreadcrumbSymbol& symbol : model.Symbols)
			{
				segments.push_back(Segment{.Mark = SymbolMarkOf(symbol.Kind),
										   .MarkColour = MarkColour(),
										   .Text = symbol.Name,
										   .TextColour = TextColour()});
			}

			return segments;
		}

		float ChevronWidth()
		{
			return ImGui::GetFontSize();
		}

		float MarkWidth(std::string_view mark)
		{
			Core::PushCodeFont(kBreadcrumbMarkFontSize);
			const float width = Core::TextWidth(mark);
			ImGui::PopFont();

			return width;
		}

		std::optional<float> IconWidthOf(const Segment& segment)
		{
			if (segment.Badge.has_value())
			{
				return FileIconWidth();
			}

			if (!segment.Mark.empty())
			{
				return MarkWidth(segment.Mark);
			}

			return std::nullopt;
		}

		float SeparatorWidth()
		{
			return ChevronWidth() + Core::Scaled(kChevronSideGap) * kChevronSides;
		}

		float SegmentWidth(const Segment& segment)
		{
			const std::optional<float> icon = IconWidthOf(segment);
			const float iconSlot = icon.has_value() ? *icon + Core::Scaled(kGlyphToNameGap) : 0.0f;

			return iconSlot + Core::TextWidth(segment.Text);
		}

		float WidthOf(const std::vector<Segment>& segments)
		{
			float width = segments.empty() ? 0.0f : SeparatorWidth() * static_cast<float>(segments.size() - 1);

			for (const Segment& segment : segments)
			{
				width += SegmentWidth(segment);
			}

			return width;
		}

		std::size_t FirstFolderFitting(const BreadcrumbModel& model, float room)
		{
			for (std::size_t first = 0; first < model.Folders.size(); ++first)
			{
				if (WidthOf(SegmentsOf(model, first)) <= room)
				{
					return first;
				}
			}

			return model.Folders.size();
		}

		// The top of a line of the current face centred in the row.
		float LineTopIn(const ImRect& row)
		{
			return row.Min.y + std::floor((row.GetHeight() - ImGui::GetTextLineHeight()) * kHalf);
		}

		// WARNING: A mark is text in the code face, which has its own line, so it is centred in the row at its own
		// size rather than set on the interface face's line.
		void DrawMark(std::string_view mark, ImU32 colour, float x, const ImRect& row)
		{
			Core::PushCodeFont(kBreadcrumbMarkFontSize);
			ImGui::GetWindowDrawList()->AddText(ImVec2(x, LineTopIn(row)), colour, mark.data(), mark.data() + mark.size());
			ImGui::PopFont();
		}

		void DrawSegments(const std::vector<Segment>& segments, float left, const ImRect& row)
		{
			ImDrawList* const drawList = ImGui::GetWindowDrawList();
			const float lineTop = LineTopIn(row);
			float x = left;

			for (std::size_t index = 0; index < segments.size(); ++index)
			{
				const Segment& segment = segments[index];

				if (index > 0)
				{
					x += Core::Scaled(kChevronSideGap);
					drawList->AddText(ImVec2(x, lineTop), ChevronColour(), kChevronGlyph);
					x += ChevronWidth() + Core::Scaled(kChevronSideGap);
				}

				if (segment.Badge.has_value())
				{
					DrawFileIcon(*segment.Badge, ImVec2(x, lineTop));
				}
				else if (!segment.Mark.empty())
				{
					DrawMark(segment.Mark, segment.MarkColour, x, row);
				}

				if (const std::optional<float> icon = IconWidthOf(segment))
				{
					x += *icon + Core::Scaled(kGlyphToNameGap);
				}

				drawList->AddText(ImVec2(x, lineTop), segment.TextColour, segment.Text.data(),
								  segment.Text.data() + segment.Text.size());
				x += Core::TextWidth(segment.Text);
			}
		}

		float RoomIn(float rowWidth, float toolsWidth)
		{
			return rowWidth - Core::Scaled(kBreadcrumbLeftPadding) - Core::Scaled(kBreadcrumbRightPadding) - toolsWidth;
		}

		float ToolsLeftIn(const ImRect& row, float toolsWidth)
		{
			return row.Max.x - Core::Scaled(kBreadcrumbRightPadding) - toolsWidth;
		}

		// WARNING: The row is a button that never acts, and NoFocus keeps its press from focusing anything. A press
		// on a window's empty space focuses that window, which takes the keyboard from the editor surface below.
		// It stops short of the tools, so a press on them reaches them.
		void ReserveRow(ImVec2 size)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, kNoItemSpacing));

			if (size.x > 0.0f)
			{
				ImGui::InvisibleButton(kRowId, size, ImGuiButtonFlags_NoFocus | ImGuiButtonFlags_NoNavFocus);
			}
			else
			{
				ImGui::Dummy(size);
			}

			ImGui::PopStyleVar();
		}
	}

	BreadcrumbModel BuildBreadcrumb(const std::filesystem::path& root, const std::filesystem::path& path,
									std::span<const Declaration> declarations, std::size_t caretLine)
	{
		BreadcrumbModel model;
		model.Folders = FolderNamesOf(FoldersShownFor(root, path));
		model.FileName = Core::ToUtf8(path.filename());
		model.Kind = KindOfFile(path);

		for (const Declaration* declaration : InnermostDeclarationAt(declarations, caretLine))
		{
			if (declaration->Kind != DeclarationKind::Other)
			{
				model.Symbols.push_back(BreadcrumbSymbol{.Name = declaration->Name, .Kind = declaration->Kind});
			}
		}

		return model;
	}

	float BreadcrumbHeight()
	{
		return Core::Scaled(kBreadcrumbHeight);
	}

	float BreadcrumbWidth(const BreadcrumbModel& model, std::size_t firstShownFolder)
	{
		Core::PushInterfaceFont(kBreadcrumbFontSize);
		const float width = WidthOf(SegmentsOf(model, std::min(firstShownFolder, model.Folders.size())));
		ImGui::PopFont();

		return width;
	}

	std::size_t FirstShownFolder(const BreadcrumbModel& model, float room)
	{
		Core::PushInterfaceFont(kBreadcrumbFontSize);
		const std::size_t first = FirstFolderFitting(model, room);
		ImGui::PopFont();

		return first;
	}

	// WARNING: Every piece is drawn by position and the row is then laid out once, by the button at its start, with no
	// item spacing after it, so the editor surface below starts at the row's bottom edge and is shorter by its height.
	// The row spans the window's inner rectangle rather than its content region, on both axes. Across, the padding is
	// measured from the window's edge and not from inside the window padding. Down, the row stands at the inner
	// rectangle's top, directly under the tab bar whatever its height, so the window's top padding is not kept above
	// it while its side and bottom padding stay.
	//
	// The tools stand at the row's right end and the folders give up room for them. The reserving button spans only
	// what is left of them, and the cursor is put back at the row's start first, so the row ends at its bottom edge
	// whatever the tools did with the cursor.
	void DrawBreadcrumb(const BreadcrumbModel& model, const BreadcrumbTools& tools)
	{
		const ImGuiWindow* window = ImGui::GetCurrentWindow();

		if (window->SkipItems)
		{
			return;
		}

		const ImVec2 rowStart(ImGui::GetCursorScreenPos().x, window->InnerRect.Min.y);
		const float rowHeight = BreadcrumbHeight();
		const ImRect row(ImVec2(window->InnerRect.Min.x, rowStart.y), ImVec2(window->InnerRect.Max.x, rowStart.y + rowHeight));

		Core::PushInterfaceFont(kBreadcrumbFontSize);

		const std::vector<Segment> segments = SegmentsOf(model, FirstFolderFitting(model, RoomIn(row.GetWidth(), tools.Width)));

		ImGui::PushClipRect(row.Min, row.Max, kClipWithinTheWindow);
		DrawSegments(segments, row.Min.x + Core::Scaled(kBreadcrumbLeftPadding), row);
		ImGui::PopClipRect();

		ImGui::PopFont();

		const bool hasTools = tools.Draw && tools.Width > 0.0f;
		const float toolsLeft = ToolsLeftIn(row, hasTools ? tools.Width : 0.0f);

		if (hasTools)
		{
			ImGui::PushClipRect(row.Min, row.Max, kClipWithinTheWindow);
			ImGui::SetCursorScreenPos(ImVec2(toolsLeft, row.Min.y));
			tools.Draw();
			ImGui::PopClipRect();
		}

		ImGui::SetCursorScreenPos(rowStart);
		const float reserved = hasTools ? toolsLeft - rowStart.x : ImGui::GetContentRegionAvail().x;
		ReserveRow(ImVec2(std::max(0.0f, reserved), rowHeight));
	}
}
