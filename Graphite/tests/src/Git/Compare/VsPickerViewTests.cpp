#include <doctest/doctest.h>

#include "ImGuiFrames.h"
#include "Git/Compare/VsPickerView.h"
#include "Git/Lines/LineReadFixtures.h"
#include "Theme/BuiltInSchemes.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <memory>
#include <optional>

using Graphite::BranchLines;
using Graphite::LineAnswerChange;
using Graphite::LineRead;
using Graphite::VsPickerView;
using Graphite::Fixtures::LineReadBuilder;

namespace {

	constexpr const char* kBranch = "refs/heads/feature/x";

	LineRead ReadWithFeature(bool withFeature)
	{
		LineReadBuilder builder;
		builder.AddRemote("origin");

		const std::size_t develop = builder.AddLocal("develop");
		builder.MakeBase(develop);

		if (withFeature)
		{
			const std::size_t feature = builder.AddLocal("feature/x");
			builder.Compare(feature, develop);
		}

		LineRead read = builder.Build();

		if (withFeature)
		{
			read.Model.Branches.back().Kind = Core::ComparedKind::Base;
			read.Model.Branches.back().AutomaticKind = Core::ComparedKind::Base;
			read.Model.Branches.back().AutomaticComparedWith = 0;
		}

		return read;
	}

	struct PickerHarness
	{
		ImGuiFrames Frames;
		Graphite::ResolvedScheme Scheme = Graphite::DarkScheme();
		LineRead Read = ReadWithFeature(true);
		std::unique_ptr<BranchLines> Lines = std::make_unique<BranchLines>(Read);
		VsPickerView View;
		std::optional<LineAnswerChange> Last;
		bool PopupOpen = false;

		void ReplaceRead(LineRead read)
		{
			Lines.reset();
			Read = std::move(read);
			Lines = std::make_unique<BranchLines>(Read);
		}

		void Draw()
		{
			const Graphite::WindowBarColours colours = Graphite::ReadWindowBarColours(Scheme);
			const std::function<std::optional<Core::LineDistance>(const Core::Oid&, const Core::Oid&)> distance =
				[](const Core::Oid&, const Core::Oid&) { return std::optional<Core::LineDistance>{}; };

			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(1000.0f, 700.0f));
			ImGui::Begin("VsPickerHarness", nullptr,
						 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

			const std::optional<LineAnswerChange> change =
				View.Draw(VsPickerView::Drawing{.Read = Read,
												.Lines = *Lines,
												.Now = LineReadBuilder::Now(),
												.DistanceBetween = distance,
												.Colours = colours,
												.RegionMin = ImVec2(0.0f, 0.0f),
												.RegionMax = ImVec2(1000.0f, 700.0f)});

			if (change.has_value())
			{
				Last = change;
			}

			PopupOpen = ImGui::IsPopupOpen(VsPickerView::kPopupId);
			ImGui::End();
		}

		void Next() { Frames.Next([&] { Draw(); }); }
		void Click(ImVec2 at) { Frames.Click(at, [&] { Draw(); }); }
		void PressKey(ImGuiKey key) { Frames.PressKey(key, [&] { Draw(); }); }

		void OpenAndSettle()
		{
			View.Open(kBranch, ImVec2(100.0f, 50.0f));
			Next();
			Next();
		}
	};
}

TEST_CASE("Git window: the picker opens under the point it is given and shows its popup")
{
	PickerHarness harness;

	harness.OpenAndSettle();

	CHECK(harness.View.IsOpen());
	CHECK(harness.PopupOpen);
}

TEST_CASE("Git window: the picker closes when its branch is gone")
{
	PickerHarness harness;
	harness.OpenAndSettle();
	REQUIRE(harness.PopupOpen);

	harness.ReplaceRead(ReadWithFeature(false));
	harness.Next();
	harness.Next();

	CHECK_FALSE(harness.View.IsOpen());
	CHECK_FALSE(harness.PopupOpen);
	CHECK_FALSE(harness.Last.has_value());
}

TEST_CASE("Git window: the picker closes on Escape")
{
	PickerHarness harness;
	harness.OpenAndSettle();
	REQUIRE(harness.PopupOpen);

	harness.PressKey(ImGuiKey_Escape);
	harness.Next();

	CHECK_FALSE(harness.View.IsOpen());
}

TEST_CASE("Git window: the picker closes on a click outside it")
{
	PickerHarness harness;
	harness.OpenAndSettle();
	REQUIRE(harness.PopupOpen);

	harness.Click(ImVec2(900.0f, 650.0f));
	harness.Next();

	CHECK_FALSE(harness.View.IsOpen());
	CHECK_FALSE(harness.Last.has_value());
}

TEST_CASE("Git window: clicking an option sends its change and closes the picker")
{
	PickerHarness harness;
	harness.OpenAndSettle();
	REQUIRE(harness.PopupOpen);

	const ImGuiContext& context = *ImGui::GetCurrentContext();
	REQUIRE(context.OpenPopupStack.Size > 0);
	const ImGuiWindow& popup = *context.OpenPopupStack.back().Window;
	const float x = popup.Pos.x + 40.0f;

	for (float y = popup.Pos.y; y < popup.Pos.y + popup.Size.y && !harness.Last.has_value(); y += 6.0f)
	{
		harness.Click(ImVec2(x, y));
	}

	REQUIRE(harness.Last.has_value());
	CHECK(harness.Last->What == LineAnswerChange::Kind::CompareAutomatically);
	CHECK(harness.Last->Ref == kBranch);
	CHECK_FALSE(harness.View.IsOpen());
}
