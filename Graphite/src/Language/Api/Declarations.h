#pragma once

#include "Language/Api/LanguageService.h"

#include <cstddef>
#include <span>
#include <vector>

namespace Graphite {

	std::vector<const Declaration*> InnermostDeclarationAt(std::span<const Declaration> declarations,
														   std::size_t line);

	// WARNING: A line the edit deleted lands on the line before the deletion.
	std::size_t LineAfterEdit(std::size_t line, std::size_t fromLine, std::ptrdiff_t delta);

	// WARNING: A declaration whose first line the edit deleted is dropped, as its lens is, and the
	// declarations it held take its own parent. A range whose last line was deleted ends on the line
	// before the deletion.
	void MoveDeclarations(std::vector<Declaration>& declarations, std::size_t fromLine, std::ptrdiff_t delta);
}
