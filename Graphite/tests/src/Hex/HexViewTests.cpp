#include <doctest/doctest.h>

#include "DeferredJobs.h"
#include "DrawnColours.h"
#include "DrawnGlyphs.h"
#include "ImGuiFrames.h"
#include "SparseFile.h"
#include "TempDirectory.h"

#include "Editor/Breadcrumb.h"
#include "Hex/HexFile.h"
#include "Hex/HexText.h"
#include "Hex/HexView.h"
#include "Theme/EditorPalette.h"

#include "Core/Base/KeyCodes.h"
#include "Core/Events/FrameInput.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using Core::KeyCode;
using Core::KeyModifier;
using Graphite::ViewStatusItem;

namespace {

	constexpr ImVec2 kWindowSize(800.0f, 400.0f);
	constexpr ImVec2 kReferencePlace(0.0f, 450.0f);
	constexpr ImVec2 kReferenceSize(1280.0f, 100.0f);
	constexpr ImVec2 kFarAway(1200.0f, 700.0f);
	constexpr float kFontSize = 16.0f;
	constexpr float kInterfaceFont = 0.0f;
	constexpr float kScrollableBelow = 200.0f;
	constexpr float kHalf = 0.5f;
	constexpr int kFramesPastTheLoaderWait = 15;
	constexpr std::uint64_t kGiB = 1024ull * 1024 * 1024;
	constexpr const char* kWindow = "Hex";
	constexpr const char* kReferenceWindow = "Reference";
	constexpr const char* kFileName = "dump.bin";

	constexpr ImU32 kTextColour = IM_COL32(201, 202, 203, 255);
	constexpr ImU32 kSelectionColour = IM_COL32(20, 60, 180, 255);
	constexpr ImU32 kHoverColour = IM_COL32(70, 71, 72, 255);

	// The first 128 bytes of a Windows executable, with the DOS stub's message at 0x4E where a real one has it.
	std::string DosStub()
	{
		std::string bytes(128, '\0');

		const auto put = [&bytes](std::size_t at, std::initializer_list<int> values) {
			for (const int value : values)
			{
				bytes[at++] = static_cast<char>(value);
			}
		};

		put(0x00, {0x4D, 0x5A, 0x90, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00});
		put(0x3C, {0x80});
		put(0x40, {0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21});

		const std::string message = "This program cannot be run in DOS mode.\r\r\n$";
		bytes.replace(0x4E, message.size(), message);
		return bytes;
	}

	std::string Pattern(std::size_t size)
	{
		std::string bytes(size, '\0');

		for (std::size_t at = 0; at < size; ++at)
		{
			bytes[at] = static_cast<char>(0x41 + at % 26);
		}

		return bytes;
	}

	// Moves the modified time on as well, so the change is seen even when the size stays the same.
	void Rewrite(const std::filesystem::path& path, const std::string& contents)
	{
		const std::filesystem::file_time_type before = std::filesystem::last_write_time(path);

		{
			std::ofstream stream{path, std::ios::binary | std::ios::trunc};
			stream << contents;
		}

		std::filesystem::last_write_time(path, before + std::chrono::seconds(60));
	}

	struct Found
	{
		ImRect Box;
		std::size_t First = 0;
		std::size_t Count = 0;
	};

	// Every place the run's glyphs are drawn one after another, in the order they were drawn.
	std::vector<Found> FindRuns(const std::vector<DrawnGlyph>& drawn, const std::vector<DrawnGlyph>& run)
	{
		std::vector<Found> runs;

		if (run.empty())
		{
			return runs;
		}

		const auto sameGlyph = [](const DrawnGlyph& a, const DrawnGlyph& b) { return a.Uv.x == b.Uv.x && a.Uv.y == b.Uv.y; };

		for (auto found = std::search(drawn.begin(), drawn.end(), run.begin(), run.end(), sameGlyph); found != drawn.end();
			 found = std::search(found + 1, drawn.end(), run.begin(), run.end(), sameGlyph))
		{
			Found result{.Box = found->Box, .First = static_cast<std::size_t>(found - drawn.begin()), .Count = run.size()};

			for (auto glyph = found; glyph != found + static_cast<std::ptrdiff_t>(run.size()); ++glyph)
			{
				result.Box.Add(glyph->Box);
			}

			runs.push_back(result);
		}

		return runs;
	}

	// The rectangles filled in `colour`: four corners each, as AddRectFilled lays them down.
	std::vector<ImRect> RectsInColour(const ImDrawList& drawing, ImU32 colour)
	{
		constexpr int kCorners = 4;
		std::vector<ImRect> rects;

		for (int first = 0; first + kCorners <= drawing.VtxBuffer.Size;)
		{
			const ImDrawVert* const quad = &drawing.VtxBuffer[first];

			if (quad[0].col == colour && quad[1].col == colour && quad[2].col == colour && quad[3].col == colour)
			{
				rects.emplace_back(quad[0].pos, quad[2].pos);
				first += kCorners;
				continue;
			}

			++first;
		}

		return rects;
	}

	bool AnyCovers(const std::vector<ImRect>& rects, ImVec2 point)
	{
		return std::ranges::any_of(rects, [point](const ImRect& rect) { return rect.Contains(point); });
	}

	struct ExistingFile
	{
	};

	struct HexScreen
	{
		explicit HexScreen(const std::string& contents)
			: File(Directory.MakeFile(kFileName, contents)), Hex(File, Jobs.Submitter())
		{
			Prepare();
		}

		HexScreen(ExistingFile, std::filesystem::path file) : File(std::move(file)), Hex(File, Jobs.Submitter())
		{
			Prepare();
		}

		void Prepare()
		{
			Path.FileName = kFileName;
			Palette.Text = kTextColour;
			Palette.Selection = kSelectionColour;
		}

		void DrawWindow()
		{
			// As the owner runs it: once a frame, before any view is drawn.
			Hex.Update();

			if (Unfocused)
			{
				ImGui::FocusWindow(nullptr);
			}

			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(WindowSize);
			ImGui::Begin(kWindow, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

			const ImGuiWindow& window = *ImGui::GetCurrentWindow();
			Row = ImRect(window.InnerRect.Min,
						 ImVec2(window.InnerRect.Max.x, window.InnerRect.Min.y + Graphite::BreadcrumbHeight()));

			// The theme's hover surface, the one list rows lift to.
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(kHoverColour));

			Graphite::HexViewInput input;
			input.Path = &Path;
			input.Palette = &Palette;
			input.FontSize = kFontSize;
			input.Keys = &Keys;
			input.Notice = Notice;
			input.RouteWheel = RouteWheel;
			Last = Graphite::DrawHexView(Hex, State, input);
			ImGui::PopStyleColor();

			if (After)
			{
				After();
			}

			Glyphs = DrawnGlyphs(*ImGui::GetWindowDrawList());
			ImGui::End();

			if (!Reference.empty())
			{
				ImGui::SetNextWindowPos(kReferencePlace);
				ImGui::SetNextWindowSize(kReferenceSize);
				ImGui::Begin(kReferenceWindow, nullptr,
							 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
								 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs);

				if (ReferenceSize > 0.0f)
				{
					ImGui::PushFont(nullptr, ReferenceSize);
				}

				ImGui::TextUnformatted(Reference.c_str());

				if (ReferenceSize > 0.0f)
				{
					ImGui::PopFont();
				}

				ReferenceGlyphs = DrawnGlyphs(*ImGui::GetWindowDrawList());
				ImGui::End();
			}
		}

		std::function<void()> Drawing()
		{
			return [this] { DrawWindow(); };
		}

		void Draw() { Frames.Next(Drawing()); }

		// Draws once to ask for the rows on screen, lets their reads run, and draws what they read.
		void Land()
		{
			Draw();
			Jobs.RunAll();
			Draw();
		}

		void MoveTo(ImVec2 at)
		{
			ImGui::GetIO().AddMousePosEvent(at.x, at.y);
			Draw();
		}

		void Press(bool down)
		{
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, down);
			Draw();
		}

		void Click(ImVec2 at) { Frames.Click(at, Drawing()); }

		void ShiftClick(ImVec2 at)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddKeyEvent(ImGuiMod_Shift, true);
			io.AddKeyEvent(ImGuiKey_LeftShift, true);
			Draw();
			Click(at);
			io.AddKeyEvent(ImGuiKey_LeftShift, false);
			io.AddKeyEvent(ImGuiMod_Shift, false);
			Draw();
		}

		// As the document windows hand it on: the strokes the router left, for the frame that follows.
		void Stroke(KeyCode key, KeyModifier modifiers = KeyModifier::None)
		{
			Keys.PushKey(key, modifiers, false);
			Draw();
			Keys.Clear();
		}

		// As ImGui's own keyboard navigation reads a key: straight from io, not through the router.
		void PressNavigationKey(ImGuiKey key, int heldFrames = 1)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddKeyEvent(key, true);

			for (int frame = 0; frame < heldFrames; ++frame)
			{
				Draw();
			}

			io.AddKeyEvent(key, false);
			Draw();
		}

		void Wheel(float notches)
		{
			ImGui::GetIO().AddMouseWheelEvent(0.0f, notches);
			Draw();
		}

		// Whether `text` is drawn in the view, matched glyph for glyph against the same text drawn in a window of its
		// own in the same frames, in the code face at the view's size or in the interface font.
		std::vector<Found> FindAll(const std::string& text, float size = kFontSize)
		{
			Reference = text;
			ReferenceSize = size;
			Draw();
			Draw();
			Reference.clear();

			return FindRuns(Glyphs, ReferenceGlyphs);
		}

		std::optional<Found> FindText(const std::string& text, float size = kFontSize)
		{
			const std::vector<Found> runs = FindAll(text, size);
			return runs.empty() ? std::nullopt : std::optional<Found>(runs.front());
		}

		// A row's offset, told apart from the same digits among the bytes by starting under the heading.
		std::optional<Found> FindOffset(const std::string& offset)
		{
			const std::optional<Found> heading = FindText("Offset");
			REQUIRE(heading.has_value());

			for (const Found& run : FindAll(offset))
			{
				if (run.Box.Min.x < heading->Box.Max.x)
				{
					return run;
				}
			}

			return std::nullopt;
		}

		ImRect PartOf(const Found& found, std::size_t from, std::size_t count) const
		{
			ImRect box = Glyphs[found.First + from].Box;

			for (std::size_t glyph = found.First + from; glyph < found.First + from + count; ++glyph)
			{
				box.Add(Glyphs[glyph].Box);
			}

			return box;
		}

		std::vector<ImU32> ColoursOf(const Found& found) const
		{
			std::vector<ImU32> colours;

			for (std::size_t glyph = found.First; glyph < found.First + found.Count; ++glyph)
			{
				colours.push_back(Glyphs[glyph].Colour);
			}

			return colours;
		}

		static ImGuiWindow& Window() { return *ImGui::FindWindowByName(kWindow); }

		// What the view drew in the frame just ended; ImGui keeps it until the next frame begins.
		static const ImDrawList& Drawn() { return *Window().DrawList; }

		ImGuiFrames Frames;
		TempDirectory Directory;
		DeferredJobs Jobs;
		std::filesystem::path File;
		Graphite::HexFile Hex;
		Graphite::HexViewState State;
		Graphite::BreadcrumbModel Path;
		Graphite::EditorPalette Palette;
		Core::FrameInput Keys;
		std::string Notice;
		std::function<void()> After;
		std::function<void()> RouteWheel;
		bool Unfocused = false;
		ImVec2 WindowSize = kWindowSize;

		Graphite::HexViewFrame Last;
		ImRect Row;
		std::vector<DrawnGlyph> Glyphs;

		std::string Reference;
		float ReferenceSize = kFontSize;
		std::vector<DrawnGlyph> ReferenceGlyphs;
	};

	ImU32 Dim()
	{
		return ImGui::GetColorU32(ImGuiCol_TextDisabled);
	}

	// Glyphs stand at their own heights in a line, so two boxes are on one line when they are nearer than half a line.
	bool OnOneLine(const ImRect& a, const ImRect& b)
	{
		return std::abs(a.GetCenter().y - b.GetCenter().y) < kFontSize * kHalf;
	}

	bool AllIn(const std::vector<ImU32>& colours, ImU32 colour)
	{
		return std::ranges::all_of(colours, [colour](ImU32 each) { return each == colour; });
	}

	void CheckSelection(const Graphite::HexSelection& selection, std::uint64_t anchor, std::uint64_t active)
	{
		CHECK(selection.Anchor == anchor);
		CHECK(selection.Active == active);
	}
}

TEST_CASE("The hex view draws the header, offsets, bytes and ASCII of the rows on screen")
{
	HexScreen screen(DosStub());
	screen.Land();

	const std::optional<Found> heading = screen.FindText("Offset");
	const std::optional<Found> columns = screen.FindText("00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F");
	const std::optional<Found> ascii = screen.FindText("ASCII");
	REQUIRE(heading.has_value());
	REQUIRE(columns.has_value());
	REQUIRE(ascii.has_value());
	CHECK(heading->Box.Min.y >= screen.Row.Max.y);
	CHECK(AllIn(screen.ColoursOf(*heading), Dim()));
	CHECK(AllIn(screen.ColoursOf(*columns), Dim()));
	CHECK(AllIn(screen.ColoursOf(*ascii), Dim()));

	const std::optional<Found> firstRow = screen.FindOffset("00000000");
	const std::optional<Found> messageRow = screen.FindOffset("00000040");
	const std::optional<Found> lastRow = screen.FindOffset("00000070");
	REQUIRE(firstRow.has_value());
	REQUIRE(messageRow.has_value());
	REQUIRE(lastRow.has_value());
	CHECK(firstRow->Box.Min.y >= heading->Box.Max.y);
	CHECK(messageRow->Box.Min.y > firstRow->Box.Max.y);
	CHECK(lastRow->Box.Min.y > messageRow->Box.Max.y);
	CHECK_FALSE(screen.FindOffset("00000080").has_value());

	const std::optional<Found> opening = screen.FindText("4D 5A 90 00");
	const std::optional<Found> message = screen.FindText("CD 21 54 68");
	REQUIRE(opening.has_value());
	REQUIRE(message.has_value());
	CHECK(OnOneLine(opening->Box, firstRow->Box));
	CHECK(OnOneLine(message->Box, messageRow->Box));

	// Each heading stands over its column: the 0 of 03 over the 0 of the fourth byte, 00.
	CHECK(screen.PartOf(*columns, 6, 1).Min.x == doctest::Approx(screen.PartOf(*opening, 6, 1).Min.x));

	const std::optional<Found> stub = screen.FindText("MZ..............");
	const std::optional<Found> text = screen.FindText("is program canno");
	REQUIRE(stub.has_value());
	REQUIRE(text.has_value());
	CHECK(stub->Box.Min.x > opening->Box.Max.x);
	CHECK(screen.FindText(".!Th").has_value());
	CHECK(AllIn(screen.ColoursOf(*text), kTextColour));
	CHECK(std::abs(ascii->Box.Min.x - stub->Box.Min.x) < kFontSize * kHalf);
}

TEST_CASE("Unread rows show their offset and placeholders")
{
	HexScreen screen(DosStub());
	screen.Draw();
	REQUIRE(screen.Jobs.Count() == 1);

	const std::optional<Found> offset = screen.FindOffset("00000000");
	const std::optional<Found> placeholders = screen.FindText("·· ·· ·· ·· ·· ·· ·· ··");
	REQUIRE(offset.has_value());
	REQUIRE(placeholders.has_value());
	CHECK(OnOneLine(placeholders->Box, offset->Box));
	CHECK(placeholders->Box.Min.x > offset->Box.Max.x);
	CHECK(AllIn(screen.ColoursOf(*placeholders), Dim()));
	CHECK(screen.FindOffset("00000070").has_value());
	CHECK_FALSE(screen.FindText("4D 5A 90 00").has_value());

	screen.Jobs.RunAll();
	screen.Draw();

	CHECK(screen.FindText("4D 5A 90 00").has_value());
	CHECK_FALSE(screen.FindText("·· ··").has_value());
}

TEST_CASE("Zero bytes and dots are drawn dim")
{
	HexScreen screen(DosStub());
	screen.Land();

	// 5A 90 00 03: only the zero is dim, though 90 shows as a dot.
	const std::optional<Found> bytes = screen.FindText("5A 90 00 03");
	REQUIRE(bytes.has_value());
	CHECK(screen.ColoursOf(*bytes) ==
		  std::vector<ImU32>{kTextColour, kTextColour, kTextColour, kTextColour, Dim(), Dim(), kTextColour, kTextColour});

	// The dots standing in for 0E 1F BA 0E 00 B4 09 CD, then the ! of 21; the full stop of "mode." is a character.
	const std::optional<Found> dots = screen.FindText("........!..L");
	REQUIRE(dots.has_value());
	const std::vector<ImU32> colours = screen.ColoursOf(*dots);
	CHECK(AllIn(std::vector<ImU32>(colours.begin(), colours.begin() + 8), Dim()));
	CHECK(colours[8] == kTextColour);
	CHECK(colours[11] == kTextColour);

	const std::optional<Found> stop = screen.FindText("mode.");
	REQUIRE(stop.has_value());
	CHECK(AllIn(screen.ColoursOf(*stop), kTextColour));
}

TEST_CASE("Hovering a byte highlights it in both columns")
{
	HexScreen screen(DosStub());
	screen.Land();

	const std::optional<Found> hex = screen.FindText("CD 21 54 68");
	const std::optional<Found> ascii = screen.FindText(".!Th");
	REQUIRE(hex.has_value());
	REQUIRE(ascii.has_value());
	const ImRect byte = screen.PartOf(*hex, 4, 2);
	const ImRect character = screen.PartOf(*ascii, 2, 1);

	for (const ImVec2 at : {byte.GetCenter(), character.GetCenter()})
	{
		screen.MoveTo(at);

		CHECK(screen.State.Hovered == std::optional<std::uint64_t>(0x4E));
		const std::vector<ImRect> marks = RectsInColour(HexScreen::Drawn(), kHoverColour);
		REQUIRE(marks.size() == 2);
		CHECK(marks[0].Contains(byte.GetCenter()));
		CHECK(marks[1].Contains(character.GetCenter()));
		CHECK(marks[0].Min.x < byte.Min.x);
		CHECK(marks[0].Max.x > byte.Max.x);
	}

	screen.MoveTo(kFarAway);

	CHECK_FALSE(screen.State.Hovered.has_value());
	CHECK(RectsInColour(HexScreen::Drawn(), kHoverColour).empty());
}

TEST_CASE("A click selects a byte and Shift+click a range")
{
	HexScreen screen(DosStub());
	screen.Land();

	const std::optional<Found> message = screen.FindText("CD 21 54 68");
	const std::optional<Found> next = screen.FindText("69 73 20 70");
	const std::optional<Found> ascii = screen.FindText("is p");
	REQUIRE(message.has_value());
	REQUIRE(next.has_value());
	REQUIRE(ascii.has_value());
	const ImRect first = screen.PartOf(*message, 4, 2);
	const ImRect fourth = screen.PartOf(*next, 2, 2);

	screen.Click(first.GetCenter());
	CheckSelection(screen.State.Selection, 0x4E, 0x4E);

	screen.ShiftClick(fourth.GetCenter());
	CheckSelection(screen.State.Selection, 0x4E, 0x51);

	const std::vector<ImRect> marks = RectsInColour(HexScreen::Drawn(), kSelectionColour);
	CHECK(AnyCovers(marks, first.GetCenter()));
	CHECK(AnyCovers(marks, screen.PartOf(*message, 6, 2).GetCenter()));
	CHECK(AnyCovers(marks, screen.PartOf(*next, 0, 2).GetCenter()));
	CHECK(AnyCovers(marks, fourth.GetCenter()));
	CHECK(AnyCovers(marks, screen.PartOf(*ascii, 1, 1).GetCenter()));
	CHECK_FALSE(AnyCovers(marks, screen.PartOf(*message, 2, 2).GetCenter()));
	CHECK_FALSE(AnyCovers(marks, screen.PartOf(*next, 4, 2).GetCenter()));

	// A click in the ASCII column selects the byte the character stands for; the space draws no glyph.
	screen.Click(screen.PartOf(*ascii, 2, 1).GetCenter());
	CheckSelection(screen.State.Selection, 0x53, 0x53);
}

TEST_CASE("Dragging selects a range")
{
	HexScreen screen(DosStub());
	screen.Land();

	const std::optional<Found> message = screen.FindText("CD 21 54 68");
	const std::optional<Found> rowEnd = screen.FindText("63 61 6E 6E 6F");
	REQUIRE(message.has_value());
	REQUIRE(rowEnd.has_value());
	const ImVec2 start = screen.PartOf(*message, 4, 2).GetCenter();
	const ImVec2 end = screen.PartOf(*rowEnd, 8, 2).GetCenter();

	screen.MoveTo(start);
	screen.Press(true);
	screen.MoveTo(ImVec2((start.x + end.x) * kHalf, end.y));
	screen.MoveTo(end);
	screen.Press(false);

	CheckSelection(screen.State.Selection, 0x4E, 0x5F);

	// Back up the ASCII column, from the o of "canno" to the s of "is".
	const std::optional<Found> ascii = screen.FindText("is program canno");
	REQUIRE(ascii.has_value());
	screen.MoveTo(screen.PartOf(*ascii, 13, 1).GetCenter());
	screen.Press(true);
	screen.MoveTo(screen.PartOf(*ascii, 1, 1).GetCenter());
	screen.Press(false);

	CheckSelection(screen.State.Selection, 0x5F, 0x51);
}

TEST_CASE("Dragging past the rows' bottom edge scrolls a row a frame")
{
	HexScreen screen(Pattern(4096));
	screen.Land();

	const std::optional<Found> top = screen.FindOffset("00000000");
	REQUIRE(top.has_value());
	screen.MoveTo(top->Box.GetCenter());
	screen.Press(true);
	CheckSelection(screen.State.Selection, 0, 0);

	screen.MoveTo(ImVec2(top->Box.GetCenter().x, kWindowSize.y + 20.0f));
	const std::uint64_t scrolled = screen.State.Scroll.TopRow;
	const std::uint64_t below = screen.State.Selection.Active / Graphite::kBytesPerRow - scrolled;
	CHECK(scrolled == 1);

	screen.Draw();
	screen.Draw();

	CHECK(screen.State.Scroll.TopRow == scrolled + 2);
	CHECK(screen.State.Selection.Active / Graphite::kBytesPerRow - screen.State.Scroll.TopRow == below);
	CHECK(screen.State.Selection.Anchor == 0);

	screen.Press(false);
	screen.Draw();

	CHECK(screen.State.Scroll.TopRow == scrolled + 2);
}

TEST_CASE("Arrow keys move the selection and scroll to keep it in view")
{
	HexScreen screen(Pattern(4096));
	screen.Land();
	const std::uint64_t rows = Graphite::RowCount(4096);

	screen.Stroke(KeyCode::Right);
	CheckSelection(screen.State.Selection, 1, 1);
	screen.Stroke(KeyCode::Right, KeyModifier::Shift);
	CheckSelection(screen.State.Selection, 1, 2);
	screen.Stroke(KeyCode::Down);
	CheckSelection(screen.State.Selection, 18, 18);
	screen.Stroke(KeyCode::Left);
	screen.Stroke(KeyCode::Up);
	CheckSelection(screen.State.Selection, 1, 1);
	screen.Stroke(KeyCode::End);
	CheckSelection(screen.State.Selection, 15, 15);
	screen.Stroke(KeyCode::Home, KeyModifier::Shift);
	CheckSelection(screen.State.Selection, 15, 0);

	screen.Stroke(KeyCode::End, KeyModifier::Control);
	CheckSelection(screen.State.Selection, 4095, 4095);
	REQUIRE(screen.State.Scroll.TopRow > 0);
	const std::uint64_t visible = rows - screen.State.Scroll.TopRow;

	screen.Stroke(KeyCode::Home, KeyModifier::Control | KeyModifier::Shift);
	CheckSelection(screen.State.Selection, 4095, 0);
	CHECK(screen.State.Scroll.TopRow == 0);

	// Down from the last whole row on screen brings one more row in.
	screen.Stroke(KeyCode::Home, KeyModifier::Control);

	for (std::uint64_t row = 1; row < visible; ++row)
	{
		screen.Stroke(KeyCode::Down);
	}

	CHECK(screen.State.Scroll.TopRow == 0);
	screen.Stroke(KeyCode::Down);
	CheckSelection(screen.State.Selection, visible * 16, visible * 16);
	CHECK(screen.State.Scroll.TopRow == 1);

	screen.Stroke(KeyCode::PageDown);
	CheckSelection(screen.State.Selection, 2 * visible * 16, 2 * visible * 16);
	CHECK(screen.State.Scroll.TopRow == 1 + visible);

	screen.Stroke(KeyCode::PageUp);
	CheckSelection(screen.State.Selection, visible * 16, visible * 16);
	CHECK(screen.State.Scroll.TopRow == 1);

	SUBCASE("Strokes that are not moves are left alone")
	{
		screen.Stroke(KeyCode::Left, KeyModifier::Control);
		screen.Stroke(KeyCode::Down, KeyModifier::Alt);
		screen.Stroke(KeyCode::A, KeyModifier::Control);
		screen.Stroke(KeyCode::C, KeyModifier::Control);

		CheckSelection(screen.State.Selection, visible * 16, visible * 16);
		CHECK(screen.State.Scroll.TopRow == 1);
	}

	SUBCASE("A view that does not hold the keyboard takes no keys")
	{
		screen.Unfocused = true;
		screen.Draw();
		REQUIRE_FALSE(screen.Last.Focused);

		screen.Stroke(KeyCode::Right);

		CheckSelection(screen.State.Selection, visible * 16, visible * 16);
	}
}

TEST_CASE("The wheel scrolls three rows a notch and is not passed on")
{
	HexScreen screen(Pattern(4096));
	screen.After = [] { ImGui::Dummy(ImVec2(1.0f, kScrollableBelow)); };
	screen.Land();
	screen.Draw();
	REQUIRE(HexScreen::Window().ScrollMax.y > 0.0f);

	const std::optional<Found> top = screen.FindOffset("00000000");
	REQUIRE(top.has_value());
	screen.MoveTo(top->Box.GetCenter());

	screen.Wheel(-1.0f);
	CHECK(screen.State.Scroll.TopRow == 3);

	screen.Wheel(-2.0f);
	CHECK(screen.State.Scroll.TopRow == 9);

	screen.Wheel(1.0f);
	CHECK(screen.State.Scroll.TopRow == 6);

	// A window at its top would scroll on the notches down, had the view left the wheel to it.
	CHECK(HexScreen::Window().Scroll.y == 0.0f);

	screen.Wheel(5.0f);
	CHECK(screen.State.Scroll.TopRow == 0);
}

TEST_CASE("Ctrl+wheel over the rows leaves the scroll and is routed to the hovered-editor chords")
{
	HexScreen screen(Pattern(4096));
	int routed = 0;
	screen.RouteWheel = [&routed] { ++routed; };
	screen.Land();
	screen.Draw();

	const std::optional<Found> top = screen.FindOffset("00000000");
	REQUIRE(top.has_value());
	screen.MoveTo(top->Box.GetCenter());

	screen.Wheel(-1.0f);
	CHECK(screen.State.Scroll.TopRow == 3);
	CHECK(routed == 0);

	ImGuiIO& io = ImGui::GetIO();
	io.AddKeyEvent(ImGuiMod_Ctrl, true);
	io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
	screen.Wheel(-1.0f);

	CHECK(screen.State.Scroll.TopRow == 3);
	CHECK(routed >= 1);

	io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
	io.AddKeyEvent(ImGuiMod_Ctrl, false);
	screen.Draw();
}

TEST_CASE("Dragging the scrollbar's grab to the bottom shows the last row")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "disk.img";
	constexpr std::uint64_t kFileSize = 5 * kGiB;

	if (!TryWriteSparseFile(path, kFileSize))
	{
		MESSAGE("Skipped: the temporary directory's filesystem cannot make a file sparse");
		return;
	}

	HexScreen screen(ExistingFile{}, path);
	screen.Land();

	const ImU32 idle = ImGui::GetColorU32(ImGuiCol_ScrollbarGrab);
	const std::optional<HorizontalExtent> across = HorizontalExtentInColour(HexScreen::Drawn(), idle);
	const std::optional<VerticalExtent> down = VerticalExtentInColour(HexScreen::Drawn(), idle);
	REQUIRE(across.has_value());
	REQUIRE(down.has_value());
	CHECK(across->Right <= kWindowSize.x);
	CHECK(across->Left >= kWindowSize.x - ImGui::GetStyle().ScrollbarSize);

	const ImVec2 grab((across->Left + across->Right) * kHalf, (down->Top + down->Bottom) * kHalf);
	screen.MoveTo(grab);
	screen.Press(true);
	CHECK(screen.State.Scroll.TopRow == 0);

	screen.MoveTo(ImVec2(grab.x, grab.y + 100.0f));
	CHECK(screen.State.Scroll.TopRow > 0);
	screen.MoveTo(ImVec2(grab.x, kWindowSize.y + 300.0f));
	screen.Press(false);
	screen.MoveTo(kFarAway);

	const std::uint64_t rows = Graphite::RowCount(kFileSize);
	CHECK(screen.State.Scroll.TopRow < rows);
	CHECK(screen.State.Scroll.TopRow > rows - 100);

	screen.Jobs.RunAll();
	screen.Draw();

	const std::optional<Found> last = screen.FindOffset("13FFFFFF0");
	const std::optional<Found> before = screen.FindOffset("13FFFFFE0");
	REQUIRE(last.has_value());
	REQUIRE(before.has_value());
	CHECK(last->Box.Max.y <= kWindowSize.y);
	CHECK(last->Box.Min.y > before->Box.Max.y);
	CHECK_FALSE(screen.FindText("·· ··").has_value());

	// The grab rests on the bottom of its track.
	const std::optional<VerticalExtent> resting = VerticalExtentInColour(HexScreen::Drawn(), idle);
	REQUIRE(resting.has_value());
	CHECK(resting->Bottom > kWindowSize.y - ImGui::GetStyle().ScrollbarSize);
}

TEST_CASE("A one-byte file draws one row")
{
	HexScreen screen("M");
	screen.Land();

	CHECK(screen.FindOffset("00000000").has_value());
	CHECK_FALSE(screen.FindOffset("00000010").has_value());
	CHECK(screen.FindText("4D").has_value());
	CHECK(screen.FindText("M").has_value());

	CHECK(screen.Last.Status ==
		  std::vector<ViewStatusItem>{{"Offset 0x00000000", false, std::nullopt},
									  {"1 byte", false, std::nullopt},
									  {"u8 77", true, std::nullopt},
									  {"1 B", false, std::nullopt}});
}

TEST_CASE("A notice is drawn above the header")
{
	HexScreen screen(DosStub());
	screen.Land();
	const std::optional<Found> without = screen.FindText("Offset");
	REQUIRE(without.has_value());

	screen.Notice = "Could not read this image: the data is not a picture";
	screen.Draw();

	const std::optional<Found> notice = screen.FindText(screen.Notice, kInterfaceFont);
	const std::optional<Found> heading = screen.FindText("Offset");
	REQUIRE(notice.has_value());
	REQUIRE(heading.has_value());
	CHECK(notice->Box.Min.y >= screen.Row.Max.y);
	CHECK(heading->Box.Min.y >= notice->Box.Max.y);
	CHECK(heading->Box.Min.y > without->Box.Min.y);
	CHECK(AllIn(screen.ColoursOf(*notice), Dim()));
}

TEST_CASE("A read failure shows the reason with Retry")
{
	HexScreen screen(DosStub());
	std::filesystem::remove(screen.File);
	screen.Land();
	REQUIRE(screen.Hex.GetFailure() == "the file was not found");

	const std::optional<Found> reason = screen.FindText("the file was not found", kInterfaceFont);
	REQUIRE(reason.has_value());
	CHECK_FALSE(screen.FindOffset("00000000").has_value());

	const std::optional<Found> retry = screen.FindText("Retry", kInterfaceFont);
	REQUIRE(retry.has_value());
	CHECK(retry->Box.Min.y >= screen.Row.Max.y);

	screen.Directory.MakeFile(kFileName, DosStub());
	screen.Click(retry->Box.GetCenter());

	CHECK(screen.Hex.GetFailure().empty());
	CHECK(screen.Jobs.Count() == 1);

	screen.Jobs.RunAll();
	screen.Draw();

	CHECK(screen.FindText("4D 5A 90 00").has_value());
	CHECK_FALSE(screen.FindText("the file was not found", kInterfaceFont).has_value());
}

TEST_CASE("The status bar items follow the selection")
{
	HexScreen screen(DosStub());
	screen.Draw();

	// The selected bytes are not read yet, so there are no values to give.
	CHECK(screen.Last.Status == std::vector<ViewStatusItem>{{"Offset 0x00000000", false, std::nullopt},
															{"1 byte", false, std::nullopt},
															{"128 B", false, std::nullopt}});

	screen.Jobs.RunAll();
	screen.Draw();

	CHECK(screen.Last.Status == std::vector<ViewStatusItem>{{"Offset 0x00000000", false, std::nullopt},
															{"1 byte", false, std::nullopt},
															{"u8 77  u16 23117  u32 9460301", true, std::nullopt},
															{"128 B", false, std::nullopt}});

	const std::optional<Found> message = screen.FindText("CD 21 54 68");
	REQUIRE(message.has_value());
	screen.Click(screen.PartOf(*message, 4, 2).GetCenter());

	for (int byte = 1; byte < 12; ++byte)
	{
		screen.Stroke(KeyCode::Right, KeyModifier::Shift);
	}

	CHECK(screen.Last.Status == std::vector<ViewStatusItem>{{"Offset 0x0000004E", false, std::nullopt},
															{"12 bytes", false, std::nullopt},
															{"u8 84  u16 26708  u32 1936287828", true, std::nullopt},
															{"128 B", false, std::nullopt}});
}

TEST_CASE("A file that shrank to nothing draws no rows and gives only its size")
{
	HexScreen screen(DosStub());
	screen.Land();
	screen.State.Selection = {0x10, 0x20};

	Rewrite(screen.File, "");
	screen.Hex.CheckDisk();
	screen.Draw();

	CHECK(screen.Last.Status == std::vector<ViewStatusItem>{{"0 B", false, std::nullopt}});
	CHECK(screen.FindText("Offset").has_value());
	CHECK_FALSE(screen.FindOffset("00000000").has_value());
	CheckSelection(screen.State.Selection, 0, 0);
}

TEST_CASE("A change on disk clamps the selection and keeps the scroll")
{
	HexScreen screen(Pattern(4096));
	screen.Land();
	screen.State.Selection = {4000, 4095};
	screen.State.Scroll.TopRow = 10;
	screen.Draw();
	REQUIRE(screen.State.Scroll.TopRow == 10);

	Rewrite(screen.File, Pattern(2048));
	screen.Hex.CheckDisk();
	screen.Draw();

	CheckSelection(screen.State.Selection, 2047, 2047);
	CHECK(screen.State.Scroll.TopRow == 10);
	CHECK(screen.State.SeenChange == screen.Hex.GetChangeCount());
}

TEST_CASE("A read still waiting after a fifth of a second shows Reading… at the header's right end")
{
	HexScreen screen(DosStub());
	screen.Draw();
	REQUIRE(screen.Hex.IsWaiting());
	CHECK_FALSE(screen.FindText("Reading…", kInterfaceFont).has_value());

	for (int frame = 0; frame < kFramesPastTheLoaderWait; ++frame)
	{
		screen.Draw();
	}

	const std::optional<Found> loader = screen.FindText("Reading…", kInterfaceFont);
	const std::optional<Found> heading = screen.FindText("Offset");
	REQUIRE(loader.has_value());
	REQUIRE(heading.has_value());
	CHECK(loader->Box.Max.x <= kWindowSize.x);
	CHECK(loader->Box.Min.x > kWindowSize.x * kHalf);
	CHECK(loader->Box.GetCenter().y > heading->Box.Min.y - kFontSize * kHalf);
	CHECK(loader->Box.GetCenter().y < heading->Box.Max.y + kFontSize * kHalf);

	screen.Jobs.RunAll();
	screen.Draw();

	CHECK_FALSE(screen.FindText("Reading…", kInterfaceFont).has_value());
}

TEST_CASE("The loader stands after the ASCII heading in a wide window and is left out of a narrow one")
{
	constexpr float kNarrowWidth = 400.0f;

	HexScreen wide(DosStub());
	wide.Draw();

	for (int frame = 0; frame < kFramesPastTheLoaderWait; ++frame)
	{
		wide.Draw();
	}

	const std::optional<Found> wideLoader = wide.FindText("Reading…", kInterfaceFont);
	const std::optional<Found> ascii = wide.FindText("ASCII");
	REQUIRE(wideLoader.has_value());
	REQUIRE(ascii.has_value());
	CHECK(wideLoader->Box.Min.x > ascii->Box.Max.x);

	HexScreen narrow(DosStub());
	narrow.WindowSize = ImVec2(kNarrowWidth, kWindowSize.y);
	narrow.Draw();

	for (int frame = 0; frame < kFramesPastTheLoaderWait; ++frame)
	{
		narrow.Draw();
	}

	REQUIRE(narrow.Hex.IsWaiting());
	CHECK_FALSE(narrow.FindText("Reading…", kInterfaceFont).has_value());
	CHECK(narrow.FindText("Offset").has_value());
}

TEST_CASE("ImGui's keyboard navigation neither selects nor scrolls")
{
	constexpr int kHeldFrames = 5;

	HexScreen screen(Pattern(4096));
	screen.Land();
	REQUIRE((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0);

	SUBCASE("A clicked byte is not activated by Space")
	{
		const std::optional<Found> opening = screen.FindText("41 42 43 44");
		REQUIRE(opening.has_value());
		screen.Click(screen.PartOf(*opening, 2, 2).GetCenter());
		CheckSelection(screen.State.Selection, 1, 1);
	}

	SUBCASE("A clicked grab is not activated by Space")
	{
		const ImU32 idle = ImGui::GetColorU32(ImGuiCol_ScrollbarGrab);
		const std::optional<HorizontalExtent> across = HorizontalExtentInColour(HexScreen::Drawn(), idle);
		const std::optional<VerticalExtent> down = VerticalExtentInColour(HexScreen::Drawn(), idle);
		REQUIRE(across.has_value());
		REQUIRE(down.has_value());
		screen.Click(ImVec2((across->Left + across->Right) * kHalf, (down->Top + down->Bottom) * kHalf));
	}

	REQUIRE(screen.State.Scroll.TopRow == 0);
	const Graphite::HexSelection before = screen.State.Selection;

	// Away from the view, where a press taken for the pointer's would select the last byte and scroll down to it.
	screen.MoveTo(kFarAway);
	screen.PressNavigationKey(ImGuiKey_RightArrow);
	screen.PressNavigationKey(ImGuiKey_Space, kHeldFrames);
	screen.PressNavigationKey(ImGuiKey_Enter);

	CheckSelection(screen.State.Selection, before.Anchor, before.Active);
	CHECK(screen.State.Scroll.TopRow == 0);
}

TEST_CASE("The selected bytes' values are read even when their rows are not on screen")
{
	constexpr std::uint64_t kBlockRows = Graphite::kHexBlockBytes / Graphite::kBytesPerRow;

	const auto hasValues = [](const std::vector<ViewStatusItem>& status, const std::string& values) {
		return std::ranges::find(status, ViewStatusItem{values, true, std::nullopt}) != status.end();
	};

	SUBCASE("After their block has left the cache")
	{
		HexScreen screen(Pattern((Graphite::kHexCachedBlocks + 2) * Graphite::kHexBlockBytes));
		screen.Land();
		REQUIRE(hasValues(screen.Last.Status, "u8 65  u16 16961  u32 1145258561"));

		// The rows of one block after another, more blocks than the cache holds.
		for (std::uint64_t block = 1; block <= Graphite::kHexCachedBlocks + 1; ++block)
		{
			screen.State.Scroll.TopRow = block * kBlockRows;
			screen.Land();
		}

		CHECK(hasValues(screen.Last.Status, "u8 65  u16 16961  u32 1145258561"));
	}

	SUBCASE("When they run on into a block whose rows are not on screen")
	{
		HexScreen screen(Pattern(2 * Graphite::kHexBlockBytes));
		screen.State.Selection = {0xFFFD, 0xFFFD};
		screen.State.Scroll.TopRow = kBlockRows - 100;
		screen.Land();

		CHECK(hasValues(screen.Last.Status, "u8 78  u16 20302  u32 1364217678"));
	}
}
