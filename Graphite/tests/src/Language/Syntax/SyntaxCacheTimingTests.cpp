#include <doctest/doctest.h>

#include "Core/Text/TextBuffer.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/SyntaxCache.h"

#include "SyntaxFixtures.h"

#include <algorithm>
#include <chrono>
#include <string>

TEST_CASE("Typing in a ten-thousand-line C# file costs less than the per-keystroke budget" * doctest::skip())
{
	Core::TextBuffer buffer{TenThousandLinesOfCSharp()};
	Graphite::SyntaxCache cache;
	cache.SetSyntax(Graphite::SyntaxOf(Graphite::LanguageId::CSharp));
	cache.SetParseBudget(Graphite::ParseBudget{.PerAttempt = std::chrono::seconds{10}, .PauseAfterRunningOut = std::chrono::microseconds{0}});

	const std::size_t middleLine = buffer.GetLineCount() / 2;

	const std::chrono::steady_clock::time_point firstParseStart = std::chrono::steady_clock::now();
	REQUIRE_FALSE(cache.TokensOf(buffer, middleLine).empty());
	const double firstParse = MillisecondsSince(firstParseStart);

	constexpr int kKeystrokes = 20;
	constexpr double kKeystrokeBudgetMilliseconds = 4.0;
	double slowestKeystroke = 0.0;
	double totalKeystrokes = 0.0;

	for (int keystroke = 0; keystroke < kKeystrokes; ++keystroke)
	{
		buffer.Insert(buffer.LineStart(middleLine) + 4, "x");

		const std::chrono::steady_clock::time_point keystrokeStart = std::chrono::steady_clock::now();
		REQUIRE_FALSE(cache.TokensOf(buffer, middleLine).empty());
		const double keystrokeCost = MillisecondsSince(keystrokeStart);

		slowestKeystroke = std::max(slowestKeystroke, keystrokeCost);
		totalKeystrokes += keystrokeCost;
	}

	MESSAGE("lines: ", buffer.GetLineCount(), ", bytes: ", buffer.GetLength());
	MESSAGE("first parse: ", firstParse, " ms");
	MESSAGE("keystroke average: ", totalKeystrokes / kKeystrokes, " ms, slowest: ", slowestKeystroke, " ms");

	WARN(totalKeystrokes / kKeystrokes < kKeystrokeBudgetMilliseconds);
}