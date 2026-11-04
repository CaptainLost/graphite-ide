#pragma once

#include "Git/Lines/LineReader.h"
#include "Git/Lines/LineStore.h"

#include "Core/Base/Result.h"
#include "Core/Git/Lines/LineAnswers.h"

#include <cstdint>
#include <string>

namespace Graphite {

	// One answer the user gives about the lines.
	struct LineAnswerChange
	{
		enum class Kind
		{
			UseAsBase,
			StopUsingAsBase,
			UseAsMainLine,
			CompareWith,
			CompareWithNothing,
			CompareAutomatically
		};

		Kind What = Kind::UseAsBase;
		std::string Ref;    // full name of the branch the change is about
		std::string Target; // CompareWith only
	};

	void ApplyLineChange(Core::LineAnswers& answers, const LineAnswerChange& change);

	// Synchronous. Applies the change to what the answers file holds now and saves it, then computes the model again
	// on the last read's graph and refs, without reading git. A file from a newer build, or one that exists and cannot
	// be read, is left alone and the change fails.
	Core::Result<LineRead> RecomputeLines(const LineRead& last, const LineStorePaths& store, const LineAnswerChange& change, std::int64_t now);
}
