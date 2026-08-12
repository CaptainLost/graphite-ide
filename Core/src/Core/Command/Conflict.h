#pragma once

#include "Core/Command/Chord.h"

#include <string>

namespace Core {

	struct Conflict
	{
		Chord Chord;
		std::string First;
		std::string Second;
	};
}