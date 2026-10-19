#include <doctest/doctest.h>

#include "Core/ImGui/Theme.h"
#include "Core/ImGui/UiScale.h"

#include <imgui.h>
#include <imgui_internal.h>

using Core::ScaledStyle;

namespace {
	class IsolatedImGuiContext
	{
	public:
		IsolatedImGuiContext()
			: m_Context(ImGui::CreateContext())
		{
			ImGui::GetIO().IniFilename = nullptr;
		}

		~IsolatedImGuiContext()
		{
			ImGui::DestroyContext(m_Context);
		}

		IsolatedImGuiContext(const IsolatedImGuiContext&) = delete;
		IsolatedImGuiContext& operator=(const IsolatedImGuiContext&) = delete;

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
	CHECK(style.ScrollbarSize == doctest::Approx(24.0f));
	CHECK(style.GrabMinSize == doctest::Approx(ImGuiStyle{}.GrabMinSize * 2.0f));
}

TEST_CASE("The theme draws no tab overline, no line under a tab bar, no close-all button on a dock node, and 12 px scrollbars")
{
	const ImGuiStyle style = ScaledStyle(ImGuiStyle{}, 1.0f, false);
	const ImGuiStyle small = ScaledStyle(ImGuiStyle{}, 0.75f, false);

	CHECK(style.TabBarOverlineSize == doctest::Approx(0.0f));
	CHECK(small.TabBarOverlineSize == doctest::Approx(0.0f));
	CHECK(style.TabBarBorderSize == doctest::Approx(0.0f));
	CHECK(small.TabBarBorderSize == doctest::Approx(0.0f));
	CHECK_FALSE(style.DockingNodeHasCloseButton);
	CHECK(style.ScrollbarSize == doctest::Approx(12.0f));
}

TEST_CASE("Dock splitters are one pixel and windows and popups still draw an edge")
{
	const ImGuiStyle style = ScaledStyle(ImGuiStyle{}, 1.0f, false);

	CHECK(style.DockingSeparatorSize == doctest::Approx(1.0f));
	CHECK(style.WindowBorderSize == doctest::Approx(1.0f));
	CHECK(style.PopupBorderSize == doctest::Approx(1.0f));
}

TEST_CASE("Tabs are square")
{
	CHECK(ScaledStyle(ImGuiStyle{}, 1.0f, false).TabRounding == doctest::Approx(0.0f));
	CHECK(ScaledStyle(ImGuiStyle{}, 2.0f, false).TabRounding == doctest::Approx(0.0f));
}

TEST_CASE("Indent is fifteen pixels")
{
	CHECK(ScaledStyle(ImGuiStyle{}, 1.0f, false).IndentSpacing == doctest::Approx(15.0f));
	CHECK(ScaledStyle(ImGuiStyle{}, 2.0f, false).IndentSpacing == doctest::Approx(30.0f));
}

TEST_CASE("Scrollbar grabs are four pixels")
{
	constexpr float kBothSides = 2.0f;
	const ImGuiStyle style = ScaledStyle(ImGuiStyle{}, 1.0f, false);

	CHECK(style.ScrollbarSize == doctest::Approx(12.0f));
	CHECK(style.ScrollbarPadding == doctest::Approx(4.0f));
	CHECK(style.ScrollbarSize - kBothSides * style.ScrollbarPadding == doctest::Approx(4.0f));
}

TEST_CASE("Selectables are rounded, menu items among them")
{
	const ImGuiStyle style = ScaledStyle(ImGuiStyle{}, 1.0f, false);
	const ImGuiStyle doubled = ScaledStyle(ImGuiStyle{}, 2.0f, false);

	CHECK(style.SelectableRounding == doctest::Approx(4.0f));
	CHECK(doubled.SelectableRounding == doctest::Approx(8.0f));
	CHECK(style.MenuItemRounding == doctest::Approx(4.0f));
	CHECK(doubled.MenuItemRounding == doctest::Approx(8.0f));
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
	IsolatedImGuiContext context;
	ImGui::GetStyle().FontScaleMain = 1.25f;
	ImGui::GetStyle().FontScaleDpi = 2.0f;

	CHECK(Core::UiScale() == doctest::Approx(2.5f));
	CHECK(Core::Scaled(4.0f) == doctest::Approx(10.0f));
	CHECK(Core::Scaled(ImVec2(2.0f, 4.0f)).y == doctest::Approx(10.0f));
}

TEST_CASE("ScaledStyle below one keeps every hairline at least a pixel thick and a size the theme sets to zero at zero")
{
	const ImGuiStyle style = ScaledStyle(ImGuiStyle{}, 0.75f, false);

	CHECK(style.WindowBorderSize == doctest::Approx(1.0f));
	CHECK(style.PopupBorderSize == doctest::Approx(1.0f));
	CHECK(style.ChildBorderSize == doctest::Approx(1.0f));
	CHECK(style.SeparatorSize >= 1.0f);
	CHECK(style.InputTextCursorSize >= 1.0f);
	CHECK(style.MouseCursorScale >= 1.0f);
	CHECK(style.FrameBorderSize == doctest::Approx(0.0f));
}

TEST_CASE("A change of scale multiplies by the ratio of the two")
{
	CHECK(Core::ScaleChangeFactor(1.0f, 1.5f) == doctest::Approx(1.5f));
	CHECK(Core::ScaleChangeFactor(2.0f, 1.0f) == doctest::Approx(0.5f));
	CHECK(Core::ScaleChangeFactor(1.25f, 1.25f) == doctest::Approx(1.0f));
}

TEST_CASE("The first scale, which is zero before the first frame, and a scale that is not one change nothing")
{
	CHECK(Core::ScaleChangeFactor(0.0f, 1.5f) == doctest::Approx(1.0f));
	CHECK(Core::ScaleChangeFactor(1.5f, 0.0f) == doctest::Approx(1.0f));
	CHECK(Core::ScaleChangeFactor(-1.0f, 1.5f) == doctest::Approx(1.0f));
}

TEST_CASE("Scaling the dock nodes multiplies their reference sizes and leaves a zero reference size at zero")
{
	IsolatedImGuiContext context;

	const ImGuiID first = ImGui::DockBuilderAddNode(0x100, ImGuiDockNodeFlags_None);
	const ImGuiID second = ImGui::DockBuilderAddNode(0x200, ImGuiDockNodeFlags_None);
	ImGui::DockBuilderGetNode(first)->SizeRef = ImVec2(300.0f, 200.0f);
	ImGui::DockBuilderGetNode(second)->SizeRef = ImVec2(0.0f, 120.0f);

	Core::ScaleDockNodeSizes(1.5f);

	CHECK(ImGui::DockBuilderGetNode(first)->SizeRef.x == doctest::Approx(450.0f));
	CHECK(ImGui::DockBuilderGetNode(first)->SizeRef.y == doctest::Approx(300.0f));
	CHECK(ImGui::DockBuilderGetNode(second)->SizeRef.x == doctest::Approx(0.0f));
	CHECK(ImGui::DockBuilderGetNode(second)->SizeRef.y == doctest::Approx(180.0f));

	Core::ScaleDockNodeSizes(1.0f);

	CHECK(ImGui::DockBuilderGetNode(first)->SizeRef.x == doctest::Approx(450.0f));
}
