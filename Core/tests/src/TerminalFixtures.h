#pragma once

#include "Core/Terminal/TerminalScreen.h"

#include <cstdint>
#include <string>

inline std::u32string ScreenRowText(const Core::TerminalScreen& screen, std::uint16_t row)
{
	std::u32string text;

	for (std::uint16_t column = 0; column < screen.GetColumns(); ++column)
	{
		text.push_back(screen.At(row, column).Code);
	}

	return text;
}
