#include <doctest/doctest.h>

#include "Editor/StatusBar/ClipboardCopy.h"
#include "Editor/StatusBar/StatusMessageEvent.h"

#include "Core/Events/EventBus.h"

#include <string>
#include <vector>

using Graphite::CopiedMessage;

TEST_CASE("CopiedMessage names what was copied and shows it")
{
	CHECK(CopiedMessage("path", "D:\\Repos\\a.cpp") == "Copied path: D:\\Repos\\a.cpp");
	CHECK(CopiedMessage("commit hash", "1a2b3c4") == "Copied commit hash: 1a2b3c4");
}

TEST_CASE("CopiedMessage shows only the first line of a longer text")
{
	CHECK(CopiedMessage("commit message", "Fix it\n\nThe body") ==
		  "Copied commit message: Fix it\xE2\x80\xA6");
	CHECK(CopiedMessage("error", "Cannot write\r\nmore") == "Copied error: Cannot write\xE2\x80\xA6");
}

TEST_CASE("CopiedMessage leaves the text out when there is none to show")
{
	CHECK(CopiedMessage("file name", "") == "Copied file name");
	CHECK(CopiedMessage("commit message", "\nbody only") == "Copied commit message");
}

TEST_CASE("CopiedMessage cuts a long text on a character boundary")
{
	// 79 ASCII bytes, then a two-byte character straddling the 80-byte limit.
	const std::string text = std::string(79, 'a') + "\xC5\x82" + "tail";

	CHECK(CopiedMessage("path", text) == "Copied path: " + std::string(79, 'a') + "\xE2\x80\xA6");
}

TEST_CASE("CopiedMessage keeps a text of exactly the limit whole")
{
	const std::string text(80, 'a');

	CHECK(CopiedMessage("path", text) == "Copied path: " + text);
}

TEST_CASE("ReportCopied confirms a copy already on the clipboard in CopiedMessage's words")
{
	Core::EventBus events;
	std::vector<std::string> shown;

	events.Subscribe<Graphite::StatusMessageEvent>(
		[&](const Graphite::StatusMessageEvent& event) { shown.push_back(event.Text); });

	Graphite::ReportCopied(events, "selection", "int main()\n{");

	REQUIRE(shown.size() == 1);
	CHECK(shown.front() == CopiedMessage("selection", "int main()\n{"));
}
