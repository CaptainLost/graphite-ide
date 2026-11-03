#pragma once

#include "Git/Lines/LineStore.h"

#include "Core/Base/Result.h"
#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Lines/LineAnswers.h"
#include "Core/Git/Lines/LineModel.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Repository.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Graphite {

	struct LineRead
	{
		std::vector<Core::LineRef> Refs;
		Core::AncestryGraph Graph; // the graph the model was computed on
		Core::LineModel Model;

		// The answers as this read leaves them: what the file holds with this read's own changes merged in, or, when
		// the file is from a newer build or cannot be read and is left alone, the answers the model used with them.
		Core::LineAnswers Answers;

		std::size_t CommitsRead = 0; // read from git this time; 0 when the cache already had every tip
	};

	// Synchronous. Loads the cached graph, reads only what it lacks, computes the model, records newly found bases in
	// KeptBases, drops kept bases and comparisons that name refs no longer there, and saves the graph and the answers
	// when they changed. A ref whose tip git cannot read as a commit is not read and is TipMissing in the model.
	Core::Result<LineRead> ReadLines(const Core::Repository& repository, const LineStorePaths& store, std::int64_t now);

	// The answers file after a read: re-reads it and applies only what the read changed itself, from `loaded` (what it
	// read at its start) to `computed`: the names it added to and dropped from KeptBases and the Comparisons entries it
	// dropped. An answer the user gave while the read ran survives. A file from a newer build, or one that exists and
	// cannot be read, is not written. Returns the answers as they then stand: the file's, or `computed` when the file
	// is left alone.
	Core::LineAnswers SaveAnswerChanges(const std::filesystem::path& file, const Core::LineAnswers& loaded, const Core::LineAnswers& computed);

	// The LineRefs for QueryBranches' answer, each remote branch given to the longest remote name that prefixes it
	// ("team/fork/main" -> remote "team/fork", short "main"). A local upstream is a remote branch of that name, else a
	// local branch of that name, else a remote branch that is gone.
	std::vector<Core::LineRef> LineRefsOf(const std::vector<Core::Branch>& branches, const std::vector<std::string>& remotes);
}
