#include <doctest/doctest.h>

#include <imgui.h>

TEST_CASE("ImWchar is 32-bit so a codepoint above U+FFFF, such as an emoji in a file name or a conversation, is not truncated")
{
	CHECK(sizeof(ImWchar) == sizeof(ImWchar32));
}
