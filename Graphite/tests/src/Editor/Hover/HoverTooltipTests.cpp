#include <doctest/doctest.h>

#include "ImGuiFrames.h"

#include "Editor/Hover/HoverTooltip.h"

#include "Core/ImGui/FontSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>
#include <string_view>
#include <vector>

namespace {

	constexpr float kEditorFontSize = 20.0f;
	constexpr std::string_view kHoverTooltipPrefix = "##HoverTooltip";

	Graphite::HoverInfo EverySection()
	{
		Graphite::HoverInfo info;
		info.Kind = "function";
		info.Name = "Draw";
		info.Scope = "Widget";
		info.Access = "public";
		info.Qualifiers = {"static"};
		info.Declaration = "void Draw(int depth);";
		info.OverloadNote = "(+2 overloads)";
		info.ReturnType = "void";
		info.Parameters = {"int depth"};
		info.Type = "int";
		info.TypeAka = "signed int";
		info.Value = "4";
		info.Offset = "8";
		info.Size = "4";
		info.Padding = "4";
		info.Alignment = "4";
		info.Documentation = "Draws the widget.";
		info.Parsed = true;

		return info;
	}

	Graphite::DefinitionPreview PreviewOf(bool tooLarge)
	{
		Graphite::DefinitionPreview preview;
		preview.Path = "widget.txt";
		preview.FirstLine = 4;
		preview.Lines = {"void Widget::Draw(int depth)", "{", "}"};
		preview.TooLarge = tooLarge;

		return preview;
	}

	struct TooltipFrame
	{
		Graphite::HoverActions Actions;
		Graphite::HoverState Hover;
		std::vector<Graphite::DiagnosticSpan> Diagnostics;
		std::string LoadingText;

		int StackBefore = 0;
		int StackAfter = 0;

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f));
			ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

			Core::PushCodeFont(kEditorFontSize);

			Graphite::HoverTooltipLook look;
			look.LineHeight = ImGui::GetTextLineHeight();
			look.CellWidth = ImGui::CalcTextSize("0").x;
			look.LoadingText = LoadingText;

			const Graphite::HoverAnchor anchor{.Min = ImVec2(100.0f, 100.0f), .Max = ImVec2(140.0f, 120.0f)};

			StackBefore = ImGui::GetCurrentContext()->FontStack.Size;
			Graphite::DrawHoverTooltip(Actions, Hover, Diagnostics, look, anchor, nullptr);
			StackAfter = ImGui::GetCurrentContext()->FontStack.Size;

			ImGui::PopFont();
			ImGui::End();
		}
	};

	const ImGuiWindow* HoverTooltipWindow()
	{
		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (std::string_view{window->Name}.starts_with(kHoverTooltipPrefix))
			{
				return window;
			}
		}

		return nullptr;
	}

	float InterfaceLineHeight()
	{
		Core::PushInterfaceFont();
		const float height = ImGui::GetTextLineHeight();
		ImGui::PopFont();

		return height;
	}
}

TEST_CASE("A hover tooltip with every section draws two frames with its fonts balanced")
{
	for (const bool tooLarge : {false, true})
	{
		CAPTURE(tooLarge);

		ImGuiFrames frames;
		TooltipFrame tooltip;
		tooltip.Actions.ShowDiagnostic = true;
		tooltip.Actions.ShowInfo = true;
		tooltip.Actions.ShowPreview = true;
		tooltip.Hover.Info = EverySection();
		tooltip.Hover.Preview = PreviewOf(tooLarge);
		tooltip.Diagnostics = {Graphite::DiagnosticSpan{.From = 0, .To = 4, .Message = "unused parameter", .Source = "clangd"},
							   Graphite::DiagnosticSpan{.From = 0, .To = 4, .Message = "shadows a member"}};

		frames.Next([&] { tooltip.Draw(); });
		frames.Next([&] { tooltip.Draw(); });

		CHECK(tooltip.Hover.Tooltip.Shown);
		CHECK(tooltip.StackAfter == tooltip.StackBefore);
	}
}

TEST_CASE("A hover tooltip that is loading, failed or found no definition draws with its fonts balanced")
{
	ImGuiFrames frames;
	TooltipFrame tooltip;
	tooltip.Actions.InfoLoading = true;
	tooltip.Actions.ShowPreview = true;
	tooltip.Actions.PreviewHeldByCtrl = true;
	tooltip.Hover.Preview = Graphite::DefinitionPreview{.Unreadable = true};
	tooltip.Hover.NoDefinitionFound = true;
	tooltip.LoadingText = "Loading";

	frames.Next([&] { tooltip.Draw(); });
	frames.Next([&] { tooltip.Draw(); });

	CHECK(tooltip.Hover.Tooltip.Shown);
	CHECK(tooltip.StackAfter == tooltip.StackBefore);

	tooltip.Actions = Graphite::HoverActions{.InfoGaveUp = true};
	tooltip.Hover.InfoFailureText = "clangd not installed";

	frames.Next([&] { tooltip.Draw(); });

	CHECK(tooltip.Hover.Tooltip.Shown);
	CHECK(tooltip.StackAfter == tooltip.StackBefore);
}

TEST_CASE("A hover that is still loading shows the loader and nothing else it lacks")
{
	const Graphite::HoverState hover;
	Graphite::HoverTooltipLook look;
	look.LoadingText = "Loading";

	const Graphite::HoverSections sections =
		Graphite::SectionsToShow(Graphite::HoverActions{.InfoLoading = true}, hover, {}, look);

	CHECK(sections.Loading);
	CHECK_FALSE(sections.Info);
	CHECK(sections.Any());
}

TEST_CASE("An empty or failed plain hover has nothing to show")
{
	Graphite::HoverState hover;
	hover.Info = Graphite::HoverInfo{};
	const Graphite::HoverTooltipLook look;

	CHECK_FALSE(Graphite::SectionsToShow(Graphite::HoverActions{.ShowInfo = true}, hover, {}, look).Any());
	CHECK_FALSE(Graphite::SectionsToShow(Graphite::HoverActions{.InfoGaveUp = true}, hover, {}, look).Any());
}

TEST_CASE("A hover that could not reach its server says why, once for both kinds of question")
{
	Graphite::HoverState hover;
	hover.InfoFailureText = "clangd not installed";
	hover.PreviewFailureText = "clangd not installed";
	const Graphite::HoverTooltipLook look;

	const Graphite::HoverSections sections =
		Graphite::SectionsToShow(Graphite::HoverActions{.InfoGaveUp = true, .PreviewGaveUp = true}, hover, {}, look);

	CHECK(sections.InfoFailure);
	CHECK_FALSE(sections.PreviewFailure);
}

TEST_CASE("A Ctrl+hover with no definition says so")
{
	Graphite::HoverState hover;
	hover.Preview = Graphite::DefinitionPreview{.Unreadable = true};
	hover.NoDefinitionFound = true;
	const Graphite::HoverTooltipLook look;

	const Graphite::HoverSections sections = Graphite::SectionsToShow(Graphite::HoverActions{.ShowPreview = true, .PreviewHeldByCtrl = true}, hover, {}, look);

	CHECK(sections.NoDefinition);
	CHECK_FALSE(sections.Preview);
}

TEST_CASE("A preview shown on every hover, without Ctrl, says nothing when it finds no definition")
{
	Graphite::HoverState hover;
	hover.Preview = Graphite::DefinitionPreview{.Unreadable = true};
	hover.NoDefinitionFound = true;
	const Graphite::HoverTooltipLook look;

	CHECK_FALSE(Graphite::SectionsToShow(Graphite::HoverActions{.ShowPreview = true}, hover, {}, look).Any());
}

TEST_CASE("Hover documentation is set at the interface size while the editor is at another")
{
	ImGuiFrames frames;
	TooltipFrame tooltip;
	tooltip.Actions.ShowInfo = true;

	Graphite::HoverInfo info;
	info.Documentation = "Draws the widget.";
	info.Parsed = true;
	tooltip.Hover.Info = info;

	float interfaceLineHeight = 0.0f;

	const auto draw = [&] {
		tooltip.Draw();
		interfaceLineHeight = InterfaceLineHeight();
	};

	frames.Next(draw);
	frames.Next(draw);

	const ImGuiWindow* window = HoverTooltipWindow();
	REQUIRE(window != nullptr);

	CHECK(tooltip.StackAfter == tooltip.StackBefore);
	CHECK(window->ContentSize.y == interfaceLineHeight);
}
