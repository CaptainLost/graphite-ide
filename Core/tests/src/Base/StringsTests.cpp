#include <doctest/doctest.h>

#include "Core/Base/Strings.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

TEST_CASE("JoinedWith puts the separator between parts and nowhere else, empty parts included")
{
	CHECK(Core::JoinedWith(std::vector<std::string>{}, ", ").empty());
	CHECK(Core::JoinedWith(std::vector<std::string>{"a"}, ", ") == "a");
	CHECK(Core::JoinedWith(std::vector<std::string>{"a", "b", "c"}, ", ") == "a, b, c");
	CHECK(Core::JoinedWith(std::vector<std::string>{"", "x"}, ", ") == ", x");
	CHECK(Core::JoinedWith(std::vector<std::string>{"x", ""}, ", ") == "x, ");

	const std::vector<std::string> names{"a", "b", "c"};
	CHECK(Core::JoinedWith(std::span(names).first(2), "|") == "a|b");
	CHECK(Core::JoinedWith(std::vector<std::string>{std::string{"a\0b", 3}}, "") == std::string{"a\0b", 3});
}

TEST_CASE("AppendSeparated puts the separator in only after text, and appends an empty part as well")
{
	std::string text;
	Core::AppendSeparated(text, ", ", "");
	CHECK(text.empty());
	Core::AppendSeparated(text, ", ", "a");
	CHECK(text == "a");
	Core::AppendSeparated(text, ", ", "");
	CHECK(text == "a, ");
	Core::AppendSeparated(text, ", ", "b");
	CHECK(text == "a, , b");
}

TEST_CASE("AppendSeparatedIfNotEmpty skips an empty part entirely")
{
	std::string text = "a";
	Core::AppendSeparatedIfNotEmpty(text, ", ", "");
	CHECK(text == "a");
	Core::AppendSeparatedIfNotEmpty(text, ", ", "b");
	CHECK(text == "a, b");

	std::string empty;
	Core::AppendSeparatedIfNotEmpty(empty, ", ", "x");
	CHECK(empty == "x");
}

TEST_CASE("SplitSkippingEmpty never yields an empty segment")
{
	CHECK(Core::SplitSkippingEmpty("", '/').empty());
	CHECK(Core::SplitSkippingEmpty("/", '/').empty());
	CHECK(Core::SplitSkippingEmpty("a//b/", '/') == std::vector<std::string>{"a", "b"});
	CHECK(Core::SplitSkippingEmpty("\\a\\b", '\\') == std::vector<std::string>{"a", "b"});
	CHECK(Core::SplitSkippingEmpty("a/b", '\\') == std::vector<std::string>{"a/b"});
}

TEST_CASE("A number is read whole or not at all and a minus sign for an unsigned target gives nothing rather than a wrapped value")
{
	CHECK(Core::ParseNumber<std::size_t>("42") == 42);
	CHECK(Core::ParseNumber<float>("14.500000") == 14.5f);

	CHECK_FALSE(Core::ParseNumber<std::size_t>("-1"));
	CHECK_FALSE(Core::ParseNumber<std::size_t>(""));
	CHECK_FALSE(Core::ParseNumber<std::size_t>("12abc"));
	CHECK_FALSE(Core::ParseNumber<std::size_t>("abc"));
	CHECK(Core::ParseNumber<int>("-1") == -1);
	CHECK_FALSE(Core::ParseNumber<int>(" 1"));
	CHECK_FALSE(Core::ParseNumber<int>("+1"));
	CHECK_FALSE(Core::ParseNumber<int>("99999999999"));
	CHECK(Core::ParseNumber<double>("0.5") == 0.5);
	CHECK_FALSE(Core::ParseNumber<double>("1e400"));
}

TEST_CASE("FirstLine stops at the first newline and keeps a carriage return before it")
{
	static_assert(Core::FirstLine("").empty());
	static_assert(Core::FirstLine("abc") == "abc");
	static_assert(Core::FirstLine("a\nb") == "a");
	static_assert(Core::FirstLine("a\r\nb") == "a\r");
	static_assert(Core::FirstLine("\nb").empty());
}
