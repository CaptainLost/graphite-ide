#include <doctest/doctest.h>

#include "Core/ImGui/Scrollbar.h"

#include <imgui.h>

TEST_CASE("A scrollbar's grab is kept inside the bar by the style's padding on every side")
{
	constexpr float kPadding = 4.0f;

	CHECK(Core::ScrollbarGrabInset(kPadding, ImVec2(12.0f, 300.0f)) == doctest::Approx(4.0f));
}

TEST_CASE("A scrollbar narrower than twice its padding keeps its grab inset by half its width")
{
	constexpr float kPadding = 4.0f;

	CHECK(Core::ScrollbarGrabInset(kPadding, ImVec2(6.0f, 300.0f)) == doctest::Approx(3.0f));
	CHECK(Core::ScrollbarGrabInset(kPadding, ImVec2(12.0f, 5.0f)) == doctest::Approx(2.0f));
}

TEST_CASE("A scrollbar's grab inset is a whole number of pixels, as ImGui's own is")
{
	constexpr float kPadding = 4.5f;

	CHECK(Core::ScrollbarGrabInset(kPadding, ImVec2(18.0f, 300.0f)) == doctest::Approx(4.0f));
}
