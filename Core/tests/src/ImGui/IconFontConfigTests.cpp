#include <doctest/doctest.h>

#include <imgui.h>

TEST_CASE("ImWchar is 32-bit so MDI's private-use-plane codepoints above U+FFFF, such as language-cpp at U+F0672, are not truncated")
{
	CHECK(sizeof(ImWchar) == sizeof(ImWchar32));
}
