#include <doctest/doctest.h>

#include "Settings/Panel/ShortcutWhen.h"

#include "Core/Command/Chord.h"
#include "Core/Command/Trigger.h"

#include <ostream>
#include <string>
#include <vector>

using Core::Chord;

namespace {
	std::string When(std::vector<Chord> chords)
	{
		return Graphite::DescribeWhen(chords);
	}
}

TEST_CASE("A shortcut with Ctrl fires everywhere, and one that types a letter fires only when nothing is typed into")
{
	CHECK(When({Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_E)}) == "Everywhere");
	CHECK(When({Chord::Key(ImGuiKey_F)}) == "Not while typing");
}

TEST_CASE("A shortcut that waits in some places names them")
{
	CHECK(When({Chord::Key(ImGuiMod_Ctrl | ImGuiKey_Z)}) == "Not in text fields");
	CHECK(When({Chord::Key(ImGuiKey_F).WithFlags({.FiresInTerminal = true})}) == "Not in the editor or text fields");
	CHECK(When({Chord::Key(ImGuiMod_Ctrl | ImGuiKey_R).WithFlags({.FiresInTerminal = false})}) == "Not in the terminal");
}

TEST_CASE("Shortcuts of one command that differ are told apart by the ones that wait")
{
	CHECK(When({Chord::Key(ImGuiKey_F), Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_E)}) == "F: not while typing");
	CHECK(When({Chord::Key(ImGuiKey_F), Chord::Key(ImGuiKey_G)}) == "Not while typing");
}

TEST_CASE("A wheel turn says nothing about typing, and neither does a command with no shortcut")
{
	CHECK(When({Chord::Wheel(ImGuiMod_Ctrl, Core::Trigger::WheelUp, Core::ShortcutScope::HoveredEditor)}).empty());
	CHECK(When({}).empty());
}

TEST_CASE("The menu says why a letter nobody ticked waits, and stops once it has been ticked")
{
	const std::vector<Chord> letter{Chord::Key(ImGuiKey_F), Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_E)};
	CHECK(Graphite::DescribeDefaultWait(letter) == "F types a character, so it waits while you type unless ticked here.");

	const std::vector<Chord> letters{Chord::Key(ImGuiKey_F), Chord::Key(ImGuiMod_Shift | ImGuiKey_G)};
	CHECK(Graphite::DescribeDefaultWait(letters) == "F, Shift+G type characters, so they wait while you type unless ticked here.");

	const std::vector<Chord> ticked{Chord::Key(ImGuiKey_F).WithFlags({.FiresInEditor = true})};
	CHECK(Graphite::DescribeDefaultWait(ticked).empty());
}
