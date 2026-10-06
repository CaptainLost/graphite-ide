#include <doctest/doctest.h>

#include "Core/Command/Binding.h"

#include <optional>
#include <string>

using Core::Binding;
using Core::Chord;
using Core::ParseBinding;
using Core::ShortcutScope;
using Core::Trigger;

TEST_CASE("A binding of one chord is written as that chord alone")
{
	const Binding binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S)};

	CHECK(Core::ToString(binding) == "Ctrl+S");
}

TEST_CASE("Alternates follow the primary, separated from it")
{
	const Binding binding{.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal),
						  .Alternates = {Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd)}};

	CHECK(Core::ToString(binding) == "Ctrl+=|Ctrl+Num+");

	const std::optional<Binding> parsed = ParseBinding("Ctrl+=|Ctrl+Num+");

	REQUIRE(parsed.has_value());
	CHECK(parsed->Primary == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal));
	REQUIRE(parsed->Alternates.size() == 1);
	CHECK(parsed->Alternates[0] == Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd));
}

TEST_CASE("A binding that reaches nothing round-trips through its own token because an empty text is refused as malformed and would bring the factory chord back")
{
	const Binding unbound;

	CHECK(Core::ToString(unbound) == "None");

	const std::optional<Binding> parsed = ParseBinding("None");

	REQUIRE(parsed.has_value());
	CHECK(parsed->Primary.IsEmpty());
	CHECK(parsed->Alternates.empty());

	CHECK_FALSE(ParseBinding("").has_value());
}

TEST_CASE("The unbound token is read case- and whitespace-insensitively like every other name in the hand-edited settings file")
{
	for (const char* handWrittenToken : {"none", " None ", "NONE"})
	{
		const std::optional<Binding> parsed = ParseBinding(handWrittenToken);

		REQUIRE(parsed.has_value());
		CHECK(parsed->Primary.IsEmpty());
	}
}

TEST_CASE("A binding is refused whole when any part of it is unreadable rather than keeping only the chords this build recognises")
{
	CHECK_FALSE(ParseBinding("Ctrl+S|Ctrl+Nonsense").has_value());
	CHECK_FALSE(ParseBinding("Ctrl+Nonsense").has_value());
	CHECK_FALSE(ParseBinding("").has_value());
	CHECK_FALSE(ParseBinding("|").has_value());
}

TEST_CASE("Every kind of chord in a binding survives a round trip through text including the wheel gesture")
{
	const Binding binding{
		.Primary = Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Equal),
		.Alternates = {Chord::Key(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd),
					   Chord::Wheel(ImGuiMod_Ctrl, Trigger::WheelUp, ShortcutScope::HoveredEditor)}};

	const std::optional<Binding> parsed = ParseBinding(Core::ToString(binding));

	REQUIRE(parsed.has_value());
	CHECK(parsed->Primary == binding.Primary);
	REQUIRE(parsed->Alternates.size() == binding.Alternates.size());
	CHECK(parsed->Alternates[0] == binding.Alternates[0]);
	CHECK(parsed->Alternates[1] == binding.Alternates[1]);
}
