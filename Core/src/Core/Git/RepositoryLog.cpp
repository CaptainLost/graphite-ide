#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/CommitFilter.h"
#include "Core/Git/LineRangeTracking.h"
#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace Core {

	namespace {

		constexpr std::string_view kCarriageReturnLineFeed = "\r\n";
		constexpr std::string_view kLineFeed = "\n";
		constexpr std::size_t kNotALineTerminator = std::string_view::npos;

		constexpr std::size_t kStashParentsShown = 1;

		// Git does not normalise line endings inside a commit message, so a message written with
		// CRLF keeps it.
		std::size_t OffsetAfterLineTerminator(std::string_view text, std::size_t terminatorStart)
		{
			if (terminatorStart >= text.size())
			{
				return kNotALineTerminator;
			}

			const std::string_view rest = text.substr(terminatorStart);

			if (rest.starts_with(kCarriageReturnLineFeed))
			{
				return terminatorStart + kCarriageReturnLineFeed.size();
			}

			if (rest.starts_with(kLineFeed))
			{
				return terminatorStart + kLineFeed.size();
			}

			return kNotALineTerminator;
		}

		Signature ToSignature(const git_signature* signature)
		{
			if (signature == nullptr)
			{
				return Signature{};
			}

			return Signature{.Name = TextOrEmpty(signature->name),
							 .Email = TextOrEmpty(signature->email),
							 .When = std::chrono::sys_seconds{std::chrono::seconds{signature->when.time}}};
		}

		CommitSummary ToSummary(git_commit* commit)
		{
			CommitSummary summary;
			summary.Id = ToOid(*git_commit_id(commit));

			const unsigned int parentCount = git_commit_parentcount(commit);
			summary.Parents.reserve(parentCount);

			for (unsigned int parentIndex = 0; parentIndex < parentCount; ++parentIndex)
			{
				summary.Parents.push_back(ToOid(*git_commit_parent_id(commit, parentIndex)));
			}

			summary.Author = ToSignature(git_commit_author(commit));
			summary.Committer = ToSignature(git_commit_committer(commit));

			if (const char* summaryLine = git_commit_summary(commit); summaryLine != nullptr)
			{
				summary.Summary = summaryLine;
			}

			// git_commit_body() trims the surrounding whitespace and loses the trailing newline the
			// message was written with, so the body is cut out of the raw message instead.
			if (const char* rawMessage = git_commit_message(commit); rawMessage != nullptr)
			{
				const std::string_view message(rawMessage);
				const std::size_t titleEnd = message.find_first_of("\r\n");

				if (titleEnd != std::string_view::npos)
				{
					std::size_t bodyStart = OffsetAfterLineTerminator(message, titleEnd);

					if (bodyStart != kNotALineTerminator)
					{
						// Only the one blank line that separates title from body is skipped. A second
						// blank line is the body's own, as `git log --format=%b` shows it.
						if (const std::size_t afterSeparatorLine = OffsetAfterLineTerminator(message, bodyStart);
							afterSeparatorLine != kNotALineTerminator)
						{
							bodyStart = afterSeparatorLine;
						}

						if (bodyStart < message.size())
						{
							summary.Body = message.substr(bodyStart);
						}
					}
				}
			}

			return summary;
		}

		struct PatternMatchInDiff
		{
			const FileDiff* FirstMatchedFile = nullptr;
			bool MatchedSeveralFiles = false;
		};

		// Both sides of each file are checked, because a rename names the file differently on each:
		// the commit that renamed old.txt to new.txt is part of new.txt's history.
		PatternMatchInDiff MatchPatternInDiff(const Diff& diff, const std::filesystem::path& pattern)
		{
			const std::string patternText = ToGenericUtf8(pattern);

			PatternMatchInDiff match;

			for (const FileDiff& file : diff.Files)
			{
				if (!NamesPath(patternText, file.NewPath) && !NamesPath(patternText, file.OldPath))
				{
					continue;
				}

				if (match.FirstMatchedFile == nullptr)
				{
					match.FirstMatchedFile = &file;
					continue;
				}

				match.MatchedSeveralFiles = true;
				break;
			}

			return match;
		}

		struct UnpushedCommits
		{
			std::unordered_set<Oid> Commits;
			bool UnpushedKnown = false;
		};

		// Hiding the upstream stops the walk at the merge base, so this costs the ahead distance
		// rather than the length of the history.
		UnpushedCommits CollectUnpushed(git_repository* handle, const std::optional<std::string>& reference,
										std::size_t maxWalk)
		{
			UnpushedCommits unpushed;

			ReferenceHandle branch;
			int lookupResult = 0;

			if (reference.has_value())
			{
				lookupResult = git_reference_lookup(Out(branch), handle, reference->c_str());
			}
			else
			{
				lookupResult = git_repository_head(Out(branch), handle);
			}

			if (lookupResult != 0)
			{
				return unpushed;
			}

			// A detached HEAD resolves to a direct reference still named "HEAD", and a remote-tracking
			// ref is no branch here either: neither has an upstream to compare against.
			if (git_reference_is_branch(branch.get()) == 0)
			{
				return unpushed;
			}

			ReferenceHandle upstream;

			if (git_branch_upstream(Out(upstream), branch.get()) != 0)
			{
				return unpushed;
			}

			RevwalkHandle walk;

			if (git_revwalk_new(Out(walk), handle) == 0)
			{
				if (git_revwalk_push_ref(walk.get(), git_reference_name(branch.get())) == 0 &&
					git_revwalk_hide_ref(walk.get(), git_reference_name(upstream.get())) == 0)
				{
					git_oid commitId{};
					std::size_t walkedCount = 0;

					while (walkedCount < maxWalk && git_revwalk_next(&commitId, walk.get()) == 0)
					{
						unpushed.Commits.insert(ToOid(commitId));
						++walkedCount;
					}

					// A set cut off at the ceiling would read every commit past it as pushed, the one
					// wrong answer this must not give, so a partial set is thrown away as unknown.
					unpushed.UnpushedKnown = walkedCount < maxWalk;

					if (!unpushed.UnpushedKnown)
					{
						unpushed.Commits.clear();
					}
				}
			}

			return unpushed;
		}

		bool HasWildcard(const std::filesystem::path& pattern)
		{
			const std::string patternText = ToGenericUtf8(pattern);
			return patternText.find('*') != std::string::npos || patternText.find('?') != std::string::npos;
		}

		// A wildcard pattern, or one that named several files in this commit, names a set, and a set
		// has no single identity for a rename to carry.
		bool PatternNamesExactlyOneFile(const PatternMatchInDiff& match, const std::filesystem::path& pattern)
		{
			return !match.MatchedSeveralFiles && !HasWildcard(pattern);
		}
	}

	Result<LogPage> Repository::QueryLog(const LogRequest& request) const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		if (request.Stash.has_value())
		{
			git_oid stashId = ToGitOid(*request.Stash);

			CommitHandle stashCommit;

			if (git_commit_lookup(Out(stashCommit), m_Handle.get(), &stashId) != 0)
			{
				const std::string message = LastLibgit2Message();
				return Error{ErrorCode::NotFound, std::format("no such stash {}: {}", request.Stash->ToHex(), message)};
			}

			CommitSummary summary = ToSummary(stashCommit.get());

			if (summary.Parents.size() > kStashParentsShown)
			{
				summary.Parents.resize(kStashParentsShown);
			}

			LogPage page;

			if (CommitMatchesFilter(request.Filter, summary))
			{
				page.Commits.push_back(std::move(summary));
			}

			return page;
		}

		RevwalkHandle walk;

		if (git_revwalk_new(Out(walk), m_Handle.get()) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot walk the history: {}", LastLibgit2Message())};
		}

		// Topological first so a branch stays contiguous, date second so two unrelated branches
		// interleave the way a reader expects.
		git_revwalk_sorting(walk.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);

		int pushResult = 0;

		if (request.Ref.has_value())
		{
			pushResult = git_revwalk_push_ref(walk.get(), request.Ref->c_str());
		}
		else
		{
			pushResult = git_revwalk_push_head(walk.get());
		}

		if (pushResult != 0)
		{
			// An unborn HEAD lands here, and so does a ref deleted between the panel listing it and
			// this call. Both mean there is nothing to show, not a failure to read.
			const std::string message = LastLibgit2Message();

			if (request.Ref.has_value())
			{
				return Error{ErrorCode::NotFound, std::format("no such ref '{}': {}", *request.Ref, message)};
			}

			LogPage emptyHistory{.Commits = {}, .Complete = true};
			return emptyHistory;
		}

		// Measured against the ref itself, not the page: it is a property of the branch, so a
		// filtered page still marks the rows it does show correctly.
		UnpushedCommits unpushed = CollectUnpushed(m_Handle.get(), request.Ref, request.MaxWalk);

		std::vector<CommitSummary> commits;
		git_oid commitId{};

		std::optional<std::filesystem::path> trackedPath = request.File;
		std::optional<OneBasedLineRange> trackedRange = request.Lines;

		std::size_t walkedCount = 0;
		bool complete = true;

		while (true)
		{
			const bool pageIsFull = commits.size() >= request.MaxCount;
			const bool walkCeilingReached = walkedCount >= request.MaxWalk;

			// Stopping here says nothing about whether history goes on past this point, so the page
			// is not known to be the whole answer.
			if (pageIsFull || walkCeilingReached)
			{
				complete = false;
				break;
			}

			const bool historyRanOut = git_revwalk_next(&commitId, walk.get()) != 0;

			if (historyRanOut)
			{
				break;
			}

			++walkedCount;

			CommitHandle commit;

			if (git_commit_lookup(Out(commit), m_Handle.get(), &commitId) != 0)
			{
				CORE_TRACE("Skipping an unreadable commit while walking the history");
				complete = false;
				continue;
			}

			CommitSummary summary = ToSummary(commit.get());

			if (!trackedPath.has_value())
			{
				if (CommitMatchesFilter(request.Filter, summary))
				{
					commits.push_back(std::move(summary));
				}

				continue;
			}

			DiffRequest diffRequest;
			diffRequest.Target = DiffTarget::CommitToParent;
			diffRequest.From = summary.Id;

			Result<Diff> diff = QueryDiff(diffRequest);

			if (!diff.HasValue())
			{
				// An unread diff cannot say whether the commit touched the file or moved the range, and
				// mapping the range across a change never read is worse than stopping short.
				CORE_TRACE("Stopping the history walk: cannot diff commit {}: {}", summary.Id.ToHex(),
						   diff.GetError().Message);
				complete = false;
				break;
			}

			const PatternMatchInDiff match = MatchPatternInDiff(diff.GetValue(), *trackedPath);
			const FileDiff* matchedFile = match.FirstMatchedFile;

			if (matchedFile == nullptr)
			{
				continue;
			}

			// The rename is followed before the keep-or-drop decision below: a commit that renames the
			// file and edits only lines outside the range is dropped, and older commits still know the
			// file by its old name.
			const bool fileWasRenamedHere = matchedFile->Change == FileChange::Renamed && !matchedFile->OldPath.empty();

			if (request.FollowRenames && fileWasRenamedHere && PatternNamesExactlyOneFile(match, *trackedPath))
			{
				trackedPath = matchedFile->OldPath;
			}

			// The filter is asked only after the diff, so a commit it drops still follows the file
			// through a rename and maps the tracked range back to the parent.
			const bool changesTrackedLines = !trackedRange.has_value() || TouchesRange(*matchedFile, *trackedRange);

			if (changesTrackedLines && CommitMatchesFilter(request.Filter, summary))
			{
				commits.push_back(std::move(summary));
			}

			// A dropped commit's edits still move the tracked lines, so the range is mapped either way.
			if (trackedRange.has_value())
			{
				trackedRange = MapRangeToParent(*matchedFile, *trackedRange);

				if (!trackedRange.has_value())
				{
					complete = false;
					break;
				}
			}
		}

		return LogPage{.Commits = std::move(commits),
					   .Complete = complete,
					   .Unpushed = std::move(unpushed.Commits),
					   .UpstreamKnown = unpushed.UnpushedKnown};
	}
}
