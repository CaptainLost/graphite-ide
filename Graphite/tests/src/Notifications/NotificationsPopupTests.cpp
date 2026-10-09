#include <doctest/doctest.h>

#include "ImGuiFrames.h"

#include "Editor/StatusBar/StatusBar.h"
#include "Notifications/NotificationHistory.h"
#include "Notifications/NotificationsPopup.h"
#include "UI/IconButton.h"

#include "Core/ImGui/Toasts.h"
#include "Core/Text/DocumentSet.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <string>

namespace {
	struct PopupScreen
	{
		ImGuiFrames Frames;
		Graphite::NotificationHistory History;
		Graphite::NotificationsPopup Popup;
		Core::ToastPalette Palette;
		std::filesystem::path OpenFolder = "D:/Repos/Game";

		void Draw()
		{
			Popup.Render(History, Palette, OpenFolder, std::chrono::locate_zone("UTC"));
		}

		void Next()
		{
			Frames.Next([&] { Draw(); });
		}

		// A popup sizes itself on the frame it appears and shows from the next.
		void OpenFromBell()
		{
			Popup.OpenFromBell(Graphite::kStatusBarWindowName);
			Next();
			Next();
		}

		void Click(ImVec2 at, ImGuiMouseButton button = ImGuiMouseButton_Left)
		{
			Frames.Click(at, [&] { Draw(); }, button);
		}

		static int OpenPopups()
		{
			return ImGui::GetCurrentContext()->OpenPopupStack.Size;
		}

		static const ImGuiWindow& PopupWindow()
		{
			const ImGuiContext& context = *ImGui::GetCurrentContext();
			REQUIRE(context.OpenPopupStack.Size >= 1);
			REQUIRE(context.OpenPopupStack[0].Window != nullptr);
			return *context.OpenPopupStack[0].Window;
		}

		static const ImGuiWindow& ListWindow()
		{
			const ImGuiWindow& popup = PopupWindow();
			const ImGuiWindow* list = nullptr;
			for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
			{
				const bool isTheList = window->ParentWindow == &popup && (window->Flags & ImGuiWindowFlags_ChildWindow) != 0;
				if (isTheList)
				{
					list = window;
				}
			}
			REQUIRE(list != nullptr);
			return *list;
		}

		// Inside the first entry, a little in from the list's top-left corner.
		static ImVec2 FirstEntry()
		{
			const ImGuiWindow& list = ListWindow();
			return ImVec2(list.InnerRect.Min.x + 40.0f, list.InnerRect.Min.y + 8.0f);
		}

		static const ImGuiWindow& EntryMenuWindow()
		{
			const ImGuiContext& context = *ImGui::GetCurrentContext();
			REQUIRE(context.OpenPopupStack.Size == 2);
			const ImGuiWindow* menu = context.OpenPopupStack[1].Window;
			REQUIRE(menu != nullptr);
			return *menu;
		}

		// Over the first item of the menu open above the popup.
		static ImVec2 FirstMenuItem()
		{
			const ImGuiWindow& menu = EntryMenuWindow();
			return ImVec2(menu.Pos.x + menu.WindowPadding.x + 4.0f, menu.Pos.y + menu.WindowPadding.y + 4.0f);
		}

		// Measured inside a frame: the text's size needs the font a frame sets up.
		ImVec2 ConfirmationsCheckboxFrame()
		{
			ImVec2 frameCentre;
			Frames.Next([&] {
				Draw();
				const ImGuiWindow& popup = PopupWindow();
				const ImGuiStyle& style = ImGui::GetStyle();
				const float frameSide = ImGui::GetFrameHeight();
				const float labelWidth = ImGui::CalcTextSize("Show confirmations").x;
				const float contentRight = popup.Pos.x + popup.Size.x - popup.WindowPadding.x;
				const float frameLeft = contentRight - Graphite::IconButtonSize() - style.ItemSpacing.x - labelWidth -
										style.ItemInnerSpacing.x - frameSide;
				frameCentre = ImVec2(frameLeft + frameSide * 0.5f, popup.Pos.y + popup.WindowPadding.y + frameSide * 0.5f);
			});
			return frameCentre;
		}
	};

	struct HorizontalExtent
	{
		float Left = FLT_MAX;
		float Right = -FLT_MAX;
	};

	HorizontalExtent DrawnExtentOf(const ImGuiWindow& window)
	{
		HorizontalExtent extent;
		for (const ImDrawVert& vertex : window.DrawList->VtxBuffer)
		{
			extent.Left = std::min(extent.Left, vertex.pos.x);
			extent.Right = std::max(extent.Right, vertex.pos.x);
		}
		return extent;
	}

	struct VerticalExtent
	{
		float Top = FLT_MAX;
		float Bottom = -FLT_MAX;
	};

	ImU32 OpaqueColourOf(const Core::Color& colour)
	{
		return ImGui::ColorConvertFloat4ToU32(ImVec4(colour.R, colour.G, colour.B, 1.0f));
	}

	// Alpha is left out of the comparison: an anti-aliased edge is drawn in the shape's colour at
	// no alpha.
	bool HasColourOf(const ImDrawVert& vertex, ImU32 colour)
	{
		return (vertex.col & ~IM_COL32_A_MASK) == (colour & ~IM_COL32_A_MASK);
	}

	VerticalExtent DrawnExtentInColour(const ImGuiWindow& window, ImU32 colour)
	{
		VerticalExtent extent;
		for (const ImDrawVert& vertex : window.DrawList->VtxBuffer)
		{
			if (HasColourOf(vertex, colour))
			{
				extent.Top = std::min(extent.Top, vertex.pos.y);
				extent.Bottom = std::max(extent.Bottom, vertex.pos.y);
			}
		}
		return extent;
	}

	bool DrawsInColourWithin(const ImGuiWindow& window, ImU32 colour, ImVec2 areaMin, ImVec2 areaMax)
	{
		for (const ImDrawVert& vertex : window.DrawList->VtxBuffer)
		{
			const bool insideArea = vertex.pos.x >= areaMin.x && vertex.pos.x <= areaMax.x && vertex.pos.y >= areaMin.y &&
									vertex.pos.y <= areaMax.y;
			const bool drawnThereInColour = insideArea && HasColourOf(vertex, colour);
			if (drawnThereInColour)
			{
				return true;
			}
		}
		return false;
	}

	std::string WordsOfText(int wordCount)
	{
		std::string text;
		for (int wordNumber = 1; wordNumber <= wordCount; ++wordNumber)
		{
			text += "word" + std::to_string(wordNumber) + " ";
		}
		return text;
	}

	Graphite::NotificationEntry Entry(std::uint64_t toastId, Core::ToastKind kind, std::string message = "message")
	{
		return {.ToastId = toastId, .Kind = kind, .Title = "Push failed", .Message = std::move(message)};
	}

	std::string LinesOfText(int lineCount)
	{
		std::string text;
		for (int lineNumber = 1; lineNumber <= lineCount; ++lineNumber)
		{
			text += "line " + std::to_string(lineNumber) + "\n";
		}
		return text;
	}

	int DisplayIndexOf(const ImGuiWindow& window)
	{
		const ImVector<ImGuiWindow*>& windowsBackToFront = ImGui::GetCurrentContext()->Windows;
		for (int windowIndex = 0; windowIndex < windowsBackToFront.Size; ++windowIndex)
		{
			if (windowsBackToFront[windowIndex] == &window)
			{
				return windowIndex;
			}
		}
		return -1;
	}

	ImVec2 BellCentre()
	{
		const ImGuiWindow* bar = ImGui::FindWindowByName(Graphite::kStatusBarWindowName);
		REQUIRE(bar != nullptr);
		return ImVec2(bar->Pos.x + bar->Size.x - bar->WindowPadding.x - 3.0f, bar->Pos.y + bar->Size.y * 0.5f);
	}

	void DrawBarOpeningPopup(const Core::DocumentSet& documents, const Graphite::StatusMessages& messages, Graphite::NotificationsPopup& popup)
	{
		const Graphite::StatusBarClick clicked = Graphite::DrawStatusBar(documents, std::nullopt, std::filesystem::path(), messages);
		if (clicked == Graphite::StatusBarClick::Notifications)
		{
			popup.OpenFromBell(Graphite::kStatusBarWindowName);
		}
	}

	void DrawEditorWindow(bool& focusRequested)
	{
		if (focusRequested)
		{
			ImGui::SetNextWindowFocus();
			focusRequested = false;
		}

		ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
		ImGui::SetNextWindowSize(ImVec2(200.0f, 100.0f));
		ImGui::Begin("Editor");
		ImGui::TextUnformatted("text");
		ImGui::End();
	}

	// Drawn before the editor: a window focuses itself on the frame it appears, and the editor
	// is the one that must start with the keyboard.
	void DrawExplorerWindow()
	{
		ImGui::SetNextWindowPos(ImVec2(10.0f, 150.0f));
		ImGui::SetNextWindowSize(ImVec2(200.0f, 100.0f));
		ImGui::Begin("Explorer");
		ImGui::TextUnformatted("files");
		ImGui::End();
	}

	// Over the explorer's empty space, below its one line of text.
	ImVec2 InsideTheExplorer()
	{
		return ImVec2(110.0f, 220.0f);
	}
}

TEST_CASE("The popup over an empty history completes frames without an ImGui assertion")
{
	PopupScreen screen;
	screen.OpenFromBell();

	CHECK(PopupScreen::OpenPopups() == 1);
}

TEST_CASE("The popup holding only hidden confirmations completes frames without an ImGui assertion")
{
	PopupScreen screen;
	screen.History.Record(Entry(1, Core::ToastKind::Success));
	screen.OpenFromBell();

	CHECK(PopupScreen::OpenPopups() == 1);
}

TEST_CASE("An entry about another folder completes frames without an ImGui assertion")
{
	PopupScreen screen;
	Graphite::NotificationEntry entry = Entry(1, Core::ToastKind::Error);
	entry.Folder = "D:/Repos/IDE";
	screen.History.Record(entry);
	screen.OpenFromBell();

	CHECK(PopupScreen::OpenPopups() == 1);
}

TEST_CASE("Opening the popup marks every notification seen")
{
	PopupScreen screen;
	screen.History.Record(Entry(1, Core::ToastKind::Error));
	screen.History.NoteDeparture({.Id = 1, .Reason = Core::ToastDepartureReason::Cleared});
	REQUIRE(screen.History.MostSevereMissed().has_value());

	screen.OpenFromBell();

	CHECK_FALSE(screen.History.MostSevereMissed().has_value());
}

TEST_CASE("A notification missed while the popup is open is seen at once")
{
	PopupScreen screen;
	screen.OpenFromBell();

	screen.History.Record(Entry(1, Core::ToastKind::Warning));
	screen.History.NoteDeparture({.Id = 1, .Reason = Core::ToastDepartureReason::Expired});
	screen.Next();

	CHECK_FALSE(screen.History.MostSevereMissed().has_value());
}

TEST_CASE("A click on a long entry expands it and a second click collapses it")
{
	PopupScreen screen;
	screen.History.Record(Entry(1, Core::ToastKind::Error, LinesOfText(12)));
	screen.OpenFromBell();

	screen.Click(PopupScreen::FirstEntry());
	CHECK(screen.Popup.IsExpanded(1));

	screen.Next();
	screen.Click(PopupScreen::FirstEntry());
	CHECK_FALSE(screen.Popup.IsExpanded(1));
}

TEST_CASE("The popup opens at the height it keeps, with the whole list inside it")
{
	PopupScreen screen;
	screen.History.Record(Entry(1, Core::ToastKind::Error, LinesOfText(12)));
	screen.History.Record(Entry(2, Core::ToastKind::Warning));
	screen.OpenFromBell();

	const ImGuiWindow& popup = PopupScreen::PopupWindow();
	const ImGuiWindow& list = PopupScreen::ListWindow();
	const float heightOnOpening = popup.Size.y;
	CHECK(list.Pos.y + list.Size.y <= popup.Pos.y + popup.Size.y - popup.WindowPadding.y);

	screen.Next();
	screen.Next();

	CHECK(PopupScreen::PopupWindow().Size.y == heightOnOpening);
}

TEST_CASE("An expanded entry is drawn at its full height on the frame after the click")
{
	PopupScreen screen;
	screen.History.Record(Entry(1, Core::ToastKind::Error, LinesOfText(12)));
	screen.OpenFromBell();
	const float collapsedHeight = PopupScreen::PopupWindow().Size.y;

	screen.Click(PopupScreen::FirstEntry());
	screen.Next();
	const float heightAfterTheClick = PopupScreen::PopupWindow().Size.y;
	screen.Next();
	screen.Next();

	CHECK(heightAfterTheClick > collapsedHeight);
	CHECK(PopupScreen::PopupWindow().Size.y == heightAfterTheClick);
}

TEST_CASE("A list taller than its share of the window scrolls, with its rows clear of the scrollbar")
{
	PopupScreen screen;
	for (std::uint64_t toastId = 1; toastId <= 10; ++toastId)
	{
		screen.History.Record(Entry(toastId, Core::ToastKind::Error, LinesOfText(6)));
	}
	screen.OpenFromBell();

	const ImGuiWindow& list = PopupScreen::ListWindow();
	const float highestListHeight = ImGui::GetMainViewport()->WorkSize.y * 0.6f;
	CHECK(list.Size.y <= highestListHeight);
	CHECK(list.ScrollbarY);
	CHECK(list.ContentSize.x <= list.InnerRect.GetWidth());
}

TEST_CASE("A click on a short entry leaves it as it is")
{
	PopupScreen screen;
	screen.History.Record(Entry(1, Core::ToastKind::Error, "one line"));
	screen.OpenFromBell();

	screen.Click(PopupScreen::FirstEntry());

	CHECK_FALSE(screen.Popup.IsExpanded(1));
}

TEST_CASE("Copy in an entry's menu copies the title and the message")
{
	PopupScreen screen;
	screen.History.Record(Entry(1, Core::ToastKind::Error, "rejected"));
	std::string copied;
	screen.Popup.OnCopied = [&](std::string_view text) { copied = std::string(text); };
	screen.OpenFromBell();

	screen.Click(PopupScreen::FirstEntry(), ImGuiMouseButton_Right);
	screen.Next();
	screen.Click(PopupScreen::FirstMenuItem());

	CHECK(copied == "Push failed\nrejected");
}

TEST_CASE("Copy whose handler records a notification copies the entry it was chosen on")
{
	PopupScreen screen;
	screen.History.Record(Entry(1, Core::ToastKind::Error, "rejected"));
	screen.History.Record(Entry(2, Core::ToastKind::Warning, "behind"));
	std::string copied;
	screen.Popup.OnCopied = [&](std::string_view text) {
		copied = std::string(text);
		screen.History.Record(Entry(3, Core::ToastKind::Success, "Copied"));
	};
	screen.OpenFromBell();

	screen.Click(PopupScreen::FirstEntry(), ImGuiMouseButton_Right);
	screen.Next();
	screen.Click(PopupScreen::FirstMenuItem());

	CHECK(copied == "Push failed\nbehind");
	CHECK(screen.History.Entries(true).size() == 3);
}

TEST_CASE("Esc closes the popup")
{
	PopupScreen screen;
	screen.OpenFromBell();

	screen.Frames.PressKey(ImGuiKey_Escape, [&] { screen.Draw(); });

	CHECK(PopupScreen::OpenPopups() == 0);
}

TEST_CASE("Esc with an entry's menu open closes the menu and leaves the popup")
{
	PopupScreen screen;
	screen.History.Record(Entry(1, Core::ToastKind::Error));
	screen.OpenFromBell();
	screen.Click(PopupScreen::FirstEntry(), ImGuiMouseButton_Right);
	screen.Next();
	REQUIRE(PopupScreen::OpenPopups() == 2);

	screen.Frames.PressKey(ImGuiKey_Escape, [&] { screen.Draw(); });

	CHECK(PopupScreen::OpenPopups() == 1);
}

TEST_CASE("The bell opens the popup and a second click on it closes the popup without reopening it")
{
	PopupScreen screen;
	Core::DocumentSet documents;
	Graphite::StatusMessages messages;
	bool bellClicked = false;

	const std::function<void()> drawBarAndPopup = [&] {
		const Graphite::StatusBarClick clicked =
			Graphite::DrawStatusBar(documents, std::nullopt, std::filesystem::path(), messages);
		if (clicked == Graphite::StatusBarClick::Notifications)
		{
			bellClicked = true;
			screen.Popup.OpenFromBell(Graphite::kStatusBarWindowName);
		}
		screen.Draw();
	};

	screen.Frames.Next(drawBarAndPopup);
	screen.Frames.Next(drawBarAndPopup);

	const ImGuiWindow* bar = ImGui::FindWindowByName(Graphite::kStatusBarWindowName);
	REQUIRE(bar != nullptr);
	const ImVec2 bell(bar->Pos.x + bar->Size.x - bar->WindowPadding.x - 3.0f, bar->Pos.y + bar->Size.y * 0.5f);

	screen.Frames.Click(bell, drawBarAndPopup);
	screen.Frames.Next(drawBarAndPopup);
	REQUIRE(bellClicked);
	REQUIRE(PopupScreen::OpenPopups() == 1);

	bellClicked = false;
	screen.Frames.Click(bell, drawBarAndPopup);
	screen.Frames.Next(drawBarAndPopup);
	screen.Frames.Next(drawBarAndPopup);

	CHECK_FALSE(bellClicked);
	CHECK(PopupScreen::OpenPopups() == 0);
}

TEST_CASE("A toast shown while the popup is open is drawn beneath the popup")
{
	PopupScreen screen;
	Core::Toasts toasts;
	const std::function<void()> drawPopupThenToasts = [&] {
		screen.Draw();
		toasts.Render(screen.Palette);
	};

	screen.Popup.OpenFromBell(Graphite::kStatusBarWindowName);
	screen.Frames.Next(drawPopupThenToasts);
	screen.Frames.Next(drawPopupThenToasts);

	const std::uint64_t toastId = toasts.Show(Core::ToastKind::Error, "Push failed", "rejected");
	screen.Frames.Next(drawPopupThenToasts);
	screen.Frames.Next(drawPopupThenToasts);
	screen.Frames.Next(drawPopupThenToasts);

	const ImGuiWindow* toastWindow = ImGui::FindWindowByName(std::format("##toast{}", toastId).c_str());
	REQUIRE(toastWindow != nullptr);
	REQUIRE(PopupScreen::OpenPopups() == 1);
	CHECK(DisplayIndexOf(PopupScreen::PopupWindow()) > DisplayIndexOf(*toastWindow));
}

TEST_CASE("Esc on the popup opened from the bell hands the keyboard back to the editor")
{
	PopupScreen screen;
	Core::DocumentSet documents;
	Graphite::StatusMessages messages;
	bool editorFocusRequested = true;
	const std::function<void()> drawBarEditorAndPopup = [&] {
		DrawBarOpeningPopup(documents, messages, screen.Popup);
		DrawEditorWindow(editorFocusRequested);
		screen.Draw();
	};

	screen.Frames.Next(drawBarEditorAndPopup);
	screen.Frames.Next(drawBarEditorAndPopup);
	const ImGuiWindow* editor = ImGui::FindWindowByName("Editor");
	REQUIRE(editor != nullptr);
	REQUIRE(ImGui::GetCurrentContext()->NavWindow == editor);

	screen.Frames.Click(BellCentre(), drawBarEditorAndPopup);
	screen.Frames.Next(drawBarEditorAndPopup);
	REQUIRE(PopupScreen::OpenPopups() == 1);

	screen.Frames.PressKey(ImGuiKey_Escape, drawBarEditorAndPopup);

	REQUIRE(PopupScreen::OpenPopups() == 0);
	CHECK(ImGui::GetCurrentContext()->NavWindow == editor);
}

namespace {
	struct BellScreen
	{
		PopupScreen Screen;
		Core::DocumentSet Documents;
		Graphite::StatusMessages Messages;
		bool EditorFocusRequested = true;

		void Draw()
		{
			DrawBarOpeningPopup(Documents, Messages, Screen.Popup);
			DrawExplorerWindow();
			DrawEditorWindow(EditorFocusRequested);
			Screen.Draw();
		}

		void Next()
		{
			Screen.Frames.Next([&] { Draw(); });
		}

		void Click(ImVec2 at)
		{
			Screen.Frames.Click(at, [&] { Draw(); });
		}

		static const ImGuiWindow* KeyboardHolder()
		{
			return ImGui::GetCurrentContext()->NavWindow;
		}
	};
}

TEST_CASE("A second click on the bell closes the popup and hands the keyboard back to the editor")
{
	BellScreen bell;
	bell.Next();
	bell.Next();
	const ImGuiWindow* editor = ImGui::FindWindowByName("Editor");
	REQUIRE(editor != nullptr);
	REQUIRE(BellScreen::KeyboardHolder() == editor);

	bell.Click(BellCentre());
	bell.Next();
	REQUIRE(PopupScreen::OpenPopups() == 1);

	bell.Click(BellCentre());
	bell.Next();

	REQUIRE(PopupScreen::OpenPopups() == 0);
	CHECK(BellScreen::KeyboardHolder() == editor);
}

TEST_CASE("A click in another window closes the popup and leaves the keyboard in that window")
{
	BellScreen bell;
	bell.Next();
	bell.Next();
	const ImGuiWindow* explorer = ImGui::FindWindowByName("Explorer");
	REQUIRE(explorer != nullptr);
	REQUIRE(BellScreen::KeyboardHolder() == ImGui::FindWindowByName("Editor"));

	bell.Click(BellCentre());
	bell.Next();
	REQUIRE(PopupScreen::OpenPopups() == 1);

	bell.Click(InsideTheExplorer());
	bell.Next();

	REQUIRE(PopupScreen::OpenPopups() == 0);
	CHECK(BellScreen::KeyboardHolder() == explorer);
}

TEST_CASE("The popup says it is open from the frame it opens until Esc closes it")
{
	PopupScreen screen;
	screen.Next();
	CHECK_FALSE(screen.Popup.IsOpen());

	screen.OpenFromBell();
	CHECK(screen.Popup.IsOpen());

	screen.Frames.PressKey(ImGuiKey_Escape, [&] { screen.Draw(); });
	CHECK_FALSE(screen.Popup.IsOpen());
}

TEST_CASE("Clear in the popup's header empties the history")
{
	PopupScreen screen;
	screen.History.Record(Entry(1, Core::ToastKind::Error));
	screen.OpenFromBell();

	const ImGuiWindow& popup = PopupScreen::PopupWindow();
	const ImVec2 clearButton(popup.Pos.x + popup.Size.x - popup.WindowPadding.x - 6.0f,
							 popup.Pos.y + popup.WindowPadding.y + ImGui::GetFrameHeight() * 0.5f);
	screen.Click(clearButton);

	CHECK(screen.History.Entries(true).empty());
}

TEST_CASE("The popup is padded as a toast card is, with its list filling the space inside the padding")
{
	PopupScreen screen;
	screen.History.Record(Entry(1, Core::ToastKind::Error, LinesOfText(12)));
	screen.OpenFromBell();

	const ImGuiWindow& popup = PopupScreen::PopupWindow();
	const ImGuiWindow& list = PopupScreen::ListWindow();
	const float toastCardPaddingX = 14.0f;
	const float toastCardPaddingY = 12.0f;

	CHECK(popup.WindowPadding.x == toastCardPaddingX);
	CHECK(popup.WindowPadding.y == toastCardPaddingY);
	CHECK(list.Pos.x == doctest::Approx(popup.Pos.x + toastCardPaddingX));
	CHECK(list.Pos.x + list.Size.x == doctest::Approx(popup.Pos.x + popup.Size.x - toastCardPaddingX));
	CHECK(list.ContentSize.x == doctest::Approx(list.InnerRect.GetWidth()));
	CHECK(list.Pos.y + list.Size.y == doctest::Approx(popup.Pos.y + popup.Size.y - toastCardPaddingY));
}

TEST_CASE("An entry's menu keeps the default window padding")
{
	PopupScreen screen;
	screen.History.Record(Entry(1, Core::ToastKind::Error));
	screen.OpenFromBell();
	screen.Click(PopupScreen::FirstEntry(), ImGuiMouseButton_Right);
	screen.Next();

	const ImGuiWindow& menu = PopupScreen::EntryMenuWindow();
	CHECK(menu.WindowPadding.x == ImGui::GetStyle().WindowPadding.x);
	CHECK(menu.WindowPadding.y == ImGui::GetStyle().WindowPadding.y);
}

TEST_CASE("An entry's icon, folder line, message and time keep eight pixels clear of the row's sides")
{
	PopupScreen screen;
	Graphite::NotificationEntry entry = Entry(1, Core::ToastKind::Error, WordsOfText(60));
	entry.Folder = "D:/Repos/" + std::string(200, 'f');
	screen.History.Record(entry);
	screen.OpenFromBell();

	const ImGuiWindow& list = PopupScreen::ListWindow();
	const float rowLeft = list.Pos.x;
	const float rowRight = list.Pos.x + list.Size.x;
	const float rowPadding = 8.0f;

	// The icon's anti-aliased edge reaches half a pixel out of its circle, and the last glyph of
	// the time may stop a pixel short of its advance.
	const float fringe = 1.0f;
	const float glyphBearing = 2.0f;

	const HorizontalExtent drawn = DrawnExtentOf(list);
	CHECK(drawn.Left >= rowLeft + rowPadding - fringe);
	CHECK(drawn.Left <= rowLeft + rowPadding);
	CHECK(drawn.Right <= rowRight - rowPadding);
	CHECK(drawn.Right >= rowRight - rowPadding - glyphBearing);
}

TEST_CASE("An entry's icon keeps eight pixels clear of the top and the bottom of its row")
{
	PopupScreen screen;
	screen.Palette.Error = {.R = 0.9f, .G = 0.2f, .B = 0.2f};
	const std::string noMessage;
	screen.History.Record(Entry(1, Core::ToastKind::Error, noMessage));
	screen.OpenFromBell();

	const ImGuiWindow& list = PopupScreen::ListWindow();
	const float rowTop = list.Pos.y;
	const float rowBottom = list.Pos.y + list.Size.y;
	const float rowInset = 8.0f;

	// The icon's anti-aliased edge reaches half a pixel out of its circle.
	const float fringe = 1.0f;

	const VerticalExtent icon = DrawnExtentInColour(list, OpaqueColourOf(screen.Palette.Error));
	CHECK(icon.Top >= rowTop + rowInset - fringe);
	CHECK(icon.Top <= rowTop + rowInset);
	CHECK(icon.Bottom <= rowBottom - rowInset + fringe);
	CHECK(icon.Bottom >= rowBottom - rowInset);
}

TEST_CASE("A hovered entry is highlighted across the whole row")
{
	PopupScreen screen;
	screen.History.Record(Entry(1, Core::ToastKind::Error));
	screen.OpenFromBell();

	const ImVec2 overTheEntry = PopupScreen::FirstEntry();
	ImGui::GetIO().AddMousePosEvent(overTheEntry.x, overTheEntry.y);
	screen.Next();

	const ImGuiWindow& list = PopupScreen::ListWindow();
	const HorizontalExtent drawn = DrawnExtentOf(list);
	CHECK(drawn.Left <= list.Pos.x);
	CHECK(drawn.Right >= list.Pos.x + list.Size.x);
}

TEST_CASE("Ticking Show confirmations in the header lists the confirmations it hid")
{
	PopupScreen screen;
	screen.History.Record(Entry(1, Core::ToastKind::Success));
	screen.OpenFromBell();
	const float heightWithNothingListed = PopupScreen::PopupWindow().Size.y;

	screen.Click(screen.ConfirmationsCheckboxFrame());
	const ImGuiID checkboxId = ImHashStr("Show confirmations", 0, PopupScreen::PopupWindow().ID);
	CHECK(ImGui::GetCurrentContext()->HoveredId == checkboxId);

	screen.Next();
	CHECK(PopupScreen::PopupWindow().Size.y > heightWithNothingListed);
}

TEST_CASE("The Show confirmations box is outlined in the dim text colour with the pointer away from it")
{
	PopupScreen screen;
	screen.Palette.TextDim = {.R = 0.25f, .G = 0.5f, .B = 0.75f};
	screen.OpenFromBell();

	const ImVec2 boxCentre = screen.ConfirmationsCheckboxFrame();
	const float halfSide = ImGui::GetFrameHeight() * 0.5f;

	// The outline's anti-aliased edge may reach a pixel past the box.
	const float fringe = 1.0f;
	const ImVec2 aroundTheBoxMin(boxCentre.x - halfSide - fringe, boxCentre.y - halfSide - fringe);
	const ImVec2 aroundTheBoxMax(boxCentre.x + halfSide + fringe, boxCentre.y + halfSide + fringe);

	CHECK(DrawsInColourWithin(PopupScreen::PopupWindow(), OpaqueColourOf(screen.Palette.TextDim), aroundTheBoxMin,
							  aroundTheBoxMax));
}
