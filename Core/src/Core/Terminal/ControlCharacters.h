#pragma once

#include <string_view>

namespace Core {

	inline constexpr char kEscapeCharacter = '\x1B';

	inline constexpr char kBellCharacter = '\x07';

	// What the Backspace key sends. The Delete key sends CSI 3 ~ instead.
	inline constexpr char kDeleteCharacter = '\x7F';

	// The 7-bit form, ESC [. The 8-bit C1 form (0x9B) is not used.
	inline constexpr std::string_view kControlSequenceIntroducer = "\x1B[";
}
