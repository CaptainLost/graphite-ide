#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "ImGuiFrames.h"
#include "TempDirectory.h"

#include "Editor/StatusBar/StatusBar.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Text/DocumentSet.h"

#include <imgui_internal.h>

#include <cmath>
#include <cstddef>
#include <filesystem>
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
			WordsRight = bar.Pos.x + bar.Size.x - bar.WindowPadding.x - Core::IconButtonSize(Core::IconButtonFit::Bar) -
						 Core::Scaled(Graphite::kStatusItemGap);
			WordsWidth = ImGui::CalcTextSize(words).x;
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

	bool IsGlyphVertex(const ImDrawVert& vertex)
	{
		const ImVec2 white = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
		return vertex.uv.x != white.x || vertex.uv.y != white.y;
	}

	std::vector<ImRect> GlyphQuads(const ImDrawList& drawing)
	{
		std::vector<ImRect> quads;
		int first = 0;

		while (first + kVerticesInAGlyph <= drawing.VtxBuffer.Size)
		{
			const ImDrawVert* quad = &drawing.VtxBuffer[first];
			const bool allGlyph = IsGlyphVertex(quad[0]) && IsGlyphVertex(quad[1]) && IsGlyphVertex(quad[2]) && IsGlyphVertex(quad[3]);
			const bool isRectangle = quad[0].pos.y == quad[1].pos.y && quad[1].pos.x == quad[2].pos.x &&
									 quad[2].pos.y == quad[3].pos.y && quad[3].pos.x == quad[0].pos.x;

			if (allGlyph && isRectangle)
			{
				quads.emplace_back(quad[0].pos, quad[2].pos);
				first += kVerticesInAGlyph;
				continue;
			}

			++first;
		}

		return quads;
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
		TheCodeFaceAtTheInterfaceSize
	};

	std::vector<ImRect> GlyphRunOf(const char* text, RunFace face)
	{
		const bool inTheCodeFace = face == RunFace::TheCodeFaceAtTheInterfaceSize;
		constexpr ImGuiWindowFlags kBareWindow = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
												 ImGuiWindowFlags_NoSavedSettings;

		constexpr ImVec2 kRoomForTheWholeRun(400.0f, 100.0f);

		ImGui::SetNextWindowSize(kRoomForTheWholeRun);
		ImGui::Begin(inTheCodeFace ? "##CodeFaceReference" : "##BarFaceReference", nullptr, kBareWindow);

		if (inTheCodeFace)
		{
			Core::PushCodeFont(Core::kInterfaceFontSize);
		}

		ImGui::TextUnformatted(text);

		if (inTheCodeFace)
		{
			ImGui::PopFont();
		}

		std::vector<ImRect> run = GlyphQuads(*ImGui::GetWindowDrawList());
		ImGui::End();

		return run;
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

TEST_CASE("The bell is a glyph button like any other: the text colour at rest, on the hover fill under the pointer")
{
	StatusBarScreen screen;
	screen.Frames.Next([&] { screen.Draw(); });
	screen.Frames.Next([&] { screen.Draw(); });
	const ImU32 dimColour = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text);

	CHECK(DrawsInColour(StatusBarScreen::BarDrawing(), textColour));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), dimColour));

	screen.HoverAt(screen.BellCentre());

	CHECK(DrawsInColour(StatusBarScreen::BarDrawing(), HoverFill()));
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

	screen.Frames.Next([&] { screen.DrawMeasuringWords(kWords); });

	const ImGuiWindow& bar = StatusBarScreen::Bar();
	const float gap = Core::Scaled(Graphite::kStatusItemGap);
	const float branchLeft = bar.Pos.x + bar.WindowPadding.x;
	const float branchWidth = ImGui::CalcTextSize(kBranch).x;
	const float changesWidth = ImGui::CalcTextSize(kChanges).x;

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

TEST_CASE("The caret position is drawn at the interface size in the code face")
{
	constexpr float kBarFontSize = 20.0f;
	constexpr const char* kCaretAtTheStart = "Ln 1, Col 1";

	TempDirectory temp;
	StatusBarScreen screen;
	screen.Open(temp.MakeFile("notes.txt", "abc\n"));
	ImGui::GetStyle().FontSizeBase = kBarFontSize;

	std::vector<ImRect> atTheInterfaceSize;
	std::vector<ImRect> atTheBarSize;

	screen.Frames.Next([&] {
		screen.Draw();
		atTheInterfaceSize = GlyphRunOf(kCaretAtTheStart, RunFace::TheCodeFaceAtTheInterfaceSize);
		atTheBarSize = GlyphRunOf(kCaretAtTheStart, RunFace::TheBarFace);
	});

	constexpr std::size_t kGlyphsInTheRun = 8;
	REQUIRE(atTheInterfaceSize.size() == kGlyphsInTheRun);
	REQUIRE(atTheBarSize.size() == kGlyphsInTheRun);
	REQUIRE(atTheInterfaceSize.front().GetHeight() < atTheBarSize.front().GetHeight());
	CHECK(DrawsGlyphRun(StatusBarScreen::BarDrawing(), atTheInterfaceSize));
	CHECK_FALSE(DrawsGlyphRun(StatusBarScreen::BarDrawing(), atTheBarSize));
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
