#pragma once

#include "Core/Command/Chord.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	struct Binding
	{
		Chord Primary;
		std::vector<Chord> Alternates;
	};

	std::string ToString(const Binding& binding);

	std::optional<Binding> ParseBinding(std::string_view text);
}
