#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Base/Log.h"
#include "Core/Git/CommitFilter.h"
#include "Core/Git/LineRangeTracking.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <format>
#include <string>
#include <string_view>

namespace Core {

	namespace {

		// Returns the offset immediately after the line terminator that starts at `pos` —
		// "\r\n" or a bare "\n" — or npos if `pos` is not the start of one. Git does not
		// normalise line endings inside a commit message, so a message written with CRLF
		// keeps it.
		std::size_t SkipTerminator(std::string_view text, std::size_t pos)
		{
			if (pos < text.size() && text[pos] == '\r' && pos + 1 < text.size() &&
				text[pos + 1] == '\n')
			{
				return pos + 2;
			}

			if (pos < text.size() && text[pos] == '\n')
			{
				return pos + 1;
			}

			return std::string_view::npos;
		}

		Signature ToSignature(const git_signature* who)
		{
			if (who == nullptr)
			{
				return Signature{};
			}

			return Signature{who->name != nullptr ? who->name : "",
							 who->email != nullptr ? who->email : "",
							 std::chrono::sys_seconds{std::chrono::seconds{who->when.time}}};
		}

		CommitSummary ToSummary(git_commit* commit)
		{
			CommitSummary summary;
			summary.Id = ToOid(*git_commit_id(commit));

			const unsigned int parents = git_commit_parentcount(commit);
			summary.Parents.reserve(parents);

			for (unsigned int index = 0; index < parents; ++index)
			{
				summary.Parents.push_back(ToOid(*git_commit_parent_id(commit, index)));
			}

			summary.Author = ToSignature(git_commit_author(commit));
			summary.Committer = ToSignature(git_commit_committer(commit));

			if (const char* line = git_commit_summary(commit); line != nullptr)
			{
				summary.Summary = line;
			}

			// git_commit_body() trims the surrounding whitespace, which loses the trailing
			// newline a message was written with. The body is read out of the raw message
			// instead, so a caller that writes the commit back out (or diffs it against
			// what was typed) sees exactly what git recorded.
			if (const char* raw = git_commit_message(commit); raw != nullptr)
			{
				const std::string_view text(raw);
				const std::size_t titleEnd = text.find_first_of("\r\n");

				if (titleEnd != std::string_view::npos)
				{
					std::size_t bodyStart = SkipTerminator(text, titleEnd);

					if (bodyStart != std::string_view::npos)
					{
						// Exactly one blank line separates the title from the body, per
						// git convention — only that single separator is consumed here.
						// A second blank line is the body's own leading content and must
						// survive, matching what `git log --format=%b` shows.
						if (const std::size_t afterBlank = SkipTerminator(text, bodyStart);
							afterBlank != std::string_view::npos)
						{
							bodyStart = afterBlank;
						}

						if (bodyStart < text.size())
						{
							summary.Body = text.substr(bodyStart);
						}
					}
				}
			}

			return summary;
		}

		// What one commit's diff has to say about a pattern: the first file the pattern
		// names, and whether it named more than one of them. Both sides of each file are
		// checked because a rename names the file differently on each: the commit that
		// renamed old.txt to new.txt is part of new.txt's history.
		struct PathMatch
		{
			const FileDiff* File = nullptr;

			// Set when the pattern named more than one file in this commit. Only the rename
			// decision below reads it — for keeping the commit, one match is as good as six.
			bool Several = false;
		};

		PathMatch FindPath(const Diff& diff, const std::filesystem::path& pattern)
		{
			const std::string text = pattern.generic_string();

			PathMatch match;

			for (const FileDiff& file : diff.Files)
			{
				if (!NamesPath(text, file.NewPath) && !NamesPath(text, file.OldPath))
				{
					continue;
				}

				if (match.File == nullptr)
				{
					match.File = &file;
					continue;
				}

				match.Several = true;
				break;
			}

			return match;
		}

		bool HasWildcard(const std::filesystem::path& pattern)
		{
			const std::string text = pattern.generic_string();
			return text.find('*') != std::string::npos || text.find('?') != std::string::npos;
		}
	}

	Result<LogPage> Repository::QueryLog(const LogRequest& request) const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		git_revwalk* walk = nullptr;

		if (git_revwalk_new(&walk, m_Handle) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot walk the history: {}", LastLibgit2Message())};
		}

		// Topological first so a branch stays contiguous, date second so two unrelated
		// branches interleave the way a reader expects.
		git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);

		const int pushed = request.Ref.has_value()
							   ? git_revwalk_push_ref(walk, request.Ref->c_str())
							   : git_revwalk_push_head(walk);

		if (pushed != 0)
		{
			// An unborn HEAD lands here, and so does a ref that was deleted between the
			// panel listing it and this call. Neither is worth a failure the user has to
			// read; both mean "there is nothing to show".
			const std::string message = LastLibgit2Message();
			git_revwalk_free(walk);

			if (request.Ref.has_value())
			{
				return Error{ErrorCode::NotFound,
							 std::format("no such ref '{}': {}", *request.Ref, message)};
			}

			// Nothing was walked, so there is nothing this answer could be missing.
			return LogPage{{}, true};
		}

		std::vector<CommitSummary> commits;
		git_oid id{};

		std::optional<std::filesystem::path> tracked = request.File;
		std::optional<LineRange> trackedRange = request.Lines;

		// Bounds the work of the walk itself, independent of request.MaxCount below, which
		// bounds only what is kept. File and line-range narrowing diff every commit they
		// pass through, so a rarely-touched file in a huge history would otherwise walk and
		// diff the whole thing looking for a handful of matches.
		std::size_t walked = 0;

		// Set to false on every path that stops the walk before it runs out of history on
		// its own, so the caller can tell a short page from a finished one.
		bool complete = true;

		while (true)
		{
			if (commits.size() >= request.MaxCount)
			{
				// The caller asked for this many and no more, but that says nothing about
				// whether history continues past them - the walk simply stopped looking,
				// the same as it does at the MaxWalk ceiling below. A history exactly
				// MaxCount long and one that keeps going past it look identical here, and
				// only Complete tells a caller which one it got, letting it offer "load
				// more" rather than presenting a full-length page as the end of the story.
				complete = false;
				break;
			}

			if (walked >= request.MaxWalk)
			{
				// Reaching the ceiling does not mean history continues past it - only that
				// the walk stopped checking. Either way, the page in hand is not the whole
				// answer.
				complete = false;
				break;
			}

			if (git_revwalk_next(&id, walk) != 0)
			{
				// The walk ran out of commits on its own: this page is the whole history.
				break;
			}

			++walked;

			git_commit* commit = nullptr;

			if (git_commit_lookup(&commit, m_Handle, &id) != 0)
			{
				CORE_TRACE("Skipping an unreadable commit while walking the history");
				continue;
			}

			CommitSummary summary = ToSummary(commit);
			git_commit_free(commit);

			if (!tracked.has_value())
			{
				if (Matches(request.Filter, summary))
				{
					commits.push_back(std::move(summary));
				}

				continue;
			}

			DiffRequest diffRequest;
			diffRequest.Target = DiffTarget::CommitToParent;
			diffRequest.From = summary.Id;

			auto diff = QueryDiff(diffRequest);

			if (!diff.HasValue())
			{
				// The diff failed, so nothing about this commit can be trusted: not whether
				// it touched the tracked file, not how it would have moved the tracked
				// range. Carrying on would risk mapping the range across a change that was
				// never actually read, which is worse than stopping with what has already
				// been found — the walk cannot tell a genuine end of history from a commit
				// it simply could not read.
				CORE_TRACE("Stopping the history walk: cannot diff commit {}: {}",
						   summary.Id.ToHex(), diff.GetError().Message);
				complete = false;
				break;
			}

			const PathMatch match = FindPath(diff.GetValue(), *tracked);
			const FileDiff* file = match.File;

			if (file == nullptr)
			{
				continue;
			}

			// A rename moves the name older commits know this file by. The tracked path is
			// updated before the keep/drop decision below, not after it: a commit that
			// renames the file and also edits lines outside the tracked range touches no
			// hunk in range, so the old code's early continue below would skip this block
			// entirely and leave `tracked` holding a name only this commit and its
			// descendants ever used, silently ending the walk one commit early.
			// Only for a pattern that names one file and says so exactly. A pattern with a
			// wildcard names a set, and a set has no one identity for a rename to carry:
			// swapping "*.txt" for the renamed file's old path would drop every other .txt
			// from a question that plainly included them. The same holds when the pattern
			// named several files in this commit, wildcard or not.
			if (request.FollowRenames && file->Change == FileChange::Renamed
				&& !file->OldPath.empty() && !match.Several && !HasWildcard(*tracked))
			{
				tracked = file->OldPath;
			}

			std::optional<LineRange> nextRange = trackedRange;

			if (trackedRange.has_value())
			{
				if (!TouchesRange(*file, *trackedRange))
				{
					// The commit changed the file but not these lines. Its edits still move
					// them, so the range is mapped even though the commit is dropped.
					nextRange = MapRangeToParent(*file, *trackedRange);

					if (!nextRange.has_value())
					{
						complete = false;
						break;
					}

					trackedRange = nextRange;
					continue;
				}

				nextRange = MapRangeToParent(*file, *trackedRange);
			}

			// Asked here rather than up where the summary was read, so a commit the filter
			// drops is still diffed on the way past: that diff is what follows the file
			// through a rename and maps the tracked range back to the parent, and skipping
			// it would end the walk at the first unmatched commit that renamed the file.
			if (Matches(request.Filter, summary))
			{
				commits.push_back(std::move(summary));
			}

			if (trackedRange.has_value())
			{
				if (!nextRange.has_value())
				{
					complete = false;
					break;
				}

				trackedRange = nextRange;
			}
		}

		git_revwalk_free(walk);
		return LogPage{std::move(commits), complete};
	}
}
