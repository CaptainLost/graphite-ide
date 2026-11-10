#include "Sessions/Overview/DashboardCanvas.h"

#include "UI/FittedText.h"
#include "UI/TypographicMarks.h"

#include "Core/ImGui/ImGuiRenderer.h"

#include <algorithm>
#include <cctype>
#include <cfloat>

namespace Graphite {

	namespace {
		constexpr float kFaintShareOfTile = 0.35f;
		constexpr float kBrighterShare = 0.25f;
		constexpr float kNoWrap = 0.0f;
		constexpr float kTileRounding = 0.4f;
		constexpr float kCaptionGap = 0.5f;
		constexpr float kAsideGap = 0.5f;
		constexpr float kBottomPadding = 0.55f;
		constexpr float kSwatchSize = 0.6f;
		constexpr float kSwatchGap = 0.4f;
		constexpr float kSwatchRounding = 0.15f;
		constexpr float kHalf = 0.5f;
	}

	ImFont* FaceOf(TextWeight weight)
	{
		ImFont* const strong = Core::ImGuiRenderer::GetBoldFont();
		return weight == TextWeight::Strong && strong != nullptr ? strong : ImGui::GetFont();
	}

	float TextWidthAt(std::string_view text, float size, TextWeight weight)
	{
		return FaceOf(weight)->CalcTextSizeA(size, FLT_MAX, kNoWrap, text.data(), text.data() + text.size()).x;
	}

	float WrappedHeightAt(std::string_view text, float size, float wrapWidth)
	{
		return FaceOf(TextWeight::Regular)
			->CalcTextSizeA(size, FLT_MAX, wrapWidth, text.data(), text.data() + text.size())
			.y;
	}

	std::string FitAt(std::string_view text, float room, float size, TextWeight weight)
	{
		const float ellipsisWidth = TextWidthAt(kEllipsis, size, weight);
		const std::optional<std::size_t> cut =
			CutToFit(text, room, ellipsisWidth, [&](std::string_view part) { return TextWidthAt(part, size, weight); });

		if (!cut)
		{
			return std::string{text};
		}

		if (ellipsisWidth > room)
		{
			return {};
		}

		return std::string{text.substr(0, *cut)} + std::string{kEllipsis};
	}

	void DrawTextAt(ImDrawList& drawing, ImVec2 at, float size, ImU32 colour, std::string_view text, TextWeight weight)
	{
		drawing.AddText(FaceOf(weight), size, at, colour, text.data(), text.data() + text.size());
	}

	void DrawWrappedAt(ImDrawList& drawing, ImVec2 at, float size, ImU32 colour, std::string_view text, float wrapWidth)
	{
		drawing.AddText(FaceOf(TextWeight::Regular), size, at, colour, text.data(), text.data() + text.size(), wrapWidth);
	}

	ImU32 Faint(const DashboardColours& colours)
	{
		return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(colours.Dim),
													 ImGui::ColorConvertU32ToFloat4(colours.Tile), kFaintShareOfTile));
	}

	ImU32 Brighter(ImU32 colour)
	{
		const ImVec4 channels = ImGui::ColorConvertU32ToFloat4(colour);
		return ImGui::ColorConvertFloat4ToU32(ImLerp(channels, ImVec4(1.0f, 1.0f, 1.0f, channels.w), kBrighterShare));
	}

	bool DashboardCanvas::IsHot(const ImRect& rect) const
	{
		return PointerInside && rect.Contains(ImGui::GetIO().MousePos);
	}

	void DashboardCanvas::Add(DashboardRegion region)
	{
		if (IsHot(region.Rect))
		{
			Regions.push_back(std::move(region));
		}
	}

	const DashboardRegion* DashboardCanvas::Hovered() const
	{
		for (auto region = Regions.rbegin(); region != Regions.rend(); ++region)
		{
			if (IsHot(region->Rect))
			{
				return &*region;
			}
		}

		return nullptr;
	}

	float TileHeaderHeight(float unit)
	{
		return (kTilePaddingYInFontSizes + kCaptionSize + kCaptionGap) * unit;
	}

	float TileBottomPadding(float unit)
	{
		return kBottomPadding * unit;
	}

	float TileSidePadding(float unit)
	{
		return kTilePaddingXInFontSizes * unit;
	}

	void DrawTileBackground(DashboardCanvas& canvas, const ImRect& tile)
	{
		const float rounding = canvas.Units(kTileRounding);
		canvas.Drawing.AddRectFilled(tile.Min, tile.Max, canvas.Colours.Tile, rounding);
		canvas.Drawing.AddRect(tile.Min, tile.Max, canvas.Colours.Frame, rounding);
	}

	ImRect DrawTileFrame(DashboardCanvas& canvas, const ImRect& tile, std::string_view caption, std::string_view aside)
	{
		DrawTileBackground(canvas, tile);

		const float side = TileSidePadding(canvas.Unit);
		const float size = canvas.Units(kCaptionSize);
		const ImVec2 at(tile.Min.x + side, tile.Min.y + canvas.Units(kTilePaddingYInFontSizes));
		const float room = std::max(0.0f, tile.GetWidth() - 2.0f * side);
		const std::string shown = FitAt(UpperCase(caption), room, size);
		DrawTextAt(canvas.Drawing, at, size, canvas.Colours.Dim, shown);

		if (!aside.empty())
		{
			const float asideRoom = room - TextWidthAt(shown, size) - canvas.Units(kAsideGap);
			const std::string fitted = FitAt(aside, std::max(0.0f, asideRoom), size);
			DrawTextAt(canvas.Drawing, ImVec2(tile.Max.x - side - TextWidthAt(fitted, size), at.y), size,
					   Faint(canvas.Colours), fitted);
		}

		return ImRect(ImVec2(tile.Min.x + side, tile.Min.y + TileHeaderHeight(canvas.Unit)),
					  ImVec2(tile.Max.x - side, tile.Max.y - TileBottomPadding(canvas.Unit)));
	}

	std::string UpperCase(std::string_view text)
	{
		std::string upper{text};
		std::ranges::transform(upper, upper.begin(),
							   [](char letter) { return static_cast<char>(std::toupper(static_cast<unsigned char>(letter))); });
		return upper;
	}

	void DrawSwatch(DashboardCanvas& canvas, float left, float top, float pitch, ImU32 colour)
	{
		const float size = canvas.Units(kSwatchSize);
		const ImVec2 min(left, top + (pitch - size) * kHalf);
		canvas.Drawing.AddRectFilled(min, ImVec2(min.x + size, min.y + size), colour, canvas.Units(kSwatchRounding));
	}

	float SwatchRoom(float unit)
	{
		return (kSwatchSize + kSwatchGap) * unit;
	}
}
