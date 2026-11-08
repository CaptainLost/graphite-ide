#include <doctest/doctest.h>

#include "Editor/Navigation/LineTarget.h"

using Graphite::LineTarget;
using Graphite::ParseLineTarget;

TEST_CASE("Go to Line reads a line, or a line and a column, counting from one")
{
	CHECK(ParseLineTarget("42") == LineTarget{.Line = 41});
	CHECK(ParseLineTarget(" 42 ") == LineTarget{.Line = 41});
	CHECK(ParseLineTarget("42:7") == LineTarget{.Line = 41, .Column = 6});
	CHECK(ParseLineTarget("42:") == LineTarget{.Line = 41});
}

TEST_CASE("Go to Line refuses text that names no line")
{
	CHECK_FALSE(ParseLineTarget("").has_value());
	CHECK_FALSE(ParseLineTarget("0").has_value());
	CHECK_FALSE(ParseLineTarget("abc").has_value());
	CHECK_FALSE(ParseLineTarget("12x").has_value());
	CHECK_FALSE(ParseLineTarget("3:y").has_value());
	CHECK_FALSE(ParseLineTarget("-4").has_value());
}
