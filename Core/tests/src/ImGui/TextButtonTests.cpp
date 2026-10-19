#include <doctest/doctest.h>

#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/TextButton.h"
#include "Core/ImGui/Theme.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace {

	constexpr float kFieldHeight = 26.0f;
	constexpr float kTextButtonRounding = 5.0f;
	constexpr int kSettleFrames = 2;
	const ImVec2 kFarAway(700.0f, 500.0f);

	Core::ChromePalette LightPalette()
	{
		Core::ChromePalette palette = Core::Theme::DefaultPalette();
		palette.Background = Core::Color{0.953f, 0.953f, 0.953f, 1.0f};
		palette.Surface = Core::Color{1.0f, 1.0f, 1.0f, 1.0f};
		palette.SurfaceHovered = Core::Color{0.898f, 0.898f, 0.898f, 1.0f};
		palette.SurfaceActive = Core::Color{0.839f, 0.839f, 0.839f, 1.0f};
		palette.Text = Core::Color{0.141f, 0.141f, 0.141f, 1.0f};
		palette.TextDisabled = Core::Color{0.431f, 0.431f, 0.431f, 1.0f};
		palette.Border = Core::Color{0.784f, 0.784f, 0.784f, 1.0f};

		return palette;
	}

	struct DrawnButton
	{
		ImRect Frame;
		int VerticesInHoverSurface = 0;
		int VerticesInLineColour = 0;
		int VerticesOutsideTheText = 0;
	};

	class ButtonScreen
	{
	public:
		explicit ButtonScreen(const Core::ChromePalette& palette)
			: m_Context(ImGui::CreateContext())
		{
			ImGuiIO& inputOutput = ImGui::GetIO();
			inputOutput.DisplaySize = ImVec2(800.0f, 600.0f);
			inputOutput.DeltaTime = 1.0f / 60.0f;
			inputOutput.IniFilename = nullptr;
			inputOutput.Fonts->Build();

			Core::Theme::Apply(palette);
			Core::Theme::SetMetrics(ImGui::GetStyle());
			m_HoverSurface = ImGui::ColorConvertFloat4ToU32(Core::ToImVec4(palette.SurfaceHovered));
			m_Line = ImGui::ColorConvertFloat4ToU32(Core::ToImVec4(palette.Border));
			m_Text = ImGui::ColorConvertFloat4ToU32(Core::ToImVec4(palette.Text));
		}

		~ButtonScreen() { ImGui::DestroyContext(m_Context); }

		ButtonScreen(const ButtonScreen&) = delete;
		ButtonScreen& operator=(const ButtonScreen&) = delete;

		DrawnButton DrawWithPointerAt(ImVec2 pointer)
		{
			ImGui::GetIO().AddMousePosEvent(pointer.x, pointer.y);

			DrawnButton drawn;

			for (int frame = 0; frame < kSettleFrames; ++frame)
			{
				drawn = DrawOnce();
			}

			return drawn;
		}

	private:
		DrawnButton DrawOnce()
		{
			DrawnButton drawn;

			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
			ImGui::Begin("Buttons", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
			ImGui::PushFont(nullptr, Core::kInterfaceFontSize);

			ImDrawList& drawing = *ImGui::GetWindowDrawList();
			const int verticesBefore = drawing.VtxBuffer.Size;

			Core::TextButton("Search");
			drawn.Frame = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			for (int vertexIndex = verticesBefore; vertexIndex < drawing.VtxBuffer.Size; ++vertexIndex)
			{
				const ImDrawVert& vertex = drawing.VtxBuffer[vertexIndex];
				drawn.VerticesInHoverSurface += vertex.col == m_HoverSurface ? 1 : 0;
				drawn.VerticesInLineColour += vertex.col == m_Line ? 1 : 0;
				drawn.VerticesOutsideTheText += vertex.col != m_Text ? 1 : 0;
			}

			ImGui::PopFont();
			ImGui::End();
			ImGui::EndFrame();

			return drawn;
		}

		ImGuiContext* m_Context = nullptr;
		ImU32 m_HoverSurface = 0;
		ImU32 m_Line = 0;
		ImU32 m_Text = 0;
	};
}

TEST_CASE("A text button has no fill at rest and the hover surface when hovered, in dark and in light")
{
	for (const Core::ChromePalette& palette : {Core::Theme::DefaultPalette(), LightPalette()})
	{
		CAPTURE(palette.Background.R);
		ButtonScreen screen(palette);

		const DrawnButton resting = screen.DrawWithPointerAt(kFarAway);
		CHECK(resting.VerticesOutsideTheText == 0);

		const DrawnButton hovered = screen.DrawWithPointerAt(resting.Frame.GetCenter());
		CHECK(hovered.VerticesInHoverSurface > 0);
	}
}

TEST_CASE("A text button is as tall as a field and draws no border, though fields do")
{
	ButtonScreen screen(Core::Theme::DefaultPalette());

	const DrawnButton resting = screen.DrawWithPointerAt(kFarAway);
	CHECK(resting.Frame.GetHeight() == doctest::Approx(kFieldHeight));
	CHECK(ImGui::GetStyle().FrameBorderSize > 0.0f);

	const DrawnButton hovered = screen.DrawWithPointerAt(resting.Frame.GetCenter());
	CHECK(hovered.VerticesInLineColour == 0);
}

TEST_CASE("A text button rounds its corners by five pixels, a pixel less than a field")
{
	ButtonScreen screen(Core::Theme::DefaultPalette());

	CHECK(Core::TextButtonRounding() == doctest::Approx(kTextButtonRounding));
	CHECK(ImGui::GetStyle().FrameRounding > Core::TextButtonRounding());
}
