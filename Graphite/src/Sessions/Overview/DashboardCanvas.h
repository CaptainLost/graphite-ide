#pragma once

#include "Sessions/Overview/DashboardColours.h"
#include "Sessions/Overview/DashboardLayout.h"
#include "Sessions/Overview/DashboardTips.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	enum class TextFace : std::uint8_t
	{
		Regular,
		Strong,
		Code // the app's code font, for names a person types: settings keys, worktrees
	};

	ImFont* FaceOf(TextFace face);

	// WARNING: Unrounded, unlike ImGui::CalcTextSize, so runs placed side by side stay on the text.
	float TextWidthAt(std::string_view text, float size, TextFace face = TextFace::Regular);

	float WrappedHeightAt(std::string_view text, float size, float wrapWidth);

	// WARNING: Empty when not even the ellipsis fits.
	std::string FitAt(std::string_view text, float room, float size, TextFace face = TextFace::Regular);

	void DrawTextAt(ImDrawList& drawing, ImVec2 at, float size, ImU32 colour, std::string_view text,
					TextFace face = TextFace::Regular);

	void DrawWrappedAt(ImDrawList& drawing, ImVec2 at, float size, ImU32 colour, std::string_view text, float wrapWidth);

	// WARNING: A span between backticks is set in the code face and colour; the backticks are not drawn. Lines break at
	// spaces only.
	float RunsWidthAt(std::string_view text, float size);

	float WrappedRunsHeightAt(std::string_view text, float size, float wrapWidth);

	void DrawWrappedRunsAt(ImDrawList& drawing, ImVec2 at, float size, ImU32 colour, ImU32 codeColour, std::string_view text,
						   float wrapWidth);

	inline constexpr float kCaptionSize = 0.8f;
	inline constexpr float kValueSize = 1.4f;
	inline constexpr float kBodySize = 0.9f;
	inline constexpr float kSmallSize = 0.8f;
	inline constexpr float kLinePitch = 1.3f;

	ImU32 Faint(const DashboardColours& colours);

	ImU32 Brighter(ImU32 colour);

	struct DashboardRegion
	{
		ImRect Rect;
		std::function<TipCard()> Tip;
		std::optional<std::chrono::year_month_day> ShowDay;
		std::optional<std::size_t> OpenSession;
	};

	struct DashboardCanvas
	{
		const DashboardModel& Model;
		const LocalCalendar& Calendar;
		const DashboardColours& Colours;
		ImDrawList& Drawing;
		float Unit = 0.0f;
		bool PointerInside = false;
		std::vector<DashboardRegion> Regions;

		float Units(float fontSizes) const { return fontSizes * Unit; }

		bool IsHot(const ImRect& rect) const;

		// WARNING: Keeps only a region under the pointer, since no other can show a tooltip or take a click this frame.
		void Add(DashboardRegion region);

		const DashboardRegion* Hovered() const;
	};

	float TileHeaderHeight(float unit);

	float TileBottomPadding(float unit);

	float TileSidePadding(float unit);

	ImRect DrawTileFrame(DashboardCanvas& canvas, const ImRect& tile, std::string_view caption, std::string_view aside = {});

	void DrawTileBackground(DashboardCanvas& canvas, const ImRect& tile);

	std::string UpperCase(std::string_view text);

	void DrawSwatch(DashboardCanvas& canvas, float left, float top, float pitch, ImU32 colour);

	float SwatchRoom(float unit);
}
