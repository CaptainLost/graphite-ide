#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Base/Ascii.h"
#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Core {

	namespace {

		struct OwnCommit
		{
			Signature Author;
			std::vector<Oid> Parents;
		};

		using OwnCommitMap = std::unordered_map<Oid, OwnCommit>;
		using NewestAuthorMap = std::map<std::string, Signature>;

		std::string AuthorKeyOf(const Signature& author)
		{
			return Lowered(Trimmed(author.Email));
		}

		bool IsNewerAuthor(const Signature& author, const Signature& otherAuthor)
		{
			if (author.When != otherAuthor.When)
			{
				return author.When > otherAuthor.When;
			}

			return AuthorKeyOf(author) < AuthorKeyOf(otherAuthor);
		}

		Error WalkFailure(const std::string& what)
		{
			return Error{ErrorCode::IoFailure, std::format("{}: {}", what, LastLibgit2Message())};
		}

		OwnCommit ReadOwnCommit(git_commit* commit)
		{
			OwnCommit ownCommit;
			ownCommit.Author = ToSignature(git_commit_author(commit));

			const unsigned int parentCount = git_commit_parentcount(commit);

			for (unsigned int parentIndex = 0; parentIndex < parentCount; ++parentIndex)
			{
				ownCommit.Parents.push_back(ToOid(*git_commit_parent_id(commit, parentIndex)));
			}

			return ownCommit;
		}

		Result<OwnCommitMap> WalkOwnCommits(git_repository* handle, const OwnAuthorsRequest& request)
		{
			RevwalkHandle walk;

			if (git_revwalk_new(Out(walk), handle) != 0)
			{
				return WalkFailure("cannot walk the history");
			}

			for (const Oid& tip : request.Tips)
			{
				const git_oid tipId = ToGitOid(tip);

				if (git_revwalk_push(walk.get(), &tipId) != 0)
				{
					return WalkFailure(std::format("cannot walk from {}", tip.ShortHex()));
				}
			}

			for (const Oid& hidden : request.Hidden)
			{
				const git_oid hiddenId = ToGitOid(hidden);

				if (git_revwalk_hide(walk.get(), &hiddenId) != 0)
				{
					return WalkFailure(std::format("cannot leave out {}", hidden.ShortHex()));
				}
			}

			OwnCommitMap ownCommits;
			git_oid commitId{};
			int nextResult = 0;

			while (true)
			{
				nextResult = git_revwalk_next(&commitId, walk.get());

				if (nextResult != 0)
				{
					break;
				}

				CommitHandle commit;

				if (git_commit_lookup(Out(commit), handle, &commitId) != 0)
				{
					return WalkFailure(std::format("cannot read commit {}", ToOid(commitId).ShortHex()));
				}

				ownCommits.emplace(ToOid(commitId), ReadOwnCommit(commit.get()));
			}

			// Anything but GIT_ITEROVER means the walk stopped partway, and a partial set must not pass
			// for the whole one.
			if (nextResult != GIT_ITEROVER)
			{
				return WalkFailure("cannot walk the history");
			}

			return ownCommits;
		}

		void RememberNewer(NewestAuthorMap& newestByKey, const Signature& author)
		{
			const std::string key = AuthorKeyOf(author);
			const NewestAuthorMap::iterator held = newestByKey.find(key);

			if (held == newestByKey.end())
			{
				newestByKey.emplace(key, author);
				return;
			}

			auto& [heldKey, heldAuthor] = *held;

			if (author.When > heldAuthor.When)
			{
				heldAuthor = author;
			}
		}

		bool HasUnvisitedOwnCommit(const std::vector<Oid>& pending, const std::unordered_set<Oid>& visited,
								   const OwnCommitMap& ownCommits)
		{
			for (const Oid& commit : pending)
			{
				if (!visited.contains(commit) && ownCommits.contains(commit))
				{
					return true;
				}
			}

			return false;
		}

		TipAuthors GatherTipAuthors(const Oid& tip, const OwnCommitMap& ownCommits, std::size_t maxCommits)
		{
			TipAuthors answer;
			answer.Tip = tip;

			NewestAuthorMap newestByKey;
			std::unordered_set<Oid> visited;
			std::vector<Oid> pending{tip};

			while (!pending.empty() && answer.OwnCommits < maxCommits)
			{
				const Oid commit = pending.back();
				pending.pop_back();

				if (visited.contains(commit))
				{
					continue;
				}

				visited.insert(commit);

				const OwnCommitMap::const_iterator found = ownCommits.find(commit);

				if (found == ownCommits.end())
				{
					continue;
				}

				const auto& [foundId, ownCommit] = *found;

				++answer.OwnCommits;
				RememberNewer(newestByKey, ownCommit.Author);

				for (const Oid& parent : ownCommit.Parents)
				{
					pending.push_back(parent);
				}
			}

			answer.Truncated = answer.OwnCommits == maxCommits && HasUnvisitedOwnCommit(pending, visited, ownCommits);

			for (const auto& [key, author] : newestByKey)
			{
				answer.Authors.push_back(author);
			}

			std::sort(answer.Authors.begin(), answer.Authors.end(), IsNewerAuthor);

			return answer;
		}
	}

	Result<std::vector<TipAuthors>> Repository::QueryOwnAuthors(const OwnAuthorsRequest& request) const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		std::vector<TipAuthors> answers;

		if (request.Tips.empty())
		{
			return answers;
		}

		const Result<OwnCommitMap> ownCommits = WalkOwnCommits(m_Handle.get(), request);

		if (!ownCommits.HasValue())
		{
			return ownCommits.GetError();
		}

		std::unordered_set<Oid> answeredTips;

		for (const Oid& tip : request.Tips)
		{
			if (answeredTips.contains(tip))
			{
				continue;
			}

			answeredTips.insert(tip);
			answers.push_back(GatherTipAuthors(tip, ownCommits.GetValue(), request.MaxCommitsPerTip));
		}

		return answers;
	}
}
