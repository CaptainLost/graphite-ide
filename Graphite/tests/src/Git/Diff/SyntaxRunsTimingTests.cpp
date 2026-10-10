#include <doctest/doctest.h>

#include "Git/Diff/SyntaxRuns.h"

#include "SyntaxFixtures.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

TEST_CASE("Colouring one side of a ten-thousand-line C# diff takes one whole parse" * doctest::skip())
{
	const std::string text = TenThousandLinesOfCSharp();

	constexpr int kRuns = 5;
	double slowestRun = 0.0;
	double totalRuns = 0.0;
	std::size_t colouredLines = 0;

	for (int run = 0; run < kRuns; ++run)
	{
		const std::chrono::steady_clock::time_point runStart = std::chrono::steady_clock::now();
		const std::vector<std::vector<Graphite::Token>> lines = Graphite::ColourFileText(text, "Big.cs");
		const double runCost = MillisecondsSince(runStart);

		colouredLines = lines.size();
		slowestRun = std::max(slowestRun, runCost);
		totalRuns += runCost;
	}

	REQUIRE(colouredLines > 9000);

	MESSAGE("lines: ", colouredLines, ", bytes: ", text.size());
	MESSAGE("one side average: ", totalRuns / kRuns, " ms, slowest: ", slowestRun, " ms");

	WARN(totalRuns / kRuns < 250.0);
}
