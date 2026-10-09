#include <doctest/doctest.h>

#include "Core/Text/TextBuffer.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/SyntaxCache.h"

#include <algorithm>
#include <chrono>
#include <string>

namespace {

	constexpr std::size_t kMembers = 3300;

	std::string TenThousandLinesOfCSharp()
	{
		std::string text = "namespace Demo;\npublic class Big\n{\n";

		for (std::size_t memberIndex = 0; memberIndex < kMembers; ++memberIndex)
		{
			const std::string number = std::to_string(memberIndex);
			text += "    public int Field" + number + " = " + number + ";\n";
			text += "    public void Method" + number + "(string label) { System.Console.WriteLine($\"{label}: {Field" + number + "}\"); }\n";
			text += "    // member " + number + "\n";
		}

		text += "}\n";
		return text;
	}

	double MillisecondsSince(std::chrono::steady_clock::time_point start)
	{
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	}
}

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

	WARN(totalKeystrokes / kKeystrokes < 4.0);
}