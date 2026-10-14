#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	enum class ToastTextStyle
	{
		Plain,
		Strong,
		Path
	};

	struct ToastTextRun
	{
		std::string Text;
		ToastTextStyle Style = ToastTextStyle::Plain;
		float X = 0.0f;
		float Width = 0.0f;
	};

	struct ToastTextLine
	{
		std::vector<ToastTextRun> Runs;
	};

	using ToastTextMeasure = std::function<float(std::string_view text, bool bold)>;

	std::vector<ToastTextLine> LayoutToastText(std::string_view message, float lineWidth, const ToastTextMeasure& measure);
}
