#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Lines/LineAnswers.h"

#include <filesystem>

namespace Graphite {

	struct LineStorePaths
	{
		std::filesystem::path Answers; // <folder>/answers.json
		std::filesystem::path Graph;   // <folder>/graph.bin
	};

	// <root>/<16 lowercase hex digits>: FNV-1a 64 of the UTF-8 text of the common directory made absolute, normalised
	// (lexically_normal, no trailing separator) and lowercased, so every spelling of one directory gives one folder (lowercasing is ASCII-only).
	LineStorePaths LineStoreFor(const std::filesystem::path& gitCommonDirectory, const std::filesystem::path& root);

	// A missing or malformed file, or one written by a newer version, gives empty answers (a malformed one is logged).
	Core::LineAnswers LoadLineAnswers(const std::filesystem::path& file);

	// True only for a file that exists and parses as a JSON object whose "version" is present and not 1; a missing or
	// malformed file, or a version-1 one, is false. A caller that saves unprompted must not do so over such a file.
	bool HasForeignVersion(const std::filesystem::path& file);

	Core::Result<void> SaveLineAnswers(const std::filesystem::path& file, const Core::LineAnswers& answers);
}
