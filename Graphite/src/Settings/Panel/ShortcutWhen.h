#pragma once

#include "Core/Command/Chord.h"
#include "Core/Command/ChordFlags.h"

#include <array>
#include <span>
#include <string>

namespace Graphite {

	// The places a Global command's chord can be told to wait in, in the order the When menu lists them.
	inline constexpr std::array kTypingTargets{Core::TypingTarget::Editor, Core::TypingTarget::Terminal, Core::TypingTarget::TextField};

	inline constexpr const char* kFiresEverywhere = "Everywhere";

	// What the When column says of a command's chords: "Everywhere", "Not while typing", "Not in the terminal", or,
	// when its chords differ, each one that waits somewhere: "F: not while typing". Empty for chords that cannot
	// wait anywhere, such as a wheel turn.
	std::string DescribeWhen(std::span<const Core::Chord> chords);

	// Why a chord nobody flagged waits: "F types a character, so it waits while you type unless ticked here."
	// Empty when no chord waits by default.
	std::string DescribeDefaultWait(std::span<const Core::Chord> chords);

	const char* TypingTargetLabel(Core::TypingTarget target);

	const char* TypingTargetHeading(Core::TypingTarget target);
}
