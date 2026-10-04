#include <doctest/doctest.h>

#include "UI/ClipboardCopy.h"
#include "Editor/StatusBar/StatusMessageEvent.h"
#include "UI/ToastEvent.h"

#include "Core/Events/EventBus.h"

#include <string>
#include <vector>

using Graphite::CopiedSummary;

TEST_CASE("CopiedSummary names what was copied and shows it")
{
	CHECK(CopiedSummary("path", "D:\\Repos\\a.cpp") == "path: D:\\Repos\\a.cpp");
	CHECK(CopiedSummary("commit hash", "1a2b3c4") == "commit hash: 1a2b3c4");
}

TEST_CASE("CopiedSummary shows only the first line of a longer text")
{
	CHECK(CopiedSummary("commit message", "Fix it\n\nThe body") == "commit message: Fix it\xE2\x80\xA6");
	CHECK(CopiedSummary("error", "Cannot write\r\nmore") == "error: Cannot write\xE2\x80\xA6");
}

TEST_CASE("CopiedSummary leaves the text out when there is none to show")
{
	CHECK(CopiedSummary("file name", "") == "file name");
	CHECK(CopiedSummary("commit message", "\nbody only") == "commit message");
}

TEST_CASE("CopiedSummary cuts a long text on a character boundary")
{
	// 79 ASCII bytes, then a two-byte character straddling the 80-byte limit.
	const std::string text = std::string(79, 'a') + "\xC5\x82" + "tail";

	CHECK(CopiedSummary("path", text) == "path: " + std::string(79, 'a') + "\xE2\x80\xA6");
}

TEST_CASE("CopiedSummary keeps a text of exactly the limit whole")
{
	const std::string text(80, 'a');

	CHECK(CopiedSummary("path", text) == "path: " + text);
}

TEST_CASE("ReportCopied confirms a copy with a Success toast titled Copied and leaves the status bar alone")
{
	Core::EventBus events;
	std::vector<Graphite::ToastEvent> toasts;
	int statusMessages = 0;

	events.Subscribe<Graphite::ToastEvent>([&](const Graphite::ToastEvent& event) { toasts.push_back(event); });
	events.Subscribe<Graphite::StatusMessageEvent>([&](const Graphite::StatusMessageEvent&) { ++statusMessages; });

	Graphite::ReportCopied(events, "selection", "int main()\n{");

	REQUIRE(toasts.size() == 1);
	CHECK(toasts.front().Kind == Core::ToastKind::Success);
	CHECK(toasts.front().Title == "Copied");
	CHECK(toasts.front().Message == CopiedSummary("selection", "int main()\n{"));
	CHECK(toasts.front().CopyText.empty());

	// A copy is not about the open folder: closing it must not take the confirmation away.
	CHECK(toasts.front().Scope == Graphite::ToastScope::Application);
	CHECK(statusMessages == 0);
}

TEST_CASE("A folder toast and an application toast are shown in different groups")
{
	CHECK(Graphite::ToastGroupOf(Graphite::ToastScope::Application) == 0);
	CHECK(Graphite::ToastGroupOf(Graphite::ToastScope::Folder) != 0);
}
