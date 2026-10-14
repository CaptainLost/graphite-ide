#pragma once

#include <cstddef>

namespace Graphite {

	inline constexpr std::size_t kPreviewSpan = 21;

	struct PreviewWindow
	{
		std::size_t First = 0;
		std::size_t Last = 0;

		friend bool operator==(const PreviewWindow&, const PreviewWindow&) = default;
	};

	PreviewWindow WindowAround(std::size_t line, std::size_t lineCount, std::size_t span);

	struct ScrollCentringInput
	{
		float ContentTop = 0.0f;
		std::size_t RowsAbove = 0;
		float LineHeight = 0.0f;
		float ViewHeight = 0.0f;
	};

	float ScrollCentring(const ScrollCentringInput& input);
}
