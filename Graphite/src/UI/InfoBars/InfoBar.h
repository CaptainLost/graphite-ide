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

		bool Primary = false;
	};

	struct InfoBarContent
	{
		const char* Id = "##infoBar";
		InfoBarWeight Weight = InfoBarWeight::Information;
		const char* Glyph = "";

		ImVec4 WeightColour;

		std::string Message;
		std::vector<InfoBarButton> Buttons;

		const char* CloseTooltip = nullptr;
	};

	struct InfoBarAnswer
	{
		std::optional<std::size_t> Pressed;
		bool Closed = false;
	};

	InfoBarAnswer DrawInfoBar(const InfoBarContent& content);

	ImVec4 InfoBarBackground(InfoBarWeight weight, const ImVec4& weightColour);
}
