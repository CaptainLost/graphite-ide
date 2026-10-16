#include <doctest/doctest.h>

#include "Core/App/Redraw.h"
#include "Core/ImGui/Animation.h"

#include <imgui.h>

namespace {
	constexpr float kFrameSeconds = 1.0f / 60.0f;

	class Frames
	{
	public:
		Frames()
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(800.0f, 600.0f);
			io.DeltaTime = kFrameSeconds;
			io.Fonts->Build();
		}

		~Frames()
		{
			ImGui::DestroyContext(m_Context);
		}

		Frames(const Frames&) = delete;
		Frames& operator=(const Frames&) = delete;

		float Next(ImGuiID id, bool hovered)
		{
			ImGui::NewFrame();
			Core::Animation::BeginFrame();
			const float amount = Core::Animation::HoverAmount(id, hovered);
			ImGui::EndFrame();
			return amount;
		}

		void Skip(int frameCount)
		{
			for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
			{
				ImGui::NewFrame();
				Core::Animation::BeginFrame();
				ImGui::EndFrame();
			}
		}

		float After(float seconds, ImGuiID id, bool hovered)
		{
			float amount = 0.0f;
			for (float elapsed = 0.0f; elapsed < seconds; elapsed += kFrameSeconds)
			{
				amount = Next(id, hovered);
			}
			return amount;
		}

		float NextSwitch(ImGuiID id, bool on)
		{
			ImGui::NewFrame();
			Core::Animation::BeginFrame();
			const float amount = Core::Animation::SwitchAmount(id, on);
			ImGui::EndFrame();
			return amount;
		}

		float SwitchAfter(float seconds, ImGuiID id, bool on)
		{
			float amount = 0.0f;
			for (float elapsed = 0.0f; elapsed < seconds; elapsed += kFrameSeconds)
			{
				amount = NextSwitch(id, on);
			}
			return amount;
		}

	private:
		ImGuiContext* m_Context = nullptr;
	};

	constexpr ImGuiID kItem = 0x1234;
	constexpr ImGuiID kOtherItem = 0x5678;
	constexpr ImGuiID kComingBackItem = 0x9abc;

	constexpr bool kHovered = true;
	constexpr bool kNotHovered = false;
	constexpr bool kSwitchedOn = true;
	constexpr bool kSwitchedOff = false;

	constexpr float kSettleMarginSeconds = 0.05f;
	constexpr int kHoveredFrames = 30;
}

static_assert(Core::Animation::kLongestStepSeconds == 0.1f);

TEST_CASE("An item never hovered rests at zero")
{
	Frames frames;

	CHECK(frames.After(0.5f, kItem, kNotHovered) == doctest::Approx(0.0f));
}

TEST_CASE("A hover fades in over the hover-in time rather than arriving at once")
{
	Frames frames;
	frames.Next(kItem, kNotHovered);

	frames.Next(kItem, kHovered);
	const float early = frames.Next(kItem, kHovered);
	CHECK(early > 0.0f);
	CHECK(early < 1.0f);

	CHECK(frames.After(Core::Animation::kHoverInSeconds + kSettleMarginSeconds, kItem, kHovered) ==
		  doctest::Approx(1.0f));
}

TEST_CASE("Leaving an item fades it back to rest over the hover-out time")
{
	Frames frames;
	frames.After(Core::Animation::kHoverInSeconds + kSettleMarginSeconds, kItem, kHovered);

	frames.Next(kItem, kNotHovered);
	const float early = frames.Next(kItem, kNotHovered);
	CHECK(early < 1.0f);
	CHECK(early > 0.0f);

	CHECK(frames.After(Core::Animation::kHoverOutSeconds + kSettleMarginSeconds, kItem, kNotHovered) ==
		  doctest::Approx(0.0f));
}

TEST_CASE("Leaving mid-fade turns around from where the fade had got to")
{
	Frames frames;
	frames.Next(kItem, kNotHovered);
	const float midway = frames.After(Core::Animation::kHoverInSeconds * 0.5f, kItem, kHovered);
	REQUIRE(midway > 0.0f);
	REQUIRE(midway < 1.0f);

	const float leaving = frames.Next(kItem, kNotHovered);
	const float turned = frames.Next(kItem, kNotHovered);

	CHECK(leaving == doctest::Approx(midway).epsilon(0.1));
	CHECK(turned < leaving);
	CHECK(turned > 0.0f);
}

TEST_CASE("One item's hover does not move another's")
{
	Frames frames;
	frames.Next(kOtherItem, kNotHovered);

	for (int frameIndex = 0; frameIndex < kHoveredFrames; ++frameIndex)
	{
		frames.Next(kItem, kHovered);
		CHECK(frames.Next(kOtherItem, kNotHovered) == doctest::Approx(0.0f));
	}
}

TEST_CASE("A hover not asked for on the frame before starts at its target, so an item that comes back does not fade from where it was")
{
	Frames frames;
	REQUIRE(frames.After(Core::Animation::kHoverInSeconds + kSettleMarginSeconds, kComingBackItem, kHovered) ==
			doctest::Approx(1.0f));

	const int framesTheItemWasGone = 3;
	frames.Skip(framesTheItemWasGone);

	CHECK(frames.Next(kComingBackItem, kNotHovered) == doctest::Approx(0.0f));
}

TEST_CASE("Blend goes from one colour to the other, alpha included")
{
	const ImVec4 from(0.0f, 0.2f, 0.4f, 0.0f);
	const ImVec4 to(1.0f, 0.6f, 0.4f, 1.0f);

	const ImVec4 start = Core::Animation::Blend(from, to, 0.0f);
	const ImVec4 middle = Core::Animation::Blend(from, to, 0.5f);
	const ImVec4 end = Core::Animation::Blend(from, to, 1.0f);

	CHECK(start.x == doctest::Approx(0.0f));
	CHECK(start.w == doctest::Approx(0.0f));
	CHECK(middle.x == doctest::Approx(0.5f));
	CHECK(middle.y == doctest::Approx(0.4f));
	CHECK(middle.z == doctest::Approx(0.4f));
	CHECK(middle.w == doctest::Approx(0.5f));
	CHECK(end.x == doctest::Approx(1.0f));
	CHECK(end.w == doctest::Approx(1.0f));
}

TEST_CASE("A hover under way asks for the next frame, and a settled one does not")
{
	Frames frames;
	frames.Next(kItem, kNotHovered);
	(void)Core::Redraw::TakeDeadline();

	frames.Next(kItem, kHovered);
	CHECK(Core::Redraw::TakeDeadline().has_value());

	frames.After(Core::Animation::kHoverInSeconds + kSettleMarginSeconds, kItem, kHovered);
	(void)Core::Redraw::TakeDeadline();

	frames.Next(kItem, kHovered);
	CHECK_FALSE(Core::Redraw::TakeDeadline().has_value());
}

TEST_CASE("A switch first seen on stands at one rather than sliding in")
{
	Frames frames;

	CHECK(frames.NextSwitch(kItem, kSwitchedOn) == doctest::Approx(1.0f));
}

TEST_CASE("A switch first seen off stands at zero")
{
	Frames frames;

	CHECK(frames.NextSwitch(kItem, kSwitchedOff) == doctest::Approx(0.0f));
}

TEST_CASE("A flipped switch slides over the switch time and settles at the other end")
{
	Frames frames;
	frames.NextSwitch(kItem, kSwitchedOn);

	const float early = frames.NextSwitch(kItem, kSwitchedOff);
	CHECK(early < 1.0f);
	CHECK(early > 0.0f);

	CHECK(frames.SwitchAfter(Core::Animation::kSwitchSeconds + kSettleMarginSeconds, kItem, kSwitchedOff) == doctest::Approx(0.0f));
}

TEST_CASE("A switch under way asks for the next frame, and a settled one does not")
{
	Frames frames;
	frames.NextSwitch(kItem, kSwitchedOff);
	(void)Core::Redraw::TakeDeadline();

	frames.NextSwitch(kItem, kSwitchedOn);
	CHECK(Core::Redraw::TakeDeadline().has_value());

	frames.SwitchAfter(Core::Animation::kSwitchSeconds + kSettleMarginSeconds, kItem, kSwitchedOn);
	(void)Core::Redraw::TakeDeadline();

	frames.NextSwitch(kItem, kSwitchedOn);
	CHECK_FALSE(Core::Redraw::TakeDeadline().has_value());
}

TEST_CASE("One switch's slide does not move another's")
{
	Frames frames;
	frames.NextSwitch(kOtherItem, kSwitchedOff);
	frames.NextSwitch(kItem, kSwitchedOff);

	frames.NextSwitch(kItem, kSwitchedOn);

	CHECK(frames.NextSwitch(kOtherItem, kSwitchedOff) == doctest::Approx(0.0f));
}
