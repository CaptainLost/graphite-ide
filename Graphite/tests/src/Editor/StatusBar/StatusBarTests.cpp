#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "DrawnGlyphs.h"
#include "ImGuiFrames.h"
#include "TempDirectory.h"

#include "Editor/StatusBar/StatusBar.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/ChromePalette.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/Theme.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Text/DocumentSet.h"

#include <imgui_internal.h>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace {
	constexpr float kInsideBellInset = 3.0f;
	constexpr int kFramesPastTheHoverFade = 30;
	constexpr int kVerticesInAGlyph = 4;
	constexpr float kAntiAliasFringe = 1.0f;
	constexpr double kWithinAFringe = 0.05;
	constexpr float kSameSpot = 0.01f;

	constexpr ImU32 kErrorColour = IM_COL32(231, 17, 23, 255);
	constexpr ImU32 kWarningColour = IM_COL32(233, 181, 19, 255);
	constexpr ImU32 kReadyColour = IM_COL32(21, 199, 61, 255);

	float BarTextWidth(const char* text)
	{
		Core::PushInterfaceFont(Graphite::kStatusBarFontSize);
		const float width = ImGui::CalcTextSize(text).x;
		ImGui::PopFont();

		return width;
	}

	struct StatusBarScreen
	{
		ImGuiFrames Frames;
		Core::DocumentSet Documents;
		std::optional<std::size_t> Shown;
		Graphite::StatusMessages Messages;
		bool BellClicked = false;
		float WordsRight = 0.0f;
		float WordsWidth = 0.0f;
		float BarMiddle = 0.0f;
		Graphite::StatusBarClick LastClick = Graphite::StatusBarClick::None;

		StatusBarScreen()
		{
			Messages.Colours = Graphite::StatusBarColours{.Error = kErrorColour, .Warning = kWarningColour, .Ready = kReadyColour};
		}

		void Draw()
		{
			Core::Animation::BeginFrame();
			const Graphite::StatusBarClick clicked = Graphite::DrawStatusBar(Documents, Shown, Messages);
			if (clicked != Graphite::StatusBarClick::None)
			{
				LastClick = clicked;
			}
			if (clicked == Graphite::StatusBarClick::Notifications)
			{
				BellClicked = true;
			}
		}

		void DrawMeasuringWords(const char* words)
		{
			Draw();
			const ImGuiWindow& bar = Bar();
			const float bellWidth = bar.Size.y;
			WordsRight = bar.Pos.x + bar.Size.x - bar.WindowPadding.x - bellWidth - Core::Scaled(Graphite::kStatusItemGap);
			WordsWidth = BarTextWidth(words);
			BarMiddle = bar.Pos.y + bar.Size.y * 0.5f;
		}

		void HoverAt(ImVec2 at)
		{
			ImGui::GetIO().AddMousePosEvent(at.x, at.y);

			for (int frame = 0; frame < kFramesPastTheHoverFade; ++frame)
			{
				Frames.Next([&] { Draw(); });
			}
		}

		ImVec2 BellCentre() const
		{
			const ImGuiWindow& bar = Bar();
			const float bellRight = bar.Pos.x + bar.Size.x - bar.WindowPadding.x;
			return ImVec2(bellRight - kInsideBellInset, bar.Pos.y + bar.Size.y * 0.5f);
		}

		void Open(const std::filesystem::path& path)
		{
			REQUIRE(Documents.Open(path, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
			Shown = Documents.GetActiveIndex();
		}

		static const ImGuiWindow& Bar()
		{
			const ImGuiWindow* bar = ImGui::FindWindowByName(Graphite::kStatusBarWindowName);
			REQUIRE(bar != nullptr);
			return *bar;
		}

		static const ImDrawList& BarDrawing() { return *Bar().DrawList; }
	};

	ImU32 HoverFill()
	{
		return ImGui::GetColorU32(ImGuiCol_ButtonHovered);
	}

	bool Near(float value, float expected)
	{
		return value > expected - kSameSpot && value < expected + kSameSpot;
	}

	bool DrawsGlyphRun(const ImDrawList& drawing, const std::vector<ImRect>& run)
	{
		const std::vector<ImRect> drawn = GlyphQuads(drawing);

		if (run.empty())
		{
			return false;
		}

		for (std::size_t start = 0; start + run.size() <= drawn.size(); ++start)
		{
			bool matches = true;

			for (std::size_t glyph = 0; glyph < run.size() && matches; ++glyph)
			{
				const ImRect& candidate = drawn[start + glyph];
				const ImRect& expected = run[glyph];

				matches = Near(candidate.GetWidth(), expected.GetWidth()) && Near(candidate.GetHeight(), expected.GetHeight()) &&
						  Near(candidate.Min.x - drawn[start].Min.x, expected.Min.x - run.front().Min.x) &&
						  Near(candidate.Min.y - drawn[start].Min.y, expected.Min.y - run.front().Min.y);
			}

			if (matches)
			{
				return true;
			}
		}

		return false;
	}

	enum class RunFace
	{
		TheBarFace,
		TheCodeFaceAtTheBarSize,
		TheWindowFace
	};

	const char* ReferenceWindowFor(RunFace face)
	{
		switch (face)
		{
		case RunFace::TheBarFace:
			return "##BarFaceReference";
		case RunFace::TheCodeFaceAtTheBarSize:
			return "##CodeFaceReference";
		case RunFace::TheWindowFace:
			return "##WindowFaceReference";
		}

		return "##BarFaceReference";
	}

	std::vector<ImRect> GlyphRunOf(const char* text, RunFace face)
	{
		constexpr ImGuiWindowFlags kBareWindow = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
												 ImGuiWindowFlags_NoSavedSettings;

		constexpr ImVec2 kRoomForTheWholeRun(400.0f, 100.0f);

		ImGui::SetNextWindowSize(kRoomForTheWholeRun);
		ImGui::Begin(ReferenceWindowFor(face), nullptr, kBareWindow);

		if (face == RunFace::TheBarFace)
		{
			Core::PushInterfaceFont(Graphite::kStatusBarFontSize);
		}
		else if (face == RunFace::TheCodeFaceAtTheBarSize)
		{
			Core::PushCodeFont(Graphite::kStatusBarFontSize);
		}

		ImGui::TextUnformatted(text);

		if (face != RunFace::TheWindowFace)
		{
			ImGui::PopFont();
		}

		std::vector<ImRect> run = GlyphQuads(*ImGui::GetWindowDrawList());
		ImGui::End();

		return run;
	}

	const ImGuiWindow* ShownTooltip()
	{
		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if ((window->Flags & ImGuiWindowFlags_Tooltip) != 0 && window->Active)
			{
				return window;
			}
		}

		return nullptr;
	}

	// The tooltip shown under the pointer this frame, and its text as the bar's face and as the window's would draw it.
	struct TooltipRuns
	{
		const ImGuiWindow* Tooltip = nullptr;
		std::vector<ImRect> AtFifteen;
		std::vector<ImRect> AtTheWindowSize;
	};

	TooltipRuns DrawWithTooltip(StatusBarScreen& screen, const char* text)
	{
		TooltipRuns runs;
		screen.Frames.Next([&] {
			screen.Draw();
			runs.AtFifteen = GlyphRunOf(text, RunFace::TheBarFace);
			runs.AtTheWindowSize = GlyphRunOf(text, RunFace::TheWindowFace);
		});
		runs.Tooltip = ShownTooltip();

		return runs;
	}
}

TEST_CASE("A click on the bell at the status bar's right end reports the notifications")
{
	StatusBarScreen screen;
	screen.Frames.Next([&] { screen.Draw(); });

	screen.Frames.Click(screen.BellCentre(), [&] { screen.Draw(); });

	CHECK(screen.BellClicked);
}

TEST_CASE("The bell with a missed notification's dot completes a frame without an ImGui assertion")
{
	StatusBarScreen screen;
	screen.Messages.MissedNotificationColour = Core::Color{.R = 1.0f};
	screen.Messages.Problems = Graphite::ProblemCounts{.Errors = 2};

	screen.Frames.Next([&] { screen.Draw(); });
	screen.Frames.Next([&] { screen.Draw(); });

	CHECK_FALSE(screen.BellClicked);
}

TEST_CASE("The bell is in the status grey at rest, and in the text colour on the hover fill under the pointer")
{
	StatusBarScreen screen;
	screen.Frames.Next([&] { screen.Draw(); });
	screen.Frames.Next([&] { screen.Draw(); });
	const ImU32 dimColour = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text);

	CHECK(DrawsInColour(StatusBarScreen::BarDrawing(), Graphite::StatusBarTextColour()));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), textColour));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), dimColour));

	screen.HoverAt(screen.BellCentre());

	CHECK(DrawsInColour(StatusBarScreen::BarDrawing(), HoverFill()));
	CHECK(DrawsInColour(StatusBarScreen::BarDrawing(), textColour));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), Graphite::StatusBarTextColour()));
}

TEST_CASE("Halfway into the pointer's hover, the bell's glyph is between the status grey and the text colour, as its fill eases in")
{
	constexpr int kFramesIntoTheFade = 2;
	constexpr float kClearOfEitherEnd = 0.05f;

	StatusBarScreen screen;
	screen.Frames.Next([&] { screen.Draw(); });
	screen.Frames.Next([&] { screen.Draw(); });

	const ImVec2 bell = screen.BellCentre();
	ImGui::GetIO().AddMousePosEvent(bell.x, bell.y);

	for (int frame = 0; frame < kFramesIntoTheFade; ++frame)
	{
		screen.Frames.Next([&] { screen.Draw(); });
	}

	// With nothing else in the bar, the bell's is the one glyph drawn.
	const std::vector<DrawnGlyph> glyphs = DrawnGlyphs(StatusBarScreen::BarDrawing());
	REQUIRE(glyphs.size() == 1);

	const ImU32 statusGrey = Graphite::StatusBarTextColour();
	const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
	const float share = ShareFromTo(glyphs.front().Colour, statusGrey, text);
	CAPTURE(share);

	REQUIRE_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), HoverFill()));
	CHECK(share > kClearOfEitherEnd);
	CHECK(share < 1.0f - kClearOfEitherEnd);
}

TEST_CASE("The status bar's tooltips, an item's and the bell's, are drawn at the window's size and not at the bar's fifteen pixels")
{
	constexpr const char* kWords = "C#: Roslyn ready";
	constexpr const char* kDetail = "Roslyn ready for 3 projects";
	constexpr const char* kBellTooltip = "Notifications";

	StatusBarScreen screen;
	screen.Messages.Server = kWords;
	screen.Messages.ServerOpensSettings = true;
	screen.Messages.ServerDetail = kDetail;

	screen.Frames.Next([&] { screen.DrawMeasuringWords(kWords); });
	screen.HoverAt(ImVec2(screen.WordsRight - screen.WordsWidth * 0.5f, screen.BarMiddle));
	const TooltipRuns item = DrawWithTooltip(screen, kDetail);

	REQUIRE(item.Tooltip != nullptr);
	REQUIRE_FALSE(item.AtFifteen.empty());
	REQUIRE_FALSE(item.AtTheWindowSize.empty());
	REQUIRE(item.AtFifteen.front().GetHeight() > item.AtTheWindowSize.front().GetHeight());
	CHECK(DrawsGlyphRun(*item.Tooltip->DrawList, item.AtTheWindowSize));
	CHECK_FALSE(DrawsGlyphRun(*item.Tooltip->DrawList, item.AtFifteen));

	screen.HoverAt(screen.BellCentre());
	const TooltipRuns bell = DrawWithTooltip(screen, kBellTooltip);

	REQUIRE(bell.Tooltip != nullptr);
	CHECK(DrawsGlyphRun(*bell.Tooltip->DrawList, bell.AtTheWindowSize));
	CHECK_FALSE(DrawsGlyphRun(*bell.Tooltip->DrawList, bell.AtFifteen));
}

TEST_CASE("A click on the server's words reports the server when they open its settings")
{
	StatusBarScreen screen;
	screen.Messages.Server = "C#: Roslyn ready";
	screen.Messages.ServerOpensSettings = true;

	screen.Frames.Next([&] { screen.DrawMeasuringWords("C#: Roslyn ready"); });
	screen.Frames.Click(ImVec2(screen.WordsRight - screen.WordsWidth * 0.5f, screen.BarMiddle),
						[&] { screen.DrawMeasuringWords("C#: Roslyn ready"); });

	CHECK(screen.LastClick == Graphite::StatusBarClick::Server);
}

TEST_CASE("Server words that open nothing take no click")
{
	StatusBarScreen screen;
	screen.Messages.Server = "Restricted mode";

	screen.Frames.Next([&] { screen.DrawMeasuringWords("Restricted mode"); });
	screen.Frames.Click(ImVec2(screen.WordsRight - screen.WordsWidth * 0.5f, screen.BarMiddle),
						[&] { screen.DrawMeasuringWords("Restricted mode"); });

	CHECK(screen.LastClick == Graphite::StatusBarClick::None);
}

TEST_CASE("Problems and the background pass beside clickable server words complete a frame without an ImGui assertion")
{
	StatusBarScreen screen;
	screen.Messages.Problems = Graphite::ProblemCounts{.Errors = 2};
	screen.Messages.Server = "C++: clangd indexing 40%";
	screen.Messages.ServerOpensSettings = true;
	screen.Messages.Health = Graphite::ServerHealth::Starting;
	screen.Messages.Background = "analysing workspace: 42 of 385";

	screen.Frames.Next([&] { screen.Draw(); });
	screen.Frames.Next([&] { screen.Draw(); });

	CHECK(screen.LastClick == Graphite::StatusBarClick::None);
}

TEST_CASE("The status bar is twenty-three pixels and draws its words at fifteen")
{
	constexpr float kTwentyThree = 23.0f;
	constexpr float kFifteen = 15.0f;
	constexpr const char* kWords = "C++: clangd ready";

	StatusBarScreen screen;
	screen.Messages.Server = kWords;

	std::vector<ImRect> atFifteen;
	std::vector<ImRect> atTheWindowSize;
	screen.Frames.Next([&] {
		screen.Draw();
		atFifteen = GlyphRunOf(kWords, RunFace::TheBarFace);
		atTheWindowSize = GlyphRunOf(kWords, RunFace::TheWindowFace);
	});

	CHECK(Graphite::kStatusBarFontSize == kFifteen);
	CHECK(StatusBarScreen::Bar().Size.y == doctest::Approx(kTwentyThree));

	REQUIRE_FALSE(atFifteen.empty());
	REQUIRE_FALSE(atTheWindowSize.empty());
	REQUIRE(atFifteen.front().GetHeight() > atTheWindowSize.front().GetHeight());
	CHECK(DrawsGlyphRun(StatusBarScreen::BarDrawing(), atFifteen));
	CHECK_FALSE(DrawsGlyphRun(StatusBarScreen::BarDrawing(), atTheWindowSize));
}

TEST_CASE("The status bar keeps its pinned twenty-three pixels whatever the frame padding")
{
	constexpr float kTwentyThree = 23.0f;

	for (const float framePaddingY : {4.0f, 5.0f, 9.0f})
	{
		CAPTURE(framePaddingY);

		StatusBarScreen screen;
		ImGui::GetStyle().FramePadding.y = framePaddingY;
		screen.Frames.Next([&] { screen.Draw(); });

		CHECK(StatusBarScreen::Bar().Size.y == doctest::Approx(kTwentyThree));
	}
}

TEST_CASE("The status bar draws no border of its own, so it sits flush with the chrome above it")
{
	StatusBarScreen screen;
	screen.Frames.Next([&] { screen.Draw(); });

	CHECK(StatusBarScreen::Bar().WindowBorderSize == doctest::Approx(0.0f));
}

TEST_CASE("Problem counts draw as icons with their counts in their colours and zero counts are hidden")
{
	StatusBarScreen screen;

	screen.Messages.Problems = Graphite::ProblemCounts{.Errors = 2, .Warnings = 0};
	screen.Frames.Next([&] { screen.Draw(); });

	const int glyphAndOneDigit = 2 * kVerticesInAGlyph;
	CHECK(VerticesInColour(StatusBarScreen::BarDrawing(), kErrorColour) == glyphAndOneDigit);
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), kWarningColour));

	screen.Messages.Problems = Graphite::ProblemCounts{.Errors = 0, .Warnings = 13};
	screen.Frames.Next([&] { screen.Draw(); });

	const int glyphAndTwoDigits = 3 * kVerticesInAGlyph;
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), kErrorColour));
	CHECK(VerticesInColour(StatusBarScreen::BarDrawing(), kWarningColour) == glyphAndTwoDigits);

	screen.Messages.Problems = Graphite::ProblemCounts{.Errors = 4, .Warnings = 1};
	screen.Frames.Next([&] { screen.Draw(); });

	CHECK(VerticesInColour(StatusBarScreen::BarDrawing(), kErrorColour) == glyphAndOneDigit);
	CHECK(VerticesInColour(StatusBarScreen::BarDrawing(), kWarningColour) == glyphAndOneDigit);

	const std::optional<HorizontalExtent> errors = HorizontalExtentInColour(StatusBarScreen::BarDrawing(), kErrorColour);
	const std::optional<HorizontalExtent> warnings = HorizontalExtentInColour(StatusBarScreen::BarDrawing(), kWarningColour);
	REQUIRE(errors.has_value());
	REQUIRE(warnings.has_value());
	CHECK(errors->Right < warnings->Left);
}

TEST_CASE("No problems draw a dim check")
{
	StatusBarScreen screen;
	screen.Messages.Problems = Graphite::ProblemCounts{};

	screen.Frames.Next([&] { screen.Draw(); });

	const ImU32 dimColour = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	CHECK(VerticesInColour(StatusBarScreen::BarDrawing(), dimColour) == kVerticesInAGlyph);
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), kErrorColour));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), kWarningColour));
}

TEST_CASE("The server dot takes the health's colour")
{
	StatusBarScreen screen;
	constexpr const char* kWords = "C++: clangd ready";
	screen.Messages.Server = kWords;

	const auto drawnWith = [&screen](Graphite::ServerHealth health) {
		screen.Messages.Health = health;
		screen.Frames.Next([&] { screen.DrawMeasuringWords(kWords); });
	};

	drawnWith(Graphite::ServerHealth::Ready);

	CHECK(FillsMiddleInColour(StatusBarScreen::BarDrawing(), kReadyColour));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), kWarningColour));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), kErrorColour));

	const std::optional<HorizontalExtent> across = HorizontalExtentInColour(StatusBarScreen::BarDrawing(), kReadyColour);
	const std::optional<VerticalExtent> down = VerticalExtentInColour(StatusBarScreen::BarDrawing(), kReadyColour);
	REQUIRE(across.has_value());
	REQUIRE(down.has_value());

	const float solidDiameter = Core::Scaled(Graphite::kServerDotDiameter) - kAntiAliasFringe;
	CHECK(across->Right - across->Left == doctest::Approx(solidDiameter).epsilon(kWithinAFringe));
	CHECK(down->Bottom - down->Top == doctest::Approx(solidDiameter).epsilon(kWithinAFringe));
	CHECK(std::fabs((down->Top + down->Bottom) * 0.5f - screen.BarMiddle) < kAntiAliasFringe * 0.5f);
	CHECK(across->Right < screen.WordsRight - screen.WordsWidth);

	drawnWith(Graphite::ServerHealth::Starting);

	CHECK(FillsMiddleInColour(StatusBarScreen::BarDrawing(), kWarningColour));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), kReadyColour));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), kErrorColour));

	drawnWith(Graphite::ServerHealth::Limited);

	CHECK(FillsMiddleInColour(StatusBarScreen::BarDrawing(), kWarningColour));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), kReadyColour));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), kErrorColour));

	drawnWith(Graphite::ServerHealth::Failed);

	CHECK(FillsMiddleInColour(StatusBarScreen::BarDrawing(), kErrorColour));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), kReadyColour));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), kWarningColour));

	drawnWith(Graphite::ServerHealth::None);

	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), kReadyColour));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), kWarningColour));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), kErrorColour));
}

TEST_CASE("A clickable item fills on hover")
{
	StatusBarScreen screen;
	constexpr const char* kWords = "C#: Roslyn ready";
	screen.Messages.Server = kWords;
	screen.Messages.ServerOpensSettings = true;

	screen.Frames.Next([&] { screen.DrawMeasuringWords(kWords); });
	screen.HoverAt(ImVec2(screen.WordsRight - screen.WordsWidth * 0.5f, screen.BarMiddle));

	const ImGuiWindow& bar = StatusBarScreen::Bar();
	const std::optional<VerticalExtent> wordsFillDown = VerticalExtentInColour(StatusBarScreen::BarDrawing(), HoverFill());
	const std::optional<HorizontalExtent> wordsFillAcross = HorizontalExtentInColour(StatusBarScreen::BarDrawing(), HoverFill());
	REQUIRE(wordsFillDown.has_value());
	REQUIRE(wordsFillAcross.has_value());
	CHECK(wordsFillDown->Top == doctest::Approx(bar.Pos.y));
	CHECK(wordsFillDown->Bottom == doctest::Approx(bar.Pos.y + bar.Size.y));
	CHECK(wordsFillAcross->Left < screen.WordsRight - screen.WordsWidth);
	CHECK(wordsFillAcross->Right > screen.WordsRight);

	screen.HoverAt(screen.BellCentre());

	const std::optional<VerticalExtent> bellFillDown = VerticalExtentInColour(StatusBarScreen::BarDrawing(), HoverFill());
	REQUIRE(bellFillDown.has_value());
	CHECK(bellFillDown->Top == doctest::Approx(bar.Pos.y));
	CHECK(bellFillDown->Bottom == doctest::Approx(bar.Pos.y + bar.Size.y));
}

TEST_CASE("Server words that open nothing take no fill under the pointer")
{
	StatusBarScreen screen;
	constexpr const char* kWords = "Restricted mode";
	screen.Messages.Server = kWords;

	screen.Frames.Next([&] { screen.DrawMeasuringWords(kWords); });
	screen.HoverAt(ImVec2(screen.WordsRight - screen.WordsWidth * 0.5f, screen.BarMiddle));

	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), HoverFill()));
}

TEST_CASE("Items are separated by the named gap")
{
	StatusBarScreen screen;
	constexpr const char* kBranch = "main";
	constexpr const char* kChanges = "3 changes";
	constexpr const char* kWords = "C#: Roslyn ready";
	screen.Messages.Git = Graphite::GitStatusSegment{.Branch = kBranch, .Changes = kChanges, .HasRepository = true};
	screen.Messages.Server = kWords;
	screen.Messages.ServerOpensSettings = true;

	float branchWidth = 0.0f;
	float changesWidth = 0.0f;
	screen.Frames.Next([&] {
		screen.DrawMeasuringWords(kWords);
		branchWidth = BarTextWidth(kBranch);
		changesWidth = BarTextWidth(kChanges);
	});

	const ImGuiWindow& bar = StatusBarScreen::Bar();
	const float gap = Core::Scaled(Graphite::kStatusItemGap);
	const float branchLeft = bar.Pos.x + bar.WindowPadding.x;

	screen.HoverAt(ImVec2(branchLeft + branchWidth * 0.5f, screen.BarMiddle));
	const std::optional<HorizontalExtent> branchFill = HorizontalExtentInColour(StatusBarScreen::BarDrawing(), HoverFill());
	REQUIRE(branchFill.has_value());

	const float fillPadding = (branchFill->Right - branchFill->Left - branchWidth) * 0.5f;
	CHECK(fillPadding > 0.0f);
	CHECK(fillPadding < gap * 0.5f);
	CHECK(branchFill->Left + fillPadding == doctest::Approx(branchLeft));

	screen.HoverAt(ImVec2(branchLeft + branchWidth + gap + changesWidth * 0.5f, screen.BarMiddle));
	const std::optional<HorizontalExtent> changesFill = HorizontalExtentInColour(StatusBarScreen::BarDrawing(), HoverFill());
	REQUIRE(changesFill.has_value());

	CHECK((changesFill->Left + fillPadding) - (branchFill->Right - fillPadding) == doctest::Approx(gap));

	screen.HoverAt(ImVec2(screen.WordsRight - screen.WordsWidth * 0.5f, screen.BarMiddle));
	const std::optional<HorizontalExtent> wordsFill = HorizontalExtentInColour(StatusBarScreen::BarDrawing(), HoverFill());
	REQUIRE(wordsFill.has_value());

	screen.HoverAt(screen.BellCentre());
	const std::optional<HorizontalExtent> bellFill = HorizontalExtentInColour(StatusBarScreen::BarDrawing(), HoverFill());
	REQUIRE(bellFill.has_value());

	CHECK(bellFill->Left - (wordsFill->Right - fillPadding) == doctest::Approx(gap));
}

TEST_CASE("The status bar's text is the status grey, in dark and in light")
{
	constexpr float kStatusGreyShare = 0.47f;
	constexpr float kShareTolerance = 0.03f;
	constexpr ImU32 kMockupStatusGrey = IM_COL32(0xaa, 0xb1, 0xbb, 0xff);

	for (const Graphite::ResolvedScheme* scheme : {&Graphite::DarkScheme(), &Graphite::LightScheme()})
	{
		StatusBarScreen screen;
		const Core::ChromePalette palette = Graphite::ChromePaletteFor(*scheme);
		CAPTURE(palette.Background.R);
		Core::Theme::Apply(palette);
		screen.Messages.Git = Graphite::GitStatusSegment{.Branch = "main", .Distance = "2", .Changes = "3 changes", .HasRepository = true};
		screen.Messages.Server = "C++: clangd ready";
		screen.Messages.Background = "analysing workspace";

		ImU32 statusGrey = 0;
		screen.Frames.Next([&] {
			screen.Draw();
			statusGrey = Graphite::StatusBarTextColour();
		});

		const ImU32 text = ImGui::GetColorU32(Core::ToImVec4(palette.Text));
		const ImU32 disabled = ImGui::GetColorU32(Core::ToImVec4(palette.TextDisabled));

		CHECK(ShareFromTo(statusGrey, text, disabled) == doctest::Approx(kStatusGreyShare).epsilon(kShareTolerance));
		CHECK(DrawsInColour(StatusBarScreen::BarDrawing(), statusGrey));
		CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), text));

		if (!Core::IsLightBackground(palette.Background))
		{
			CHECK(WithinOneLevel(statusGrey, kMockupStatusGrey));
		}
	}
}

TEST_CASE("A clickable item's words turn to the text colour under the pointer, and back to the status grey after")
{
	constexpr const char* kBranch = "main";
	constexpr int kGlyphsInTheBranch = 4;

	StatusBarScreen screen;
	screen.Messages.Git = Graphite::GitStatusSegment{.Branch = kBranch, .Changes = "3 changes", .HasRepository = true};

	float branchWidth = 0.0f;
	screen.Frames.Next([&] {
		screen.Draw();
		branchWidth = BarTextWidth(kBranch);
	});

	const ImGuiWindow& bar = StatusBarScreen::Bar();
	const float branchLeft = bar.Pos.x + bar.WindowPadding.x;
	const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);

	screen.HoverAt(ImVec2(branchLeft + branchWidth * 0.5f, bar.Pos.y + bar.Size.y * 0.5f));

	CHECK(VerticesInColour(StatusBarScreen::BarDrawing(), text) == kGlyphsInTheBranch * kVerticesInAGlyph);
	CHECK(DrawsInColour(StatusBarScreen::BarDrawing(), Graphite::StatusBarTextColour()));

	constexpr ImVec2 kFarFromTheBar(10.0f, 10.0f);
	screen.HoverAt(kFarFromTheBar);

	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), text));
}

TEST_CASE("A clickable item's fill reaches seven pixels past its words on each side")
{
	constexpr float kSeven = 7.0f;
	constexpr const char* kBranch = "main";

	StatusBarScreen screen;
	screen.Messages.Git = Graphite::GitStatusSegment{.Branch = kBranch, .HasRepository = true};

	float branchWidth = 0.0f;
	screen.Frames.Next([&] {
		screen.Draw();
		branchWidth = BarTextWidth(kBranch);
	});

	const ImGuiWindow& bar = StatusBarScreen::Bar();
	const float branchLeft = bar.Pos.x + bar.WindowPadding.x;
	screen.HoverAt(ImVec2(branchLeft + branchWidth * 0.5f, bar.Pos.y + bar.Size.y * 0.5f));

	const std::optional<HorizontalExtent> fill = HorizontalExtentInColour(StatusBarScreen::BarDrawing(), HoverFill());
	REQUIRE(fill.has_value());
	CHECK(fill->Left == doctest::Approx(branchLeft - kSeven));
	CHECK(fill->Right == doctest::Approx(branchLeft + branchWidth + kSeven));
}

TEST_CASE("The server's ready dot is the mockup's green on a dark chrome and a deeper green on a light one")
{
	constexpr ImU32 kMockupReadyGreen = IM_COL32(0x7f, 0xbf, 0x6a, 0xff);
	constexpr ImU32 kDeeperLightGreen = IM_COL32(32, 96, 32, 255);

	CHECK(Graphite::ServerReadyColour(Graphite::ChromePaletteFor(Graphite::DarkScheme()).Background) == kMockupReadyGreen);
	CHECK(Graphite::ServerReadyColour(Graphite::ChromePaletteFor(Graphite::LightScheme()).Background) == kDeeperLightGreen);
}

TEST_CASE("The caret position is drawn in the code face at the bar's size, whatever the interface size")
{
	constexpr float kInterfaceFontSize = 20.0f;
	constexpr const char* kCaretAtTheStart = "Ln 1, Col 1";

	TempDirectory temp;
	StatusBarScreen screen;
	screen.Open(temp.MakeFile("notes.txt", "abc\n"));
	ImGui::GetStyle().FontSizeBase = kInterfaceFontSize;

	std::vector<ImRect> atTheBarSize;
	std::vector<ImRect> atTheInterfaceSize;

	screen.Frames.Next([&] {
		screen.Draw();
		atTheBarSize = GlyphRunOf(kCaretAtTheStart, RunFace::TheCodeFaceAtTheBarSize);
		atTheInterfaceSize = GlyphRunOf(kCaretAtTheStart, RunFace::TheWindowFace);
	});

	constexpr std::size_t kGlyphsInTheRun = 8;
	REQUIRE(atTheBarSize.size() == kGlyphsInTheRun);
	REQUIRE(atTheInterfaceSize.size() == kGlyphsInTheRun);
	REQUIRE(atTheBarSize.front().GetHeight() < atTheInterfaceSize.front().GetHeight());
	CHECK(DrawsGlyphRun(StatusBarScreen::BarDrawing(), atTheBarSize));
	CHECK_FALSE(DrawsGlyphRun(StatusBarScreen::BarDrawing(), atTheInterfaceSize));
}

TEST_CASE("The read-only warning is an item of its own in the warning colour, only for such a document")
{
	TempDirectory temp;

	{
		StatusBarScreen readOnly;
		readOnly.Open(temp.MakeFile("broken.txt", "ab\xff"));
		REQUIRE(readOnly.Documents.At(*readOnly.Shown).Document->IsReadOnly());
		readOnly.Frames.Next([&] { readOnly.Draw(); });

		CHECK(DrawsInColour(StatusBarScreen::BarDrawing(), kWarningColour));
	}

	{
		StatusBarScreen writable;
		writable.Open(temp.MakeFile("fine.txt", "abc"));
		REQUIRE_FALSE(writable.Documents.At(*writable.Shown).Document->IsReadOnly());
		writable.Frames.Next([&] { writable.Draw(); });

		CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), kWarningColour));
	}
}

TEST_CASE("A wheel over the status bar leaves its items where they are")
{
	TempDirectory temp;
	StatusBarScreen screen;
	screen.Open(temp.MakeFile("notes.txt", "abc\n"));
	screen.Messages.Server = "C++: clangd ready";
	screen.Messages.ServerOpensSettings = true;

	screen.Frames.Next([&] { screen.DrawMeasuringWords("C++: clangd ready"); });

	constexpr float kOneNotchDown = -1.0f;
	ImGui::GetIO().AddMousePosEvent(screen.WordsRight - screen.WordsWidth * 0.5f, screen.BarMiddle);
	screen.Frames.Next([&] { screen.Draw(); });
	ImGui::GetIO().AddMouseWheelEvent(0.0f, kOneNotchDown);
	screen.Frames.Next([&] { screen.Draw(); });
	screen.Frames.Next([&] { screen.Draw(); });

	CHECK(StatusBarScreen::Bar().Scroll.y == doctest::Approx(0.0f));
}

TEST_CASE("The status bar no longer shows the document path")
{
	constexpr const char* kFileName = "notes.txt";

	TempDirectory temp;
	StatusBarScreen screen;
	screen.Open(temp.MakeFile(kFileName, "abc\n"));

	std::vector<ImRect> fileName;
	screen.Frames.Next([&] {
		screen.Draw();
		fileName = GlyphRunOf(kFileName, RunFace::TheBarFace);
	});

	REQUIRE_FALSE(fileName.empty());
	CHECK_FALSE(DrawsGlyphRun(StatusBarScreen::BarDrawing(), fileName));
}

TEST_CASE("The read-only warning still shows in the status bar, where the path stood")
{
	constexpr const char* kReadOnlyWarning = "read-only: not valid text for its encoding";

	TempDirectory temp;
	StatusBarScreen screen;
	screen.Open(temp.MakeFile("broken.txt", "ab\xff"));
	REQUIRE(screen.Documents.At(*screen.Shown).Document->IsReadOnly());

	std::vector<ImRect> warning;
	screen.Frames.Next([&] {
		screen.Draw();
		warning = GlyphRunOf(kReadOnlyWarning, RunFace::TheBarFace);
	});

	CHECK(DrawsGlyphRun(StatusBarScreen::BarDrawing(), warning));

	const std::optional<HorizontalExtent> drawn = HorizontalExtentInColour(StatusBarScreen::BarDrawing(), kWarningColour);
	REQUIRE(drawn.has_value());

	const ImGuiWindow& bar = StatusBarScreen::Bar();
	CHECK(drawn->Left < bar.Pos.x + bar.WindowPadding.x + Core::Scaled(Graphite::kStatusItemGap));
}
