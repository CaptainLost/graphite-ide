#pragma once

#include "Core/Command/Chord.h"

#include <string>

namespace Core {

	struct BindingConflict
	{
		Chord SharedChord;
		std::string FirstCommandId;
		std::string SecondCommandId;
	};
}
