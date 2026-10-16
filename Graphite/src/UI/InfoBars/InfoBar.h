#pragma once

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	enum class InfoBarWeight : std::uint8_t
	{
		Information,
		Warning
	};

	struct InfoBarButton
	{
		std::string Label;

		// The one thing the bar asks for, filled with the accent. The rest are drawn without a fill, and a long label
		// of theirs, a path, is cut short.
		bool Primary = false;
	};

	struct InfoBarContent
	{
		const char* Id = "##infoBar";
		InfoBarWeight Weight = InfoBarWeight::Information;
		const char* Glyph = "";

		// The weight's colour, for the glyph and, on a warning, a tint of the bar.
		ImVec4 WeightColour;

		std::string Message;
		std::vector<InfoBarButton> Buttons;

		// The close button's tooltip; without one the bar cannot be closed.
		const char* CloseTooltip = nullptr;
	};

	struct InfoBarAnswer
	{
		std::optional<std::size_t> Pressed;
		bool Closed = false;
	};

	// The one bar over the editor for something the user should know or decide: the glyph in the weight's colour, the
	// message cut short with an ellipsis where it does not fit, and the buttons at the bar's end before its close
	// button.
	InfoBarAnswer DrawInfoBar(const InfoBarContent& content);

	// The bar's background: the menu bar's colour, tinted toward the weight's colour on a warning.
	ImVec4 InfoBarBackground(InfoBarWeight weight, const ImVec4& weightColour);
}
