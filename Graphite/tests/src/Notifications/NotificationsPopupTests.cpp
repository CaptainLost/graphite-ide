#include <doctest/doctest.h>

#include "ImGuiFrames.h"

#include "Editor/StatusBar/StatusBar.h"
#include "Notifications/NotificationHistory.h"
#include "Notifications/NotificationsPopup.h"

#include "Core/Text/DocumentSet.h"

#include <imgui_internal.h>

#include <chrono>
#include <filesystem>
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
			Popup.Render(History, Palette, OpenFolder, *std::chrono::locate_zone("UTC"));
		}

		void Next()
		{
			Frames.Next([&] { Draw(); });
		}

		// A popup sizes itself on the frame it appears and shows from the next.
		void OpenFromBell()
		{
			Popup.OpenFromBell();
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

		// Over the first item of the menu open above the popup.
		static ImVec2 FirstMenuItem()
		{
			const ImGuiContext& context = *ImGui::GetCurrentContext();
			REQUIRE(context.OpenPopupStack.Size == 2);
			const ImGuiWindow* menu = context.OpenPopupStack[1].Window;
			REQUIRE(menu != nullptr);
			return ImVec2(menu->Pos.x + menu->WindowPadding.x + 4.0f, menu->Pos.y + menu->WindowPadding.y + 4.0f);
		}
	};

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
			screen.Popup.OpenFromBell();
		}
		screen.Draw();
	};

	screen.Frames.Next(drawBarAndPopup);
	screen.Frames.Next(drawBarAndPopup);

	const ImGuiWindow* bar = ImGui::FindWindowByName("##StatusBar");
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
