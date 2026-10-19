#include "Editor/Breadcrumb.h"

#include "Language/Api/Declarations.h"
#include "UI/TypographicMarks.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/FontSet.h"
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
		constexpr float kHalf = 0.5f;
		constexpr float kNoItemSpacing = 0.0f;
		constexpr bool kClipWithinTheWindow = true;

		constexpr const char* kChevronGlyph = ICON_LC_CHEVRON_RIGHT;
		constexpr const char* kFunctionGlyph = ICON_LC_SQUARE_FUNCTION;
		constexpr const char* kTypeGlyph = ICON_LC_BRACES;
		constexpr const char* kRowId = "##Breadcrumb";

		struct Segment
		{
			const char* Glyph = nullptr;
			ImU32 GlyphColour = 0;
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

		const char* SymbolGlyphOf(DeclarationKind kind)
		{
			return kind == DeclarationKind::Function ? kFunctionGlyph : kTypeGlyph;
		}

		ImU32 DimColour()
		{
			return ImGui::GetColorU32(ImGuiCol_TextDisabled);
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

			const RowIcon fileIcon = FileIconOf(model.Kind);
			segments.push_back(Segment{.Glyph = fileIcon.Glyph,
									   .GlyphColour = fileIcon.Colour,
									   .Text = model.FileName,
									   .TextColour = TextColour()});

			for (const BreadcrumbSymbol& symbol : model.Symbols)
			{
				segments.push_back(Segment{.Glyph = SymbolGlyphOf(symbol.Kind),
										   .GlyphColour = DimColour(),
										   .Text = symbol.Name,
										   .TextColour = TextColour()});
			}

			return segments;
		}

		float GlyphWidth()
		{
			return FileIconWidth();
		}

		float SeparatorWidth()
		{
			return GlyphWidth() + Core::Scaled(kChevronSideGap) * kChevronSides;
		}

		float SegmentWidth(const Segment& segment)
		{
			const float glyph = segment.Glyph != nullptr ? GlyphWidth() + Core::Scaled(kGlyphToNameGap) : 0.0f;

			return glyph + Core::TextWidth(segment.Text);
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

		void DrawSegments(const std::vector<Segment>& segments, ImVec2 lineStart)
		{
			ImDrawList* const drawList = ImGui::GetWindowDrawList();
			float x = lineStart.x;

			for (std::size_t index = 0; index < segments.size(); ++index)
			{
				const Segment& segment = segments[index];

				if (index > 0)
				{
					x += Core::Scaled(kChevronSideGap);
					drawList->AddText(ImVec2(x, lineStart.y), DimColour(), kChevronGlyph);
					x += GlyphWidth() + Core::Scaled(kChevronSideGap);
				}

				if (segment.Glyph != nullptr)
				{
					DrawRowIcon(RowIcon{.Glyph = segment.Glyph, .Colour = segment.GlyphColour}, ImVec2(x, lineStart.y));
					x += GlyphWidth() + Core::Scaled(kGlyphToNameGap);
				}

				drawList->AddText(ImVec2(x, lineStart.y), segment.TextColour, segment.Text.data(),
								  segment.Text.data() + segment.Text.size());
				x += Core::TextWidth(segment.Text);
			}
		}

		float RoomIn(float rowWidth)
		{
			return rowWidth - Core::Scaled(kBreadcrumbLeftPadding) - Core::Scaled(kBreadcrumbRightPadding);
		}

		// WARNING: The row is a button that never acts, and NoFocus keeps its press from focusing anything. A press
		// on a window's empty space focuses that window, which takes the keyboard from the editor surface below.
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
	// The pieces span the window's inner rectangle rather than its content region, so the padding is measured from the
	// window's edge and not from inside the window padding.
	void DrawBreadcrumb(const BreadcrumbModel& model)
	{
		const ImGuiWindow* window = ImGui::GetCurrentWindow();

		if (window->SkipItems)
		{
			return;
		}

		const ImVec2 rowStart = ImGui::GetCursorScreenPos();
		const float rowHeight = BreadcrumbHeight();
		const ImRect row(ImVec2(window->InnerRect.Min.x, rowStart.y), ImVec2(window->InnerRect.Max.x, rowStart.y + rowHeight));

		Core::PushInterfaceFont(kBreadcrumbFontSize);

		const std::vector<Segment> segments = SegmentsOf(model, FirstFolderFitting(model, RoomIn(row.GetWidth())));
		const float lineTop = rowStart.y + std::floor((rowHeight - ImGui::GetTextLineHeight()) * kHalf);

		ImGui::PushClipRect(row.Min, row.Max, kClipWithinTheWindow);
		DrawSegments(segments, ImVec2(row.Min.x + Core::Scaled(kBreadcrumbLeftPadding), lineTop));
		ImGui::PopClipRect();

		ImGui::PopFont();

		ImGui::SetCursorScreenPos(rowStart);
		ReserveRow(ImVec2(std::max(0.0f, ImGui::GetContentRegionAvail().x), rowHeight));
	}
}
