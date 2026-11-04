#pragma once

#include "Git/Lines/LineReader.h"
#include "Git/Window/WindowBar.h"

#include <optional>
#include <string>

namespace Graphite {

	struct MainLineAsk
	{
		std::string GuessRef; // full name of the guessed branch
		std::string GuessName; // short name, with its remote in front for a remote branch: "origin/develop"
	};

	// The question to ask while the read's main line is a guess and the user has not put it off; nothing otherwise.
	std::optional<MainLineAsk> MainLineToAsk(const LineRead* read, bool dismissed);

	enum class MainLineAnswer
	{
		None,
		UseGuess,
		NotNow
	};

	// One bar across the available width, as high as its text wraps to, ending on an item.
	MainLineAnswer DrawMainLineQuestion(const MainLineAsk& ask, const WindowBarColours& colours);
}
