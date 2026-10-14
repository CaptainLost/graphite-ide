#include <doctest/doctest.h>

#include "Git/Changes/LineSpans.h"

using Graphite::ChangedSpan;
using Graphite::RemovedSpan;

TEST_CASE("Identical lines have nothing removed")
{
	CHECK(RemovedSpan("same text", "same text") == ChangedSpan{9, 9});
}

TEST_CASE("A word replaced in the middle is the removed span")
{
	CHECK(RemovedSpan("int count = 0;", "int total = 0;") == ChangedSpan{4, 9});
}

TEST_CASE("Text removed at the end is the removed span")
{
	CHECK(RemovedSpan("call(a, b);", "call(a);") == ChangedSpan{6, 9});
}

TEST_CASE("Text only inserted leaves an empty span where it went in")
{
	CHECK(RemovedSpan("call(a);", "call(a, b);") == ChangedSpan{6, 6});
}

TEST_CASE("A wholly different line is removed entirely")
{
	CHECK(RemovedSpan("cat", "dog") == ChangedSpan{0, 3});
}

TEST_CASE("An empty old line has nothing to remove")
{
	CHECK(RemovedSpan("", "anything") == ChangedSpan{0, 0});
}

TEST_CASE("A line emptied since is removed entirely")
{
	CHECK(RemovedSpan("gone", "") == ChangedSpan{0, 4});
}

TEST_CASE("The span never splits a multibyte character")
{
	const ChangedSpan span = RemovedSpan("a \xE2\x80\x94 b", "a \xE2\x80\x9C b");
	CHECK(span == ChangedSpan{2, 5});
}

TEST_CASE("Two separate edits cover the text between them")
{
	CHECK(RemovedSpan("one two three", "ONE two THREE") == ChangedSpan{0, 13});
}

TEST_CASE("A repeated character is not counted twice by prefix and suffix")
{
	CHECK(RemovedSpan("aaa", "aa") == ChangedSpan{2, 3});
}

TEST_CASE("An old line kept whole at the start of a longer one has nothing removed")
{
	CHECK(RemovedSpan("abc", "abcd") == ChangedSpan{3, 3});
}
