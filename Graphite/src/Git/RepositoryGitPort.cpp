#include "Git/RepositoryGitPort.h"

#include "Core/Fs/FileSystem.h"

#include <unordered_map>
#include <utility>

namespace Graphite {

	RepositoryGitPort::RepositoryGitPort(std::filesystem::path root)
		: m_Root(Core::ToUtf8(root))
	{
	}

	void RepositoryGitPort::SetRoot(std::filesystem::path root)
	{
		const std::lock_guard<std::mutex> lock{m_RootMutex};
		m_Root = Core::ToUtf8(root);

		// Bumped even when the path text is unchanged: the folder at that path may have been
		// closed and reopened - .git deleted and recreated, or re-cloned in place - and a
		// worker's cached handle for the old repository must not be mistaken for current.
		++m_Generation;
	}

	Core::Repository* RepositoryGitPort::RepositoryForThisThread()
	{
		std::string root;
		std::uint64_t generation = 0;

		{
			const std::lock_guard<std::mutex> lock{m_RootMutex};
			root = m_Root;
			generation = m_Generation;
		}

		if (root.empty())
		{
			return nullptr;
		}

		// The generation the entry was opened under travels with it, so a handle whose root
		// text still matches but whose repository has since been recreated is recognised as
		// stale and dropped, rather than answering queries against a repository that is gone.
		struct CachedRepository
		{
			Core::Repository Repo;
			std::uint64_t Generation;
		};

		static thread_local std::unordered_map<std::string, CachedRepository> handles;

		if (const auto existing = handles.find(root); existing != handles.end())
		{
			if (existing->second.Generation == generation)
			{
				return &existing->second.Repo;
			}

			// Erased rather than left in place: on Windows libgit2 keeps packfiles mmapped,
			// and a handle merely bypassed - not released - would keep blocking deletion of
			// the very .git directory the user is trying to replace.
			handles.erase(existing);
		}

		auto opened = Core::Repository::Open(Core::FromUtf8(root));

		if (!opened.HasValue())
		{
			return nullptr;
		}

		const auto [inserted, wasInserted] =
			handles.emplace(root, CachedRepository{std::move(opened).GetValue(), generation});
		(void)wasInserted;
		return &inserted->second.Repo;
	}

	Core::Result<Core::WorkingTreeStatus> RepositoryGitPort::QueryStatus()
	{
		Core::Repository* repository = RepositoryForThisThread();

		if (repository == nullptr)
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		return repository->QueryStatus();
	}

	Core::Result<std::vector<Core::Branch>> RepositoryGitPort::QueryBranches()
	{
		Core::Repository* repository = RepositoryForThisThread();

		if (repository == nullptr)
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		return repository->QueryBranches();
	}

	Core::Result<std::vector<Core::CommitSummary>> RepositoryGitPort::QueryLog(
		const Core::LogRequest& request)
	{
		Core::Repository* repository = RepositoryForThisThread();

		if (repository == nullptr)
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		return repository->QueryLog(request);
	}
}
