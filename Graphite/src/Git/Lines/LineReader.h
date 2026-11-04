#pragma once

#include "Git/Lines/LineStore.h"

#include "Core/Base/Base.h"

#include "Core/Base/Result.h"
#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Lines/LineAnswers.h"
#include "Core/Git/Lines/LineModel.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Remote.h"
#include "Core/Git/Repository.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Graphite {

	struct LineRead
	{
		Core::Ref<const std::vector<Core::Branch>> Branches; // the snapshot the model describes; Refs[i] is (*Branches)[i]
		std::vector<Core::Remote> Remotes;
		std::vector<Core::LineRef> Refs;
		Core::Ref<const Core::AncestryGraph> Graph; // the graph the model was computed on

		// Core::GenerationsOf(*Graph), computed once for each graph and handed on with it.
		Core::Ref<const std::vector<std::uint32_t>> Generations;

		Core::LineModel Model;

		// The answers as this read leaves them: what the file holds with this read's own changes merged in, or, when
		// the file is from a newer build or cannot be read and is left alone, the answers the model used with them.
		Core::LineAnswers Answers;

		std::size_t CommitsRead = 0; // read from git this time; 0 when the cache already had every tip
	};

	struct LineReadInput
	{
		Core::Ref<const std::vector<Core::Branch>> Branches;
		Core::Ref<const Core::AncestryGraph> Graph; // the previous read's, or null to load graph.bin
		std::int64_t Now = 0;

		// The previous read's, for Graph: handed on while nothing is appended, computed again when null.
		Core::Ref<const std::vector<std::uint32_t>> Generations;
	};

	// Synchronous. Starts from the graph it is given, or the cached one, reads only what it lacks, computes the model,
	// records newly found bases in KeptBases and at the end of BaseOrder (never one the user removed), drops kept bases
	// and comparisons that name refs no longer there and the BaseOrder names no longer added or kept, and saves the
	// graph and the answers when they changed. A ref whose tip git cannot read as a commit is not read and is
	// TipMissing in the model. The graph given is never changed: a read that appends to it appends to a copy, and one
	// that appends nothing hands back the same pointer.
	Core::Result<LineRead> ReadLines(const Core::Repository& repository, const LineStorePaths& store, const LineReadInput& input);

	// The same read of the branches the repository lists now, starting from the cached graph.
	Core::Result<LineRead> ReadLines(const Core::Repository& repository, const LineStorePaths& store, std::int64_t now);

	// The answers file after a read: re-reads it and applies only what the read changed itself, from `loaded` (what it
	// read at its start) to `computed`: the names it added to and dropped from KeptBases and BaseOrder and the
	// Comparisons entries it dropped. An answer the user gave while the read ran survives: a base removed meanwhile is
	// not kept again, and the read's new BaseOrder names follow those added meanwhile. A file from a newer build, or
	// one that exists and cannot be read, is not written. Returns the answers as they then stand: the file's, or
	// `computed` when the file is left alone.
	Core::LineAnswers SaveAnswerChanges(const std::filesystem::path& file, const Core::LineAnswers& loaded, const Core::LineAnswers& computed);

	// The LineRefs for QueryBranches' answer, each remote branch given to the longest remote name that prefixes it
	// ("team/fork/main" -> remote "team/fork", short "main"). A local upstream is a remote branch of that name, else a
	// local branch of that name, else a remote branch that is gone.
	std::vector<Core::LineRef> LineRefsOf(const std::vector<Core::Branch>& branches, const std::vector<std::string>& remotes);
}
