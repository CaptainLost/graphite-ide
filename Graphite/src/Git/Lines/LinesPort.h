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

		// Which repository the port opens now, as a number that changes each time the port is pointed at a folder. Asked
		// on the UI thread when a read is asked, where the port opens the repository the branch snapshot was listed from.
		virtual std::uint64_t RepositoryNow() = 0;

		// WARNING: Reads and saves nothing, and fails, once the port no longer opens `repository`: a read computes its
		// snapshot's refs against the answers of the store it opens, and must never save them into another repository's.
		virtual Core::Result<LineRead> ReadLines(std::uint64_t repository, const LineReadInput& input) = 0;

		virtual Core::Result<LineRead> RecomputeLines(const LineRead& last, const LineAnswerChange& change, std::int64_t now) = 0;
	};
}
