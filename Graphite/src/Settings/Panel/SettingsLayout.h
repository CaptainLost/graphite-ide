#pragma once

#include "Settings/Panel/SettingsPage.h"

#include <imgui.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace Graphite {

	struct CardMeasures
	{
		float CardWidth = 0.0f;
		float Padding = 0.0f;
		float Gap = 0.0f;
		float StackBelowWidth = 0.0f;
	};

	struct RowColumns
	{
		bool Stacked = false;
		float TextWidth = 0.0f;
		float ControlX = 0.0f;
		float ControlWidth = 0.0f;
	};

	struct RowContent
	{
		float TextHeight = 0.0f;
		float ControlHeight = 0.0f;
		float Padding = 0.0f;
		float Spacing = 0.0f;
	};

	struct RowHeights
	{
		float Row = 0.0f;
		float TextTop = 0.0f;
		float ControlTop = 0.0f;
	};

	RowColumns LayOutRowColumns(const CardMeasures& card, float controlWidth, bool wideControl);

	RowHeights MeasureRowHeights(const RowColumns& columns, const RowContent& content);

	class SettingsLayout final
	{
	public:
		SettingsLayout() = delete;

		static constexpr float kSidebarWidth = 220.0f;
		static constexpr float kInnerListWidth = 190.0f;
		static constexpr float kPageMargin = 20.0f;

		static float ToggleWidth();
		static float SliderWidth();
		static float NumberWidth();
		static float ComboWidth(std::span<const std::string> options);
		static float ButtonsWidth(std::span<const std::string> labels);

		static void DrawPageTitle(std::string_view title);
		static void DrawResultsLabel(std::string_view label);
		static void DrawCard(const SettingsCard& card, ImU32 problemColour);
		static void DrawPageBody(SettingsPage& page, ImU32 problemColour);

		static bool DrawSearchField(std::string& query);
		static void DrawGroupTitle(std::string_view title);
		static void DrawGroupDivider();
		static bool DrawNavigationEntry(const char* icon, std::string_view title, bool selected, bool dimmed);

		static bool Choice(const char* id, std::span<const std::string> options, std::size_t& selected, const std::string& unlistedPreview = {});
	};
}
