#include "App/Logo.h"

#include "App/LogoShape.h"
#include "App/LogoSvg.h"

#include <imgui_internal.h>

#include <string_view>
#include <vector>

namespace Graphite {

	namespace {
		const LogoShape& Shape()
		{
			// Parsed once, on first draw: the SVG is in the executable and cannot change.
			static const LogoShape shape =
				ParseLogoSvg(std::string_view(reinterpret_cast<const char*>(kLogoSvg), kLogoSvgSize));
			return shape;
		}
	}

	float LogoWidth(float height)
	{
		const LogoShape& shape = Shape();
		const float tall = shape.Max.y - shape.Min.y;

		return tall > 0.0f ? height * (shape.Max.x - shape.Min.x) / tall : 0.0f;
	}

	void DrawLogo(ImDrawList* draw, ImVec2 centre, float height)
	{
		const LogoShape& shape = Shape();
		const float tall = shape.Max.y - shape.Min.y;

		if (tall <= 0.0f)
		{
			return;
		}

		// Centred on the faces' own bounds rather than on the SVG's canvas.
		const float scale = height / tall;
		const ImVec2 middle((shape.Min.x + shape.Max.x) * 0.5f, (shape.Min.y + shape.Max.y) * 0.5f);

		const auto place = [&](ImVec2 point) {
			return ImVec2(centre.x + (point.x - middle.x) * scale, centre.y + (point.y - middle.y) * scale);
		};

		std::vector<ImVec2> points;

		for (const LogoFace& face : shape.Faces)
		{
			points.clear();

			for (const ImVec2& point : face.Points)
			{
				points.push_back(place(point));
			}

			// Filled flat, then shaded: the draw list has no gradient fill of its own.
			const int first = draw->VtxBuffer.Size;
			draw->AddConcavePolyFilled(points.data(), static_cast<int>(points.size()), face.FromColour);
			ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, first, draw->VtxBuffer.Size, place(face.From),
														  place(face.To), face.FromColour, face.ToColour);
		}
	}
}
