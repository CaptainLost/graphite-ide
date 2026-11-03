#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace Core {

	namespace {

		Error GraphFailure(const std::string& what)
		{
			return Error{ErrorCode::IoFailure, std::format("{}: {}", what, LastLibgit2Message())};
		}

		// One hex id per line; a line that is not an id is skipped rather than failing the whole read.
		std::unordered_set<Oid> ReadShallowBoundary(const std::filesystem::path& commonDirectory)
		{
			std::unordered_set<Oid> boundary;
			std::ifstream file{commonDirectory / "shallow"};
			std::string line;

			while (std::getline(file, line))
			{
				while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
				{
					line.pop_back();
				}

				if (const std::optional<Oid> id = Oid::FromHex(line))
				{
					boundary.insert(*id);
				}
			}

			return boundary;
		}

		GraphCommit ReadGraphCommit(git_commit* commit, const std::unordered_set<Oid>& shallowBoundary)
		{
			GraphCommit graphCommit;
			graphCommit.Id = ToOid(*git_commit_id(commit));
			graphCommit.Time = static_cast<std::int64_t>(git_commit_time(commit));

			const git_signature* author = git_commit_author(commit);
			graphCommit.AuthorKey = AuthorKeyOf(author != nullptr && author->email != nullptr ? author->email : "");

			if (shallowBoundary.contains(graphCommit.Id))
			{
				graphCommit.ParentsCut = true;
				return graphCommit;
			}

			const unsigned int parentCount = git_commit_parentcount(commit);

			for (unsigned int parentIndex = 0; parentIndex < parentCount; ++parentIndex)
			{
				graphCommit.Parents.push_back(ToOid(*git_commit_parent_id(commit, parentIndex)));
			}

			return graphCommit;
		}
	}

	Result<std::vector<GraphCommit>> Repository::ReadGraphCommits(const GraphReadRequest& request) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		std::vector<GraphCommit> commits;

		if (request.Tips.empty())
		{
			return commits;
		}

		RevwalkHandle walk;

		if (git_revwalk_new(Out(walk), m_Handle.get()) != 0)
		{
			return GraphFailure("cannot walk the history");
		}

		for (const Oid& tip : request.Tips)
		{
			const git_oid tipId = ToGitOid(tip);

			if (git_revwalk_push(walk.get(), &tipId) != 0)
			{
				return GraphFailure(std::format("cannot walk from {}", tip.ShortHex()));
			}
		}

		for (const Oid& known : request.Known)
		{
			const git_oid knownId = ToGitOid(known);

			if (git_revwalk_hide(walk.get(), &knownId) != 0)
			{
				return GraphFailure(std::format("cannot leave out {}", known.ShortHex()));
			}
		}

		const std::unordered_set<Oid> shallowBoundary = ReadShallowBoundary(CommonDirectory());

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

			if (git_commit_lookup(Out(commit), m_Handle.get(), &commitId) != 0)
			{
				return GraphFailure(std::format("cannot read commit {}", ToOid(commitId).ShortHex()));
			}

			commits.push_back(ReadGraphCommit(commit.get(), shallowBoundary));
		}

		// Anything but GIT_ITEROVER means the walk stopped partway, and a partial graph must not pass for
		// the whole one.
		if (nextResult != GIT_ITEROVER)
		{
			return GraphFailure("cannot walk the history");
		}

		return commits;
	}

	bool Repository::IsShallow() const
	{
		return m_Handle != nullptr && git_repository_is_shallow(m_Handle.get()) == 1;
	}

	std::filesystem::path Repository::CommonDirectory() const
	{
		if (m_Handle == nullptr)
		{
			return {};
		}

		return FromUtf8(git_repository_commondir(m_Handle.get()));
	}

	Result<std::vector<std::string>> Repository::QueryRemoteNames() const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		git_strarray names{};

		if (git_remote_list(&names, m_Handle.get()) != 0)
		{
			return GraphFailure("cannot list the remotes");
		}

		std::vector<std::string> remoteNames;

		for (std::size_t nameIndex = 0; nameIndex < names.count; ++nameIndex)
		{
			remoteNames.emplace_back(names.strings[nameIndex]);
		}

		git_strarray_dispose(&names);

		std::sort(remoteNames.begin(), remoteNames.end());
		return remoteNames;
	}
}
