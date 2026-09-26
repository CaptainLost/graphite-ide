#include "corepch.h"

#include "Core/Git/TextDiff.h"

#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <format>

namespace Core {

	namespace {

		// git numbers lines from one, and names the line a zero-length run follows rather than
		// a line it contains. That is exactly the zero-based boundary LineHunk wants.
		std::size_t ZeroBasedStart(int start, int count)
		{
			const auto from = static_cast<std::size_t>(start);
			return count == 0 ? from : from - 1;
		}

		int CollectHunk(const git_diff_delta*, const git_diff_hunk* hunk, void* payload)
		{
			auto& hunks = *static_cast<std::vector<LineHunk>*>(payload);

			hunks.push_back(LineHunk{
				ZeroBasedStart(hunk->old_start, hunk->old_lines),
				static_cast<std::size_t>(hunk->old_lines),
				ZeroBasedStart(hunk->new_start, hunk->new_lines),
				static_cast<std::size_t>(hunk->new_lines)});

			return 0;
		}
	}

	Result<std::vector<LineHunk>> DiffTexts(std::string_view oldText, std::string_view newText)
	{
		EnsureLibgit2Initialised();

		git_diff_options options = GIT_DIFF_OPTIONS_INIT;
		options.context_lines = 0;
		options.interhunk_lines = 0;

		// The caller has already decided this is text. Without this, a stray NUL would turn the
		// answer into "binary" and silently no hunks at all.
		options.flags |= GIT_DIFF_FORCE_TEXT;

		// An empty view may carry a null pointer. libgit2 reads a null buffer as "no file",
		// which is not what an empty text means.
		const char* oldData = oldText.empty() ? "" : oldText.data();
		const char* newData = newText.empty() ? "" : newText.data();

		std::vector<LineHunk> hunks;

		const int result = git_diff_buffers(oldData, oldText.size(), nullptr,
											newData, newText.size(), nullptr,
											&options, nullptr, nullptr, &CollectHunk, nullptr, &hunks);

		if (result != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot diff the two texts: {}", LastLibgit2Message())};
		}

		return hunks;
	}
}
