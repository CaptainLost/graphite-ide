#include <doctest/doctest.h>

#include "Core/App/Redraw.h"
#include "Core/ImGui/Animation.h"

#include <imgui.h>

namespace {
	constexpr float kFrameSeconds = 1.0f / 60.0f;

	// A context of its own per test: ImAnim reads its clock from ImGui's frame step, so the
	// frames have to be real ones.
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

		// Runs one frame and returns the hover amount the item reports in it.
		float Next(ImGuiID id, bool hovered)
		{
			ImGui::NewFrame();
			Core::Animation::BeginFrame();
			const float amount = Core::Animation::HoverAmount(id, hovered);
			ImGui::EndFrame();
			return amount;
		}

		// Runs frames until at least the given time has passed and returns the last amount.
		float After(float seconds, ImGuiID id, bool hovered)
		{
			float amount = 0.0f;
			for (float elapsed = 0.0f; elapsed < seconds; elapsed += kFrameSeconds)
			{
				amount = Next(id, hovered);
			}
			return amount;
		}

	private:
		ImGuiContext* m_Context = nullptr;
	};

	constexpr ImGuiID kItem = 0x1234;
	constexpr ImGuiID kOtherItem = 0x5678;
}

TEST_CASE("An item never hovered rests at zero")
{
	Frames frames;

	CHECK(frames.After(0.5f, kItem, false) == doctest::Approx(0.0f));
}

TEST_CASE("A hover fades in over the hover-in time rather than arriving at once")
{
	Frames frames;
	frames.Next(kItem, false);

	// The frame that changes the target still reports where the fade starts from; the fade
	// is under way from the one after.
	frames.Next(kItem, true);
	const float early = frames.Next(kItem, true);
	CHECK(early > 0.0f);
	CHECK(early < 1.0f);

	CHECK(frames.After(Core::Animation::kHoverInSeconds + 0.05f, kItem, true) ==
		  doctest::Approx(1.0f));
}

TEST_CASE("Leaving an item fades it back to rest over the hover-out time")
{
	Frames frames;
	frames.After(Core::Animation::kHoverInSeconds + 0.05f, kItem, true);

	frames.Next(kItem, false);
	const float early = frames.Next(kItem, false);
	CHECK(early < 1.0f);
	CHECK(early > 0.0f);

	CHECK(frames.After(Core::Animation::kHoverOutSeconds + 0.05f, kItem, false) ==
		  doctest::Approx(0.0f));
}

TEST_CASE("Leaving mid-fade turns around from where the fade had got to")
{
	Frames frames;
	frames.Next(kItem, false);
	const float midway = frames.After(Core::Animation::kHoverInSeconds * 0.5f, kItem, true);
	REQUIRE(midway > 0.0f);
	REQUIRE(midway < 1.0f);

	// The fade turns around from its value in the frame the pointer left, which is one frame
	// further on than the last hovered one - so no jump, then downward from there.
	const float leaving = frames.Next(kItem, false);
	const float turned = frames.Next(kItem, false);

	CHECK(leaving == doctest::Approx(midway).epsilon(0.1));
	CHECK(turned < leaving);
	CHECK(turned > 0.0f);
}

TEST_CASE("One item's hover does not move another's")
{
	Frames frames;
	frames.Next(kOtherItem, false);

	for (int i = 0; i < 30; ++i)
	{
		frames.Next(kItem, true);
		CHECK(frames.Next(kOtherItem, false) == doctest::Approx(0.0f));
	}
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
	frames.Next(kItem, false);
	(void)Core::Redraw::TakeDeadline();

	frames.Next(kItem, true);
	CHECK(Core::Redraw::TakeDeadline().has_value());

	frames.After(Core::Animation::kHoverInSeconds + 0.05f, kItem, true);
	(void)Core::Redraw::TakeDeadline();

	frames.Next(kItem, true);
	CHECK_FALSE(Core::Redraw::TakeDeadline().has_value());
}
