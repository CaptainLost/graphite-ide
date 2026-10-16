#include <doctest/doctest.h>

#include "Core/Base/Strings.h"

#include <cstddef>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
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

TEST_CASE("SplitLines keeps every line, the empty ones and the one after a final newline included")
{
	CHECK(Core::SplitLines("a\n\nb\n") == std::vector<std::string_view>{"a", "", "b", ""});
	CHECK(Core::SplitLines("") == std::vector<std::string_view>{""});
	CHECK(Core::SplitLines("one line") == std::vector<std::string_view>{"one line"});
}

TEST_CASE("SplitLines drops a carriage return that ends a line and keeps one anywhere else")
{
	CHECK(Core::SplitLines("a\r\nb\r\n") == std::vector<std::string_view>{"a", "b", ""});
	CHECK(Core::SplitLines("a\rb") == std::vector<std::string_view>{"a\rb"});
	CHECK(Core::SplitLines("last\r") == std::vector<std::string_view>{"last"});
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

TEST_CASE("Counted puts the count before the noun and adds an s for any count but one")
{
	CHECK(Core::Counted(1, "file") == "1 file");
	CHECK(Core::Counted(0, "file") == "0 files");
	CHECK(Core::Counted(std::size_t{3}, "untracked file") == "3 untracked files");
}

TEST_CASE("Counted takes the plural it is given where an s would be wrong")
{
	CHECK(Core::Counted(1, "more match", "more matches") == "1 more match");
	CHECK(Core::Counted(2LL, "more match", "more matches") == "2 more matches");
}