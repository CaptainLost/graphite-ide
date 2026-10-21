#include <doctest/doctest.h>

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/Slider.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <optional>

namespace {

	constexpr float kFrameSeconds = 1.0f / 60.0f;
	constexpr float kSliderWidth = 240.0f;
	constexpr float kHalf = 0.5f;
	constexpr int kSmallest = 0;
	constexpr int kLargest = 20;
	const ImVec2 kDisplaySize(400.0f, 300.0f);

	const ImVec4 kWhiteCard(1.0f, 1.0f, 1.0f, 1.0f);
	const ImVec4 kLightAccent(10.0f / 255.0f, 98.0f / 255.0f, 208.0f / 255.0f, 1.0f);

	class SliderFrames
	{
	public:
		SliderFrames()
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = kDisplaySize;
			io.DeltaTime = kFrameSeconds;
			io.IniFilename = nullptr;
			io.Fonts->Build();

			ImGui::StyleColorsLight();
			ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = kWhiteCard;
			ImGui::GetStyle().Colors[ImGuiCol_SliderGrab] = kLightAccent;
		}

		~SliderFrames() { ImGui::DestroyContext(m_Context); }

		SliderFrames(const SliderFrames&) = delete;
		SliderFrames& operator=(const SliderFrames&) = delete;

		void Next(int& value)
		{
			ImGui::NewFrame();
			Core::Animation::BeginFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kDisplaySize);
			ImGui::Begin("slider");

			m_ItemTopLeft = ImGui::GetCursorScreenPos();
			Core::SliderInt("##slider", &value, kSmallest, kLargest, "px", kSliderWidth);
			m_DrawList = ImGui::GetWindowDrawList();

			ImGui::End();
			ImGui::EndFrame();
		}

		// The rightmost point drawn in `colour`, read before the next frame clears the list.
		std::optional<float> RightmostInColour(ImU32 colour) const
		{
			float right = -FLT_MAX;
			bool found = false;

			for (const ImDrawVert& vertex : m_DrawList->VtxBuffer)
			{
				if (vertex.col == colour)
				{
					right = std::max(right, vertex.pos.x);
					found = true;
				}
			}

			return found ? std::optional<float>{right} : std::nullopt;
		}

		ImVec2 GetItemTopLeft() const { return m_ItemTopLeft; }

	private:
		ImGuiContext* m_Context = nullptr;
		ImDrawList* m_DrawList = nullptr;
		ImVec2 m_ItemTopLeft;
	};
}

TEST_CASE("A slider's knob is ringed in the filled track's colour, so it shows on a white card")
{
	SliderFrames frames;
	int value = kSmallest;

	frames.Next(value);

	// At the smallest value the filled track ends at the knob's centre, so anything in its colour
	// further right than that is the knob's edge.
	const float knobRadius = Core::kSliderKnobDiameter * kHalf;
	const float knobCentreX = frames.GetItemTopLeft().x + knobRadius;
	const std::optional<float> rightmost = frames.RightmostInColour(ImGui::GetColorU32(kLightAccent));

	REQUIRE(rightmost.has_value());
	CHECK(*rightmost > knobCentreX + knobRadius * kHalf);
}
