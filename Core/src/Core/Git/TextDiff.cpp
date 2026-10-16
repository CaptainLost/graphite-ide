#include "corepch.h"

#include "Core/Git/TextDiff.h"

#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <format>

namespace Core {

	namespace {

		// git numbers lines from one, but for a run with no lines it names the line the run
		// follows, which already is the zero-based boundary LineHunk wants.
		std::size_t ZeroBasedStart(int gitStart, int lineCount)
		{
			const std::size_t start = static_cast<std::size_t>(gitStart);
			const bool isEmptyRun = lineCount == 0;
			return isEmptyRun ? start : start - 1;
		}

		int CollectHunk(const git_diff_delta*, const git_diff_hunk* diffHunk, void* payload)
		{
			std::vector<LineHunk>& hunks = *static_cast<std::vector<LineHunk>*>(payload);

			hunks.push_back(LineHunk{
				.OldStart = ZeroBasedStart(diffHunk->old_start, diffHunk->old_lines),
				.OldCount = static_cast<std::size_t>(diffHunk->old_lines),
				.NewStart = ZeroBasedStart(diffHunk->new_start, diffHunk->new_lines),
				.NewCount = static_cast<std::size_t>(diffHunk->new_lines)});

			return 0;
		}
	}

	Result<std::vector<LineHunk>> DiffTexts(std::string_view oldText, std::string_view newText)
	{
		EnsureLibgit2Initialised();

		git_diff_options options = GIT_DIFF_OPTIONS_INIT;
		options.context_lines = 0;
		options.interhunk_lines = 0;

		// Without this a stray NUL turns the answer into "binary", which silently has no hunks.
		options.flags |= GIT_DIFF_FORCE_TEXT;

		const char* oldData = DataOrEmptyString(oldText);
		const char* newData = DataOrEmptyString(newText);

		const char* const noFileName = nullptr;
		const git_diff_file_cb noFileCallback = nullptr;
		const git_diff_binary_cb noBinaryCallback = nullptr;
		const git_diff_line_cb noLineCallback = nullptr;

		std::vector<LineHunk> hunks;

		const int diffResult = git_diff_buffers(oldData, oldText.size(), noFileName,
												newData, newText.size(), noFileName,
												&options, noFileCallback, noBinaryCallback, &CollectHunk,
												noLineCallback, &hunks);

		if (diffResult != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot diff the two texts: {}", LastLibgit2Message())};
		}

		return hunks;
	}
}
