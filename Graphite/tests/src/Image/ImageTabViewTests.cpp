#include <doctest/doctest.h>

#include "DeferredJobs.h"
#include "DrawnGlyphs.h"
#include "FakeImageTexture.h"
#include "ImGuiFrames.h"
#include "ImageFixtures.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Editor/Breadcrumb.h"
#include "Editor/EditorSettings.h"
#include "Image/ImageTab.h"
#include "Image/ImageTabView.h"
#include "Image/ImageTools.h"
#include "Image/Zoom.h"

#include "Core/ImGui/UiScale.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

using Graphite::ImageTab;
using Graphite::ImageTabState;
using Graphite::PictureBackground;
using Graphite::PictureZoom;
using Graphite::ViewStatusItem;

namespace {

	using Clock = std::chrono::steady_clock;

	constexpr ImVec2 kWindowSize(600.0f, 400.0f);
	constexpr ImVec2 kReferencePlace(0.0f, 500.0f);
	constexpr ImVec2 kFarAway(1200.0f, 700.0f);
	constexpr ImVec2 kPictureSize(2.0f, 2.0f);
	constexpr float kHalf = 0.5f;
	constexpr float kLargeZoom = 32.0f;
	constexpr int kToolButtonCount = 8;
	constexpr int kFramesPastTheLoaderWait = 15;
	constexpr const char* kWindow = "Image";
	constexpr const char* kReferenceWindow = "Reference";

	template<std::size_t Count>
	std::string Contents(const std::uint8_t (&bytes)[Count])
	{
		return std::string(reinterpret_cast<const char*>(bytes), Count);
	}

	std::optional<std::size_t> FindRun(const std::vector<DrawnGlyph>& drawn, const std::vector<DrawnGlyph>& run)
	{
		if (run.empty())
		{
			return std::nullopt;
		}

		const auto found = std::search(drawn.begin(), drawn.end(), run.begin(), run.end(),
									   [](const DrawnGlyph& a, const DrawnGlyph& b) { return a.Uv.x == b.Uv.x && a.Uv.y == b.Uv.y; });

		if (found == drawn.end())
		{
			return std::nullopt;
		}

		return static_cast<std::size_t>(found - drawn.begin());
	}

	// The rectangle the picture's texture is drawn into, when it is drawn at all.
	std::optional<ImRect> DrawnPicture(const ImDrawList& drawing, ImTextureID texture)
	{
		for (const ImDrawCmd& command : drawing.CmdBuffer)
		{
			if (command.UserCallback != nullptr || command.ElemCount == 0 || !(command.TexRef == ImTextureRef(texture)))
			{
				continue;
			}

			ImRect bounds(ImVec2(FLT_MAX, FLT_MAX), ImVec2(-FLT_MAX, -FLT_MAX));

			for (unsigned int element = 0; element < command.ElemCount; ++element)
			{
				bounds.Add(drawing.VtxBuffer[static_cast<int>(command.VtxOffset + drawing.IdxBuffer[static_cast<int>(command.IdxOffset + element)])].pos);
			}

			return bounds;
		}

		return std::nullopt;
	}

	struct ImageScreen
	{
		explicit ImageScreen(const std::string& fileName, const std::string& contents = {})
			: File(contents.empty() ? Directory.GetPath() / fileName : Directory.MakeFile(fileName, contents)),
			  Tab(File, Jobs.Submitter(), Textures)
		{
			Path.FileName = fileName;
			Path.Kind = Graphite::FileKind::Image;
		}

		void DrawWindow()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(Width, kWindowSize.y));
			ImGui::Begin(kWindow, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

			Row = ImRect(ImGui::GetCurrentWindow()->InnerRect.Min,
						 ImVec2(ImGui::GetCurrentWindow()->InnerRect.Max.x,
								ImGui::GetCurrentWindow()->InnerRect.Min.y + Graphite::BreadcrumbHeight()));
			ToolsWidth = Graphite::ImageToolsWidth();
			Last = Graphite::DrawImageTab(Tab, Path, Settings);
			Glyphs = DrawnGlyphs(*ImGui::GetWindowDrawList());
			Picture = DrawnPicture(*ImGui::GetWindowDrawList(), FakeTextureFactory::kFirstTextureId);

			ImGui::End();

			if (!Reference.empty())
			{
				ImGui::SetNextWindowPos(kReferencePlace);
				ImGui::Begin(kReferenceWindow, nullptr,
							 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
								 ImGuiWindowFlags_NoFocusOnAppearing);
				ImGui::TextUnformatted(Reference.c_str());
				ReferenceGlyphs = DrawnGlyphs(*ImGui::GetWindowDrawList());
				ImGui::End();
			}
		}

		std::function<void()> Drawing()
		{
			return [this] { DrawWindow(); };
		}

		void Draw() { Frames.Next(Drawing()); }

		void MoveTo(ImVec2 at)
		{
			ImGui::GetIO().AddMousePosEvent(at.x, at.y);
			Draw();
		}

		// As the document windows run it: the tab is updated as shown before it draws, which starts its load.
		void Update() { Tab.Update(Clock::now(), true); }

		void Land()
		{
			Update();
			Jobs.RunAll();
			Update();
			Draw();
			Draw();
		}

		// Whether `text` is drawn in the window, matched glyph for glyph against the same text drawn in a window of
		// its own in the same frames.
		std::optional<ImRect> FindText(const std::string& text)
		{
			Reference = text;
			Draw();
			Draw();
			Reference.clear();

			const std::optional<std::size_t> start = FindRun(Glyphs, ReferenceGlyphs);

			if (!start)
			{
				return std::nullopt;
			}

			ImRect box = Glyphs[*start].Box;

			for (std::size_t glyph = *start; glyph < *start + ReferenceGlyphs.size(); ++glyph)
			{
				box.Add(Glyphs[glyph].Box);
			}

			return box;
		}

		ImVec2 RowMiddle() const { return ImVec2(Row.GetCenter().x, Row.GetCenter().y); }

		float ToolsLeft() const { return Row.Max.x - Core::Scaled(Graphite::kBreadcrumbLeftPadding) - ToolsWidth; }

		ImGuiFrames Frames;
		TempDirectory Directory;
		TempAppSettings Store;
		Graphite::EditorSettings Settings{Store.Get()};
		DeferredJobs Jobs;
		FakeTextureFactory Textures;
		std::filesystem::path File;
		ImageTab Tab;
		Graphite::BreadcrumbModel Path;

		Graphite::ImageTabFrame Last;
		ImRect Row;
		float ToolsWidth = 0.0f;
		float Width = kWindowSize.x;
		std::vector<DrawnGlyph> Glyphs;
		std::optional<ImRect> Picture;

		std::string Reference;
		std::vector<DrawnGlyph> ReferenceGlyphs;
	};

	struct Span
	{
		float From = 0.0f;
		float To = 0.0f;

		float Middle() const { return (From + To) * kHalf; }
	};

	// The items along a line, found by moving the pointer across it, as ImGui reports the item under it.
	std::vector<Span> HoveredSpans(ImageScreen& screen, ImVec2 from, ImVec2 step, int steps)
	{
		std::vector<Span> spans;
		ImGuiID inside = 0;
		float start = 0.0f;

		for (int index = 0; index <= steps; ++index)
		{
			const ImVec2 at(from.x + step.x * static_cast<float>(index), from.y + step.y * static_cast<float>(index));
			const float along = step.x != 0.0f ? at.x : at.y;
			screen.MoveTo(at);
			const ImGuiID hovered = ImGui::GetCurrentContext()->HoveredId;

			if (hovered != inside)
			{
				if (inside != 0)
				{
					spans.push_back(Span{start, along});
				}

				start = along;
			}

			inside = hovered;
		}

		return spans;
	}

	ImRect PlacedPicture(ImageScreen& screen)
	{
		return Graphite::PlacePicture(screen.Tab.GetZoom(), kPictureSize, screen.Last.Area);
	}
}

TEST_CASE("An image tab draws its path row with the image tools and the picture below")
{
	ImageScreen screen("picture.png", Contents(ImageFixtures::kPng2x2));
	screen.Land();

	REQUIRE(screen.Tab.GetState() == ImageTabState::Ready);
	REQUIRE(screen.Picture.has_value());
	CHECK(screen.Picture->Min.y >= screen.Row.Max.y);
	CHECK(screen.Last.Area.Min.y == screen.Row.Max.y);
	CHECK(screen.Last.Area.Contains(*screen.Picture));

	const std::vector<Span> across =
		HoveredSpans(screen, ImVec2(0.0f, screen.RowMiddle().y), ImVec2(1.0f, 0.0f), static_cast<int>(kWindowSize.x));
	std::vector<Span> tools;

	for (const Span& span : across)
	{
		if (span.From >= screen.ToolsLeft() - 1.0f)
		{
			tools.push_back(span);
		}
	}

	REQUIRE(tools.size() == kToolButtonCount);
	CHECK(tools.back().To <= screen.Row.Max.x - Core::Scaled(Graphite::kBreadcrumbLeftPadding) + 1.0f);

	// The tools stand in the middle of the row's height, not at its top.
	const std::vector<Span> down = HoveredSpans(screen, ImVec2(tools.front().Middle(), screen.Row.Min.y),
												ImVec2(0.0f, 1.0f), static_cast<int>(screen.Row.GetHeight()));
	REQUIRE(down.size() == 1);
	CHECK(std::abs(down.front().Middle() - screen.Row.GetCenter().y) <= 1.0f);
}

TEST_CASE("An image tab shows the loader while its picture loads")
{
	ImageScreen screen("picture.png", Contents(ImageFixtures::kPng2x2));
	screen.Update();
	REQUIRE(screen.Jobs.Count() == 1);

	screen.Draw();
	CHECK(screen.Tab.GetState() == ImageTabState::Loading);
	CHECK_FALSE(screen.FindText("Loading image…").has_value());

	for (int frame = 0; frame < kFramesPastTheLoaderWait; ++frame)
	{
		screen.Draw();
	}

	const std::optional<ImRect> loader = screen.FindText("Loading image…");
	REQUIRE(loader.has_value());
	CHECK(loader->Min.y >= screen.Row.Max.y);
	CHECK_FALSE(screen.Picture.has_value());
	CHECK(screen.Last.Status.empty());

	screen.Jobs.RunAll();
	screen.Update();
	screen.Draw();

	CHECK(screen.Picture.has_value());
	CHECK_FALSE(screen.FindText("Loading image…").has_value());
}

TEST_CASE("An image tab that failed shows the reason with Retry")
{
	ImageScreen screen("missing.png");
	screen.Land();
	REQUIRE(screen.Tab.GetState() == ImageTabState::Failed);

	CHECK(screen.FindText("Could not read this image: the file was not found").has_value());
	CHECK_FALSE(screen.Picture.has_value());
	CHECK(screen.Last.Status.empty());

	const std::optional<ImRect> retry = screen.FindText("Retry");
	REQUIRE(retry.has_value());

	screen.Frames.Click(retry->GetCenter(), screen.Drawing());

	CHECK(screen.Tab.GetState() == ImageTabState::Loading);
	CHECK(screen.Jobs.Count() == 1);
}

TEST_CASE("An image tab too large to preview shows the reason without Retry")
{
	ImageScreen screen("huge.png", Contents(ImageFixtures::kPngHeader20000x12000));
	screen.Land();
	REQUIRE(screen.Tab.GetState() == ImageTabState::Failed);

	CHECK(screen.FindText("Image is too large to preview (20000 × 12000)").has_value());
	CHECK_FALSE(screen.FindText("Retry").has_value());
	CHECK_FALSE(screen.Picture.has_value());
}

TEST_CASE("An image tab whose new version could not be read keeps its picture under one line with Retry")
{
	ImageScreen screen("picture.png", Contents(ImageFixtures::kPng2x2));
	screen.Land();

	const std::filesystem::file_time_type before = std::filesystem::last_write_time(screen.File);
	screen.Directory.MakeFile("picture.png", "this is not a picture");
	std::filesystem::last_write_time(screen.File, before + std::chrono::seconds(60));
	screen.Tab.CheckDisk();
	screen.Jobs.RunAll();
	screen.Update();
	screen.Draw();
	REQUIRE_FALSE(screen.Tab.GetReloadFailure().empty());

	const std::optional<ImRect> notice = screen.FindText(screen.Tab.GetReloadFailure());
	REQUIRE(notice.has_value());
	REQUIRE(screen.Picture.has_value());
	CHECK(notice->Min.y >= screen.Row.Max.y);
	CHECK(screen.Last.Area.Min.y >= notice->Max.y);

	const std::optional<ImRect> retry = screen.FindText("Retry");
	REQUIRE(retry.has_value());
	CHECK(retry->Max.y <= screen.Last.Area.Min.y);

	screen.Frames.Click(retry->GetCenter(), screen.Drawing());

	CHECK(screen.Jobs.Count() == 1);
	CHECK(screen.Picture.has_value());
}

TEST_CASE("An image tab gives the status bar its facts and the hovered pixel")
{
	ImageScreen screen("picture.png", Contents(ImageFixtures::kPng2x2));
	screen.Land();

	const ViewStatusItem facts{"2 × 2 · PNG · 77 B", false, std::nullopt};
	CHECK(screen.Last.Status == std::vector<ViewStatusItem>{facts});

	screen.Tab.GetZoom() = Graphite::ZoomState{.Zoom = kLargeZoom, .Pan = ImVec2(0.0f, 0.0f), .Opened = true};
	screen.Draw();

	// The top-right pixel, green and opaque.
	const ImRect placed = PlacedPicture(screen);
	screen.MoveTo(ImVec2(placed.Min.x + kLargeZoom * 1.5f, placed.Min.y + kLargeZoom * kHalf));

	CHECK(screen.Last.Status == std::vector<ViewStatusItem>{{"1, 0", false, IM_COL32(0, 255, 0, 255)},
															{"#00FF00FF", true, std::nullopt},
															facts});

	screen.MoveTo(kFarAway);

	CHECK(screen.Last.Status == std::vector<ViewStatusItem>{facts});
}

TEST_CASE("The image tools zoom the picture around its middle and keep the background in the settings")
{
	ImageScreen screen("picture.png", Contents(ImageFixtures::kPng2x2));
	screen.Land();
	REQUIRE(screen.Tab.GetZoom().Zoom == 1.0f);

	const std::vector<Span> across =
		HoveredSpans(screen, ImVec2(screen.ToolsLeft(), screen.RowMiddle().y), ImVec2(1.0f, 0.0f),
					 static_cast<int>(screen.Row.Max.x - screen.ToolsLeft()));
	REQUIRE(across.size() == kToolButtonCount);

	constexpr std::size_t kZoomIn = 2;
	constexpr std::size_t kDarkSwatch = 6;

	screen.Frames.Click(ImVec2(across[kZoomIn].Middle(), screen.RowMiddle().y), screen.Drawing());
	screen.Draw();

	CHECK(screen.Tab.GetZoom().Zoom == 1.5f);
	CHECK(screen.Tab.GetZoom().Pan.x == 0.0f);
	CHECK(screen.Tab.GetZoom().Pan.y == 0.0f);

	screen.Frames.Click(ImVec2(across[kDarkSwatch].Middle(), screen.RowMiddle().y), screen.Drawing());

	CHECK(screen.Settings.GetImageBackground() == PictureBackground::Dark);
}

TEST_CASE("Zooming an image tab steps around the middle of its area and fits the whole picture")
{
	ImageScreen screen("picture.png", Contents(ImageFixtures::kPng2x2));
	screen.Land();

	const ImRect area = screen.Last.Area;
	screen.Tab.GetZoom().Pan = ImVec2(0.0f, 0.0f);

	Graphite::ZoomImageTab(screen.Tab, PictureZoom::In, area);
	CHECK(screen.Tab.GetZoom().Zoom == 1.5f);

	Graphite::ZoomImageTab(screen.Tab, PictureZoom::Out, area);
	Graphite::ZoomImageTab(screen.Tab, PictureZoom::Out, area);
	CHECK(screen.Tab.GetZoom().Zoom == doctest::Approx(2.0f / 3.0f));
	CHECK(screen.Tab.GetZoom().Pan.x == doctest::Approx(0.0f));
	CHECK(screen.Tab.GetZoom().Pan.y == doctest::Approx(0.0f));

	screen.Tab.GetZoom().Pan = ImVec2(5.0f, -3.0f);
	Graphite::ZoomImageTab(screen.Tab, PictureZoom::Fit, area);
	CHECK(screen.Tab.GetZoom().Zoom == doctest::Approx(Graphite::FitZoom(kPictureSize, area.GetSize(), Graphite::FitMargin())));
	CHECK(screen.Tab.GetZoom().Pan.x == 0.0f);
	CHECK(screen.Tab.GetZoom().Pan.y == 0.0f);

	SUBCASE("A tab with no picture yet is left as it is")
	{
		ImageTab loading(screen.Directory.MakeFile("loading.png", Contents(ImageFixtures::kPng2x2)),
						 screen.Jobs.Submitter(), screen.Textures);
		const float before = loading.GetZoom().Zoom;

		Graphite::ZoomImageTab(loading, PictureZoom::In, area);

		CHECK(loading.GetZoom().Zoom == before);
	}
}

TEST_CASE("In a 300 px pane the image tools never overlap the file name")
{
	constexpr float kNarrowPane = 300.0f;
	const std::string fileName = "beach.png";

	ImageScreen screen(fileName, Contents(ImageFixtures::kPng2x2));
	screen.Width = kNarrowPane;
	screen.Path.Folders = {"images", "holiday"};
	screen.Land();

	REQUIRE(screen.Tab.GetState() == ImageTabState::Ready);

	float nameWidth = 0.0f;
	float narrowestWidth = 0.0f;
	screen.Frames.Next([&] {
		nameWidth = Graphite::BreadcrumbWidth(screen.Path, screen.Path.Folders.size());
		narrowestWidth = Graphite::ImageToolsWidth(Graphite::ImageToolsSet::StepButtons);
	});

	const float nameRight = screen.Row.Min.x + Core::Scaled(Graphite::kBreadcrumbLeftPadding) + nameWidth;
	const float room = kNarrowPane - 2.0f * Core::Scaled(Graphite::kBreadcrumbLeftPadding);

	// The whole set would not leave the name its room, and the narrowest set would.
	REQUIRE(nameWidth + screen.ToolsWidth > room);
	REQUIRE(nameWidth + narrowestWidth <= room);

	const std::vector<Span> across =
		HoveredSpans(screen, ImVec2(0.0f, screen.RowMiddle().y), ImVec2(1.0f, 0.0f), static_cast<int>(kNarrowPane));

	// The first span is the row itself; whatever follows is a tool, and some tools are still drawn.
	REQUIRE(across.size() > 1);

	for (std::size_t index = 1; index < across.size(); ++index)
	{
		CHECK(across[index].From >= nameRight - 1.0f);
	}
}
