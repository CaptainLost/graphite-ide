#include <doctest/doctest.h>

#include <imgui.h>

TEST_CASE("ImWchar is 32-bit so MDI's private-use-plane codepoints fit")
{
    // MDI's icons live above U+FFFF (e.g. language-cpp is U+F0672), which the default
    // 16-bit ImWchar cannot represent. IMGUI_USE_WCHAR32 has to stay defined for every
    // icon to render instead of silently truncating to garbage.
    CHECK(sizeof(ImWchar) == sizeof(ImWchar32));
}
