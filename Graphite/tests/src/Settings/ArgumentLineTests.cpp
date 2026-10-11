#include <doctest/doctest.h>

#include "Settings/ArgumentLine.h"

#include <ostream>
#include <string>
#include <vector>

TEST_CASE("An argument line splits on blanks outside quotes")
{
	CHECK(Graphite::SplitArgumentLine("--log=verbose  \t--pretty") == std::vector<std::string>{"--log=verbose", "--pretty"});
	CHECK(Graphite::SplitArgumentLine("   ").empty());
}

TEST_CASE("Quotes keep blanks inside one argument and an empty pair is an empty argument")
{
	CHECK(Graphite::SplitArgumentLine(R"(--name "two words" "")") == std::vector<std::string>{"--name", "two words", ""});
}

TEST_CASE("Backslashes are literal unless a quote follows them")
{
	CHECK(Graphite::SplitArgumentLine(R"(C:\Tools\ "a\"b" "c:\dir\\")") == std::vector<std::string>{R"(C:\Tools\)", R"(a"b)", R"(c:\dir\)"});
}

TEST_CASE("Joined arguments split back into the same arguments")
{
	const std::vector<std::string> arguments{"plain", "two words", "", R"(a"b)", R"(C:\dir\)", "tab\there"};

	CHECK(Graphite::SplitArgumentLine(Graphite::JoinArgumentLine(arguments)) == arguments);
	CHECK(Graphite::JoinArgumentLine({"--log=verbose", "two words"}) == R"(--log=verbose "two words")");
}
