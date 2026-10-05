#include <doctest/doctest.h>

#include "Core/Terminal/ControlCharacters.h"

#include <ostream>
#include <string>
#include <string_view>

TEST_CASE("The control characters are the bytes the VT protocol defines")
{
	static_assert(Core::kEscapeCharacter == '\x1B');
	static_assert(Core::kBellCharacter == '\x07');
	static_assert(Core::kDeleteCharacter == '\x7F');
	CHECK(Core::kControlSequenceIntroducer == std::string_view{"\x1B[", 2});
	CHECK(std::string(1, Core::kEscapeCharacter).size() == 1);
}
