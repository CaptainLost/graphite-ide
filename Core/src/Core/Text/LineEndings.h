#pragma once

#include "Core/Jobs/CancellationToken.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// How a text's lines end, apart from the document that holds it, so what only reads files (the WSL
// agent's search) can read them as an opened document does.
namespace Core {

	enum class LineEnding
	{
		Lf,
		Crlf
	};

	std::string WithLineEnding(std::string_view text, LineEnding ending);

	struct NormalisedLineEndings
	{
		std::string Content;
		LineEnding Ending = LineEnding::Lf;
		bool Mixed = false;
		std::vector<std::size_t> ContentLineStarts{0};
	};

	NormalisedLineEndings NormaliseLineEndings(std::string_view text);

	// WARNING: Rewritten in place, which only ever shortens the text, so a load holds one copy of the file, not two.
	// False, with text left part-way, when cancellation asks to stop.
	bool NormaliseLineEndingsInPlaceUnlessCancelled(std::string& text, NormalisedLineEndings& normalised, const CancellationToken& cancellation);
}
