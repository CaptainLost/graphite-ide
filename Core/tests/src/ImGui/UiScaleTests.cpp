#include <doctest/doctest.h>

#include "Core/ImGui/Theme.h"
#include "Core/ImGui/UiScale.h"

#include <imgui.h>

using Core::ScaledStyle;

namespace {
	// A context of its own, so the style read and written is not another test's.
	class Context
	{
	public:
		Context()
			: m_Context(ImGui::CreateContext())
		{
			ImGui::GetIO().IniFilename = nullptr;
		}

		~Context()
		{
			ImGui::DestroyContext(m_Context);
		}

		Context(const Context&) = delete;
		Context& operator=(const Context&) = delete;

	private:
		ImGuiContext* m_Context = nullptr;
	};
}

TEST_CASE("ScaledStyle at one gives the theme's own sizes")
{
	const ImGuiStyle style = ScaledStyle(ImGuiStyle{}, 1.0f, false);

	CHECK(style.FramePadding.x == doctest::Approx(8.0f));
	CHECK(style.FramePadding.y == doctest::Approx(4.0f));
	CHECK(style.WindowRounding == doctest::Approx(4.0f));
}

TEST_CASE("ScaledStyle multiplies the theme's sizes and ImGui's own by the scale")
{
	const ImGuiStyle style = ScaledStyle(ImGuiStyle{}, 2.0f, false);

	CHECK(style.FramePadding.x == doctest::Approx(16.0f));
	CHECK(style.ItemSpacing.y == doctest::Approx(10.0f));
	CHECK(style.ScrollbarSize == doctest::Approx(ImGuiStyle{}.ScrollbarSize * 2.0f));
}

TEST_CASE("ScaledStyle starts from unscaled sizes, so scaling a scaled style does not compound")
{
	const ImGuiStyle once = ScaledStyle(ImGuiStyle{}, 1.5f, false);
	const ImGuiStyle twice = ScaledStyle(once, 1.5f, false);

	CHECK(twice.FramePadding.x == doctest::Approx(once.FramePadding.x));
	CHECK(twice.ScrollbarSize == doctest::Approx(once.ScrollbarSize));
}

TEST_CASE("ScaledStyle keeps the colours and font scales of the style it is given")
{
	ImGuiStyle current;
	current.Colors[ImGuiCol_Text] = ImVec4(0.1f, 0.2f, 0.3f, 1.0f);
	current.FontScaleMain = 1.25f;
	current.FontScaleDpi = 1.5f;
	current.FontSizeBase = 15.0f;

	const ImGuiStyle style = ScaledStyle(current, 2.0f, false);

	CHECK(style.Colors[ImGuiCol_Text].y == doctest::Approx(0.2f));
	CHECK(style.FontScaleMain == doctest::Approx(1.25f));
	CHECK(style.FontScaleDpi == doctest::Approx(1.5f));
	CHECK(style.FontSizeBase == doctest::Approx(15.0f));
}

TEST_CASE("ScaledStyle squares off windows and makes their background opaque for viewports")
{
	ImGuiStyle current;
	current.Colors[ImGuiCol_WindowBg].w = 0.5f;

	const ImGuiStyle style = ScaledStyle(current, 1.0f, true);

	CHECK(style.WindowRounding == doctest::Approx(0.0f));
	CHECK(style.Colors[ImGuiCol_WindowBg].w == doctest::Approx(1.0f));
}

TEST_CASE("UiScale is the product of the user's scale and the monitor's")
{
	Context context;
	ImGui::GetStyle().FontScaleMain = 1.25f;
	ImGui::GetStyle().FontScaleDpi = 2.0f;

	CHECK(Core::UiScale() == doctest::Approx(2.5f));
	CHECK(Core::Scaled(4.0f) == doctest::Approx(10.0f));
	CHECK(Core::Scaled(ImVec2(2.0f, 4.0f)).y == doctest::Approx(10.0f));
}

TEST_CASE("ScaledStyle below one keeps every hairline at least a pixel thick")
{
	// ImGui truncates each scaled size, so a 1px border at 75% would become 0 and vanish -
	// and with it the caret of every text field.
	const ImGuiStyle style = ScaledStyle(ImGuiStyle{}, 0.75f, false);

	CHECK(style.WindowBorderSize == doctest::Approx(1.0f));
	CHECK(style.PopupBorderSize == doctest::Approx(1.0f));
	CHECK(style.ChildBorderSize == doctest::Approx(1.0f));
	CHECK(style.SeparatorSize >= 1.0f);
	CHECK(style.InputTextCursorSize >= 1.0f);
	CHECK(style.MouseCursorScale >= 1.0f);
	// A size the theme sets to zero stays zero.
	CHECK(style.FrameBorderSize == doctest::Approx(0.0f));
}
