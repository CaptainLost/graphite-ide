#include "UI/FittedText.h"

#include "UI/TypographicMarks.h"

#include "Core/ImGui/TextMetrics.h"
#include "Core/Text/Encoding.h"

#include <imgui.h>

#include <vector>

namespace Graphite {

	namespace {

		// WARNING: CalcTextSize rounds widths up, so the room is given back the pixel it rounded away or a
		// prefix that exactly fits is dropped.
		constexpr float kCalcTextSizeRoundingPixel = 1.0f;

		std::size_t ByteCountWithoutTrailingSpaces(std::string_view text, std::size_t byteCount)
		{
			while (byteCount > 0 && text[byteCount - 1] == ' ')
			{
				--byteCount;
			}

			return byteCount;
		}
	}

	std::optional<std::size_t> CutToFit(std::string_view text, float maxWidth, float ellipsisWidth,
										const std::function<float(std::string_view)>& widthOf)
	{
		if (widthOf(text) <= maxWidth)
		{
			return std::nullopt;
		}

		const std::vector<std::size_t> boundaries = Core::CharacterBoundaries(text);

		// WARNING: The cut is found by halving, not by walking, because this runs for every visible cell every frame and measuring each prefix in turn is quadratic. Boundary 0 is the floor that is never measured, and the last, the whole text, is already known to be too wide.
		const float budget = maxWidth - ellipsisWidth;
		std::size_t fittingBoundary = 0;
		std::size_t tooWideBoundary = boundaries.size() - 1;

		while (tooWideBoundary - fittingBoundary > 1)
		{
			const std::size_t middleBoundary = fittingBoundary + (tooWideBoundary - fittingBoundary) / 2;

			if (widthOf(text.substr(0, boundaries[middleBoundary])) <= budget)
			{
				fittingBoundary = middleBoundary;
			}
			else
			{
				tooWideBoundary = middleBoundary;
			}
		}

		return ByteCountWithoutTrailingSpaces(text, boundaries[fittingBoundary]);
	}

	std::string FitToWidth(std::string_view text, float room, bool* cut)
	{
		const float roomWithRounding = room + kCalcTextSizeRoundingPixel;
		const float ellipsisWidth = Core::TextWidth(kEllipsis);
		const std::optional<std::size_t> cutPosition = CutToFit(text, roomWithRounding, ellipsisWidth, Core::TextWidth);

		if (cut != nullptr)
		{
			*cut = cutPosition.has_value();
		}

		if (!cutPosition.has_value())
		{
			return std::string(text);
		}

		if (ellipsisWidth > roomWithRounding)
		{
			return {};
		}

		return std::string(text.substr(0, *cutPosition)) + std::string(kEllipsis);
	}

	std::string FitToLine(std::string_view text, bool* cut)
	{
		return FitToWidth(text, ImGui::GetContentRegionAvail().x, cut);
	}
}
