#include "App/Logo.h"

#include "App/LogoShape.h"
#include "App/LogoSvg.h"

#include <imgui_internal.h>

#include <string_view>
#include <vector>

namespace Graphite {

	namespace {
		const LogoShape& EmbeddedLogoShape()
		{
			static const LogoShape shape =
				ParseLogoSvg(std::string_view(reinterpret_cast<const char*>(kLogoSvg), kLogoSvgSize));
			return shape;
		}

		ImVec2 PlaceInScreen(ImVec2 point, ImVec2 screenCentre, ImVec2 shapeCentre, float scale)
		{
			return ImVec2(screenCentre.x + (point.x - shapeCentre.x) * scale,
						  screenCentre.y + (point.y - shapeCentre.y) * scale);
		}
	}

	float LogoWidth(float height)
	{
		const LogoShape& shape = EmbeddedLogoShape();
		const float shapeHeight = shape.Max.y - shape.Min.y;

		return shapeHeight > 0.0f ? height * (shape.Max.x - shape.Min.x) / shapeHeight : 0.0f;
	}

	void DrawLogo(ImDrawList* drawList, ImVec2 centre, float height)
	{
		const LogoShape& shape = EmbeddedLogoShape();
		const float shapeHeight = shape.Max.y - shape.Min.y;

		if (shapeHeight <= 0.0f)
		{
			return;
		}

		const float scale = height / shapeHeight;
		const ImVec2 facesCentre((shape.Min.x + shape.Max.x) * 0.5f, (shape.Min.y + shape.Max.y) * 0.5f);

		std::vector<ImVec2> screenPoints;

		for (const LogoFace& face : shape.Faces)
		{
			screenPoints.clear();

			for (const ImVec2& point : face.Points)
			{
				screenPoints.push_back(PlaceInScreen(point, centre, facesCentre, scale));
			}

			// WARNING: The draw list has no gradient fill, so the face is filled flat and its vertices are shaded afterwards.
			const int firstVertexIndex = drawList->VtxBuffer.Size;
			drawList->AddConcavePolyFilled(screenPoints.data(), static_cast<int>(screenPoints.size()), face.FromColour);
			ImGui::ShadeVertsLinearColorGradientKeepAlpha(
				drawList, firstVertexIndex, drawList->VtxBuffer.Size, PlaceInScreen(face.From, centre, facesCentre, scale),
				PlaceInScreen(face.To, centre, facesCentre, scale), face.FromColour, face.ToColour);
		}
	}
}
