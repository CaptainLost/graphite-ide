#pragma once

#include "Core/Text/TextSelection.h"

inline Core::TextSelection SelectionBetween(Core::TextPoint from, Core::TextPoint to)
{
	Core::TextSelection selection;
	selection.Select(from, to);
	return selection;
}
