#pragma once

#include "Git/Lines/LineAnswerChange.h"
#include "Git/Lines/LineReader.h"

#include "Core/Base/Result.h"

#include <cstdint>

namespace Graphite {

	// The lines of the open folder's repository, read and saved in its own line store.
	class LinesPort
	{
	public:
		virtual ~LinesPort() = default;

		virtual Core::Result<LineRead> ReadLines(const LineReadInput& input) = 0;
		virtual Core::Result<LineRead> RecomputeLines(const LineRead& last, const LineAnswerChange& change, std::int64_t now) = 0;
	};
}
