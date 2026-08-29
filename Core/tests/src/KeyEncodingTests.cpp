#include <doctest/doctest.h>

#include "Core/Term/KeyEncoding.h"

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
	CHECK(Core::EncodeKey(Core::KeyCode::B, Core::KeyModifier::Alt) == "\x1B" "b");
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
