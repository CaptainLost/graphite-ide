#pragma once

#include "Git/Lines/LineStore.h"

#include "Core/Base/Result.h"
#include "Core/Git/Lines/LineAnswers.h"
#include "Core/Git/Lines/LineModel.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Repository.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Graphite {

	struct LineRead
	{
		std::vector<Core::LineRef> Refs;
		Core::LineModel Model;
		Core::LineAnswers Answers;   // as saved after this read
		std::size_t CommitsRead = 0; // read from git this time; 0 when the cache already had every tip
	};

	// Synchronous. Loads the cached graph, reads only what it lacks, computes the model, records newly found bases in
	// KeptBases, and saves the graph and the answers when they changed. An answers file written by a newer build is
	// read as empty and never overwritten.
	Core::Result<LineRead> ReadLines(const Core::Repository& repository, const LineStorePaths& store, std::int64_t now);

	// The LineRefs for QueryBranches' answer, each remote branch given to the longest remote name that prefixes it
	// ("team/fork/main" -> remote "team/fork", short "main").
	std::vector<Core::LineRef> LineRefsOf(const std::vector<Core::Branch>& branches, const std::vector<std::string>& remotes);
}
