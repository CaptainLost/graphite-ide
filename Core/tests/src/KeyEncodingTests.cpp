#include <doctest/doctest.h>

#include "Core/Term/KeyEncoding.h"

#include <string>

TEST_CASE("EncodeKey sends the control characters for the editing keys")
{
	CHECK(Core::EncodeKey(Core::KeyCode::Enter, Core::KeyModifier::None) == "\r");
	CHECK(Core::EncodeKey(Core::KeyCode::Backspace, Core::KeyModifier::None) == "\x7F");
	CHECK(Core::EncodeKey(Core::KeyCode::Tab, Core::KeyModifier::None) == "\t");
	CHECK(Core::EncodeKey(Core::KeyCode::Escape, Core::KeyModifier::None) == "\x1B");
}

TEST_CASE("EncodeKey sends escape sequences for the arrow keys")
{
	CHECK(Core::EncodeKey(Core::KeyCode::Up, Core::KeyModifier::None) == "\x1B[A");
	CHECK(Core::EncodeKey(Core::KeyCode::Down, Core::KeyModifier::None) == "\x1B[B");
	CHECK(Core::EncodeKey(Core::KeyCode::Right, Core::KeyModifier::None) == "\x1B[C");
	CHECK(Core::EncodeKey(Core::KeyCode::Left, Core::KeyModifier::None) == "\x1B[D");
}

TEST_CASE("EncodeKey sends the navigation keys")
{
	CHECK(Core::EncodeKey(Core::KeyCode::Home, Core::KeyModifier::None) == "\x1B[H");
	CHECK(Core::EncodeKey(Core::KeyCode::End, Core::KeyModifier::None) == "\x1B[F");
	CHECK(Core::EncodeKey(Core::KeyCode::Delete, Core::KeyModifier::None) == "\x1B[3~");
	CHECK(Core::EncodeKey(Core::KeyCode::PageUp, Core::KeyModifier::None) == "\x1B[5~");
	CHECK(Core::EncodeKey(Core::KeyCode::PageDown, Core::KeyModifier::None) == "\x1B[6~");
}

TEST_CASE("EncodeKey turns a control letter into its control character")
{
	CHECK(Core::EncodeKey(Core::KeyCode::C, Core::KeyModifier::Control) == std::string(1, '\x03'));
	CHECK(Core::EncodeKey(Core::KeyCode::A, Core::KeyModifier::Control) == std::string(1, '\x01'));
	CHECK(Core::EncodeKey(Core::KeyCode::Z, Core::KeyModifier::Control) == std::string(1, '\x1A'));
}

TEST_CASE("EncodeKey prefixes an alt combination with escape")
{
	CHECK(Core::EncodeKey(Core::KeyCode::B, Core::KeyModifier::Alt) == "\x1B"
																	   "b");
}

TEST_CASE("EncodeKey marks a modified arrow key")
{
	CHECK(Core::EncodeKey(Core::KeyCode::Right, Core::KeyModifier::Control) == "\x1B[1;5C");
	CHECK(Core::EncodeKey(Core::KeyCode::Left, Core::KeyModifier::Shift) == "\x1B[1;2D");
}

TEST_CASE("EncodeKey sends nothing for a plain letter")
{
	// Printable input arrives through the character queue, which already applies the
	// keyboard layout. Encoding it here as well would double every keystroke.
	CHECK(Core::EncodeKey(Core::KeyCode::A, Core::KeyModifier::None).empty());
	CHECK(Core::EncodeKey(Core::KeyCode::A, Core::KeyModifier::Shift).empty());
}

TEST_CASE("EncodeKey sends nothing for a modifier held alone")
{
	CHECK(Core::EncodeKey(Core::KeyCode::LeftControl, Core::KeyModifier::Control).empty());
}

TEST_CASE("EncodeKey sends the function keys")
{
	// F1-F4 are SS3 sequences and the rest are tilde sequences, which is what xterm sends and
	// what every terminfo entry a TUI reads is written against.
	CHECK(Core::EncodeKey(Core::KeyCode::F1, Core::KeyModifier::None) == "\x1BOP");
	CHECK(Core::EncodeKey(Core::KeyCode::F2, Core::KeyModifier::None) == "\x1BOQ");
	CHECK(Core::EncodeKey(Core::KeyCode::F3, Core::KeyModifier::None) == "\x1BOR");
	CHECK(Core::EncodeKey(Core::KeyCode::F4, Core::KeyModifier::None) == "\x1BOS");
	CHECK(Core::EncodeKey(Core::KeyCode::F5, Core::KeyModifier::None) == "\x1B[15~");
	CHECK(Core::EncodeKey(Core::KeyCode::F6, Core::KeyModifier::None) == "\x1B[17~");
	CHECK(Core::EncodeKey(Core::KeyCode::F7, Core::KeyModifier::None) == "\x1B[18~");
	CHECK(Core::EncodeKey(Core::KeyCode::F8, Core::KeyModifier::None) == "\x1B[19~");
	CHECK(Core::EncodeKey(Core::KeyCode::F9, Core::KeyModifier::None) == "\x1B[20~");
	CHECK(Core::EncodeKey(Core::KeyCode::F10, Core::KeyModifier::None) == "\x1B[21~");
	CHECK(Core::EncodeKey(Core::KeyCode::F11, Core::KeyModifier::None) == "\x1B[23~");
	CHECK(Core::EncodeKey(Core::KeyCode::F12, Core::KeyModifier::None) == "\x1B[24~");
}

TEST_CASE("EncodeKey parameterises a modified function key")
{
	// A modified F1-F4 leaves SS3 for the CSI form, which is the one that carries a parameter.
	CHECK(Core::EncodeKey(Core::KeyCode::F1, Core::KeyModifier::Shift) == "\x1B[1;2P");
	CHECK(Core::EncodeKey(Core::KeyCode::F5, Core::KeyModifier::Control) == "\x1B[15;5~");
}

TEST_CASE("EncodeKey sends insert")
{
	CHECK(Core::EncodeKey(Core::KeyCode::Insert, Core::KeyModifier::None) == "\x1B[2~");
}

TEST_CASE("EncodeKey sends a back-tab for shifted tab")
{
	// Shift+Tab is CBT, how a TUI walks its focus backwards; a plain tab would walk forwards.
	CHECK(Core::EncodeKey(Core::KeyCode::Tab, Core::KeyModifier::Shift) == "\x1B[Z");
	CHECK(Core::EncodeKey(Core::KeyCode::Tab, Core::KeyModifier::None) == "\t");
}

TEST_CASE("EncodeMouseWheel reports a notch at a one-based cell in SGR")
{
	CHECK(Core::EncodeMouseWheel(true, 0, 0, Core::KeyModifier::None, true) == "\x1B[<64;1;1M");
	CHECK(Core::EncodeMouseWheel(false, 9, 4, Core::KeyModifier::None, true) == "\x1B[<65;10;5M");
}

TEST_CASE("EncodeMouseWheel adds the modifier bits xterm defines")
{
	CHECK(Core::EncodeMouseWheel(true, 0, 0, Core::KeyModifier::Shift, true) == "\x1B[<68;1;1M");
	CHECK(Core::EncodeMouseWheel(true, 0, 0, Core::KeyModifier::Alt, true) == "\x1B[<72;1;1M");
	CHECK(Core::EncodeMouseWheel(true, 0, 0, Core::KeyModifier::Control, true) == "\x1B[<80;1;1M");
}

TEST_CASE("EncodeMouseWheel falls back to the legacy byte encoding without SGR")
{
	CHECK(Core::EncodeMouseWheel(true, 0, 0, Core::KeyModifier::None, false) == "\x1B[M`!!");
	CHECK(Core::EncodeMouseWheel(false, 2, 1, Core::KeyModifier::None, false) == "\x1B[Ma#\"");

	// One byte per coordinate cannot name a cell past 223; the last one it can is used.
	CHECK(Core::EncodeMouseWheel(true, 500, 0, Core::KeyModifier::None, false) ==
		  std::string("\x1B[M`") + static_cast<char>(255) + "!");
}
