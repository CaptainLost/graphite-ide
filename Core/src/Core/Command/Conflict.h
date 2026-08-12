#pragma once

#include "Core/Command/Chord.h"

#include <string>

namespace Core {

	struct Conflict
	{
		Chord chord;
		std::string first;
		std::string second;
	};
}