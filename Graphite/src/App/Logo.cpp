#include "App/Logo.h"

#include "App/LogoShape.h"
#include "App/LogoSvg.h"

#include <imgui_internal.h>

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <vector>

namespace Graphite {

	namespace {
		constexpr float kRedLuminanceWeight = 0.2126f;
		constexpr float kGreenLuminanceWeight = 0.7152f;
		constexpr float kBlueLuminanceWeight = 0.0722f;

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

		float LuminanceOf(ImU32 colour)
		{
			const ImVec4 channels = ImGui::ColorConvertU32ToFloat4(colour);

			return kRedLuminanceWeight * channels.x + kGreenLuminanceWeight * channels.y + kBlueLuminanceWeight * channels.z;
		}

		struct ShadeRange
		{
			float Darkest = 0.0f;
			float Lightest = 0.0f;
		};

		ShadeRange ShadeRangeOf(const LogoShape& shape)
		{
			ShadeRange range{.Darkest = 1.0f, .Lightest = 0.0f};

			for (const LogoFace& face : shape.Faces)
			{
				for (const ImU32 colour : {face.FromColour, face.ToColour})
				{
					range.Darkest = std::min(range.Darkest, LuminanceOf(colour));
					range.Lightest = std::max(range.Lightest, LuminanceOf(colour));
				}
			}

			return range;
		}

		ImU32 InTones(ImU32 colour, const ShadeRange& range, const LogoTones& tones)
		{
			const float span = range.Lightest - range.Darkest;
			const float share = span > 0.0f ? (LuminanceOf(colour) - range.Darkest) / span : 0.0f;

			ImVec4 toned = ImLerp(ImGui::ColorConvertU32ToFloat4(tones.ForDarkest),
								  ImGui::ColorConvertU32ToFloat4(tones.ForLightest), share);
			toned.w = ImGui::ColorConvertU32ToFloat4(colour).w;

			return ImGui::ColorConvertFloat4ToU32(toned);
		}
	}

	float LogoWidth(float height)
	{
		const LogoShape& shape = EmbeddedLogoShape();
		const float shapeHeight = shape.Max.y - shape.Min.y;

		return shapeHeight > 0.0f ? height * (shape.Max.x - shape.Min.x) / shapeHeight : 0.0f;
	}

	void DrawLogo(ImDrawList* drawList, ImVec2 centre, float height, const std::optional<LogoTones>& tones)
	{
		const LogoShape& shape = EmbeddedLogoShape();
		const float shapeHeight = shape.Max.y - shape.Min.y;

		if (shapeHeight <= 0.0f)
		{
			return;
		}

		const float scale = height / shapeHeight;
		const ImVec2 facesCentre((shape.Min.x + shape.Max.x) * 0.5f, (shape.Min.y + shape.Max.y) * 0.5f);
		const ShadeRange shades = ShadeRangeOf(shape);

		std::vector<ImVec2> screenPoints;

		for (const LogoFace& face : shape.Faces)
		{
			screenPoints.clear();

			for (const ImVec2& point : face.Points)
			{
				screenPoints.push_back(PlaceInScreen(point, centre, facesCentre, scale));
			}

			const ImU32 fromColour = tones ? InTones(face.FromColour, shades, *tones) : face.FromColour;
			const ImU32 toColour = tones ? InTones(face.ToColour, shades, *tones) : face.ToColour;

			// WARNING: The draw list has no gradient fill, so the face is filled flat and its vertices are shaded afterwards.
			const int firstVertexIndex = drawList->VtxBuffer.Size;
			drawList->AddConcavePolyFilled(screenPoints.data(), static_cast<int>(screenPoints.size()), fromColour);
			ImGui::ShadeVertsLinearColorGradientKeepAlpha(
				drawList, firstVertexIndex, drawList->VtxBuffer.Size, PlaceInScreen(face.From, centre, facesCentre, scale),
				PlaceInScreen(face.To, centre, facesCentre, scale), fromColour, toColour);
		}
	}
}
