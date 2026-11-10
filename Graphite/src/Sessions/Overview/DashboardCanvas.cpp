#include "Sessions/Overview/DashboardCanvas.h"

#include "UI/FittedText.h"
#include "UI/TypographicMarks.h"

#include "Core/ImGui/ImGuiRenderer.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <vector>

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
		using DashboardDrawing::kHalf;
		constexpr char kCodeMark = '`';
		constexpr char kSpace = ' ';

		struct Piece
		{
			std::string_view Text;
			TextFace Face = TextFace::Regular;
			bool AfterSpace = false;
		};

		std::vector<Piece> PiecesOf(std::string_view text)
		{
			std::vector<Piece> pieces;
			TextFace face = TextFace::Regular;
			bool afterSpace = false;
			std::size_t start = 0;

			const auto close = [&](std::size_t end) {
				if (end > start)
				{
					pieces.push_back({text.substr(start, end - start), face, afterSpace});
					afterSpace = false;
				}
			};

			for (std::size_t index = 0; index < text.size(); ++index)
			{
				if (text[index] == kSpace || text[index] == kCodeMark)
				{
					close(index);
					start = index + 1;
					afterSpace = afterSpace || text[index] == kSpace;
					face = text[index] == kCodeMark ? (face == TextFace::Code ? TextFace::Regular : TextFace::Code) : face;
				}
			}

			close(text.size());
			return pieces;
		}

		struct PlacedPiece
		{
			Piece Run;
			float X = 0.0f;
			int Line = 0;
		};

		// WARNING: Pieces with no space between them make one word, which never breaks across lines.
		std::vector<PlacedPiece> LayOutRuns(std::string_view text, float size, float wrapWidth)
		{
			const std::vector<Piece> pieces = PiecesOf(text);
			const float space = TextWidthAt(" ", size);
			std::vector<PlacedPiece> placed;
			float x = 0.0f;
			int line = 0;

			for (std::size_t first = 0; first < pieces.size();)
			{
				std::size_t end = first + 1;
				float width = TextWidthAt(pieces[first].Text, size, pieces[first].Face);

				while (end < pieces.size() && !pieces[end].AfterSpace)
				{
					width += TextWidthAt(pieces[end].Text, size, pieces[end].Face);
					++end;
				}

				if (first > 0 && x + space + width > wrapWidth)
				{
					++line;
					x = 0.0f;
				}
				else if (first > 0)
				{
					x += space;
				}

				for (std::size_t index = first; index < end; ++index)
				{
					placed.push_back({pieces[index], x, line});
					x += TextWidthAt(pieces[index].Text, size, pieces[index].Face);
				}

				first = end;
			}

			return placed;
		}
	}

	ImFont* FaceOf(TextFace face)
	{
		ImFont* const chosen = face == TextFace::Strong ? Core::ImGuiRenderer::GetBoldFont()
							   : face == TextFace::Code ? Core::ImGuiRenderer::GetMonoFont()
														: nullptr;
		return chosen != nullptr ? chosen : ImGui::GetFont();
	}

	float TextWidthAt(std::string_view text, float size, TextFace face)
	{
		return FaceOf(face)->CalcTextSizeA(size, FLT_MAX, kNoWrap, text.data(), text.data() + text.size()).x;
	}

	float WrappedHeightAt(std::string_view text, float size, float wrapWidth)
	{
		return FaceOf(TextFace::Regular)
			->CalcTextSizeA(size, FLT_MAX, wrapWidth, text.data(), text.data() + text.size())
			.y;
	}

	std::string FitAt(std::string_view text, float room, float size, TextFace face)
	{
		const float ellipsisWidth = TextWidthAt(kEllipsis, size, face);
		const std::optional<std::size_t> cut =
			CutToFit(text, room, ellipsisWidth, [&](std::string_view part) { return TextWidthAt(part, size, face); });

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

	void DrawTextAt(ImDrawList& drawing, ImVec2 at, float size, ImU32 colour, std::string_view text, TextFace face)
	{
		drawing.AddText(FaceOf(face), size, at, colour, text.data(), text.data() + text.size());
	}

	void DrawWrappedAt(ImDrawList& drawing, ImVec2 at, float size, ImU32 colour, std::string_view text, float wrapWidth)
	{
		drawing.AddText(FaceOf(TextFace::Regular), size, at, colour, text.data(), text.data() + text.size(), wrapWidth);
	}

	float RunsWidthAt(std::string_view text, float size)
	{
		const std::vector<PlacedPiece> placed = LayOutRuns(text, size, FLT_MAX);
		return placed.empty() ? 0.0f : placed.back().X + TextWidthAt(placed.back().Run.Text, size, placed.back().Run.Face);
	}

	float WrappedRunsHeightAt(std::string_view text, float size, float wrapWidth)
	{
		const std::vector<PlacedPiece> placed = LayOutRuns(text, size, wrapWidth);
		return placed.empty() ? 0.0f : static_cast<float>(placed.back().Line + 1) * size;
	}

	void DrawWrappedRunsAt(ImDrawList& drawing, ImVec2 at, float size, ImU32 colour, ImU32 codeColour, std::string_view text,
						   float wrapWidth)
	{
		for (const PlacedPiece& piece : LayOutRuns(text, size, wrapWidth))
		{
			DrawTextAt(drawing, ImVec2(at.x + piece.X, at.y + static_cast<float>(piece.Line) * size), size,
					   piece.Run.Face == TextFace::Code ? codeColour : colour, piece.Run.Text, piece.Run.Face);
		}
	}

	ImU32 Faint(const DashboardColours& colours)
	{
		return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(colours.Dim),
													 ImGui::ColorConvertU32ToFloat4(colours.Tile), kFaintShareOfTile));
	}

	ImU32 Brighter(ImU32 colour, const DashboardColours& colours)
	{
		const ImVec4 channels = ImGui::ColorConvertU32ToFloat4(colour);
		const float toward = IsLightSurface(colours.Tile) ? 0.0f : 1.0f;
		return ImGui::ColorConvertFloat4ToU32(ImLerp(channels, ImVec4(toward, toward, toward, channels.w), kBrighterShare));
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

	void DrawSwatch(ImDrawList& drawing, float unit, float left, float top, float pitch, ImU32 colour)
	{
		const float size = kSwatchSize * unit;
		const ImVec2 min(left, top + (pitch - size) * kHalf);
		drawing.AddRectFilled(min, ImVec2(min.x + size, min.y + size), colour, kSwatchRounding * unit);
	}

	float SwatchRoom(float unit)
	{
		return (kSwatchSize + kSwatchGap) * unit;
	}
}
