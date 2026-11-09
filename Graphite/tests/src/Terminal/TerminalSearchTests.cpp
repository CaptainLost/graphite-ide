#include <doctest/doctest.h>

#include "Terminal/TerminalSearch.h"

#include "Core/Terminal/TerminalScreen.h"

#include <optional>
#include <string_view>
#include <vector>

using Graphite::FindAllInTerminal;
using Graphite::NextTerminalMatch;
using Graphite::TerminalMatch;
using Graphite::TerminalSearchDirection;

namespace {
	void PrintLine(Core::TerminalScreen& screen, std::string_view text)
	{
		for (const char character : text)
		{
			screen.Print(static_cast<char32_t>(character), 1);
		}

		screen.CarriageReturn();
		screen.LineFeed();
	}
}

TEST_CASE("A terminal search finds the text in the scrollback and on the screen with case ignored")
{
	Core::TerminalScreen screen(20, 2);
	PrintLine(screen, "Build failed");
	PrintLine(screen, "ok");
	PrintLine(screen, "build FAILED again");

	const std::vector<TerminalMatch> matches = FindAllInTerminal(screen, 20, "failed");

	REQUIRE(matches.size() == 2);
	CHECK(matches[0].FromColumn == 6);
	CHECK(matches[0].ToColumn == 12);
	CHECK(matches[1].Line > matches[0].Line);
	CHECK(matches[1].FromColumn == 6);
	CHECK(FindAllInTerminal(screen, 20, "").empty());
	CHECK(FindAllInTerminal(screen, 20, "missing").empty());
}

TEST_CASE("A match after a wide character covers its cells and not its bytes")
{
	Core::TerminalScreen screen(10, 1);
	screen.Print(U'界', 2);
	screen.Print(U'a', 1);
	screen.Print(U'b', 1);

	const std::vector<TerminalMatch> matches = FindAllInTerminal(screen, 10, "ab");

	REQUIRE(matches.size() == 1);
	CHECK(matches[0].FromColumn == 2);
	CHECK(matches[0].ToColumn == 4);
}

TEST_CASE("Stepping through terminal matches starts at the newest and wraps round at either end")
{
	const std::vector<TerminalMatch> matches = {{.Line = 1, .FromColumn = 0, .ToColumn = 2},
												{.Line = 3, .FromColumn = 4, .ToColumn = 6},
												{.Line = 7, .FromColumn = 1, .ToColumn = 3}};

	CHECK(NextTerminalMatch(matches, std::nullopt, TerminalSearchDirection::Backward) == 2u);
	CHECK(NextTerminalMatch(matches, matches[0], TerminalSearchDirection::Backward) == 2u);
	CHECK(NextTerminalMatch(matches, matches[1], TerminalSearchDirection::Backward) == 0u);
	CHECK(NextTerminalMatch(matches, matches[1], TerminalSearchDirection::Forward) == 2u);
	CHECK(NextTerminalMatch(matches, matches[2], TerminalSearchDirection::Forward) == 0u);
	CHECK_FALSE(NextTerminalMatch({}, std::nullopt, TerminalSearchDirection::Forward).has_value());
}
