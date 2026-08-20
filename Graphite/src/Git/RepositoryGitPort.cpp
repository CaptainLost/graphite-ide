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

		// The generation the entry was opened under travels with it, so a handle whose root
		// text still matches but whose repository has since been recreated is recognised as
		// stale and dropped, rather than answering queries against a repository that is gone.
		//
		// This map is function-local `static thread_local`, so it is shared by every
		// RepositoryGitPort instance that happens to run on this thread, keyed by root text
		// while the generation used to judge staleness comes from `this`. With the single
		// instance the layer owns, that is harmless. Two instances pointed at the same root
		// on the same thread would not be: each would see the other's entry as merely
		// "some cached handle for this root", reuse or evict it based on its own
		// generation counter, and the two would fight over one cache slot instead of each
		// keeping its own.
		struct CachedRepository
		{
			Core::Repository Repo;
			std::uint64_t Generation;
		};

		static thread_local std::unordered_map<std::string, CachedRepository> handles;

		// SetRoot cannot reach into this map itself: it runs on the UI thread, and this
		// cache is thread_local to whichever worker thread opened the handle - there is no
		// pointer from there to here to erase through. Instead, every entry whose key is
		// not the current root is dropped here, the next time this thread is asked
		// anything at all, whether that call is for the new root, for no root at all
		// (root.empty(), a closed workspace), or for a third root entirely. That closes a
		// closed-folder-then-different-folder open and a same-thread switch from one root
		// to another without an intervening close: either way, the entry left behind by the
		// old root does not outlive this thread's next call.
		//
		// What it cannot close: a workspace that is closed and then never queried again on
		// this thread. Nothing above ever calls back into a thread's own cache from
		// outside it, so a handle opened here before the close stays open - and, on
		// Windows, keeps the closed folder's packfiles mmapped - until this thread happens
		// to be asked something else, or exits. Fixing that would mean a shared,
		// mutex-guarded map reachable from the UI thread instead of one thread_local map
		// per worker, which is a different design than the one this cache is.
		for (auto it = handles.begin(); it != handles.end();)
		{
			if (it->first != root)
			{
				it = handles.erase(it);
			}
			else
			{
				++it;
			}
		}

		if (root.empty())
		{
			return nullptr;
		}

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

		++m_OpenCount;

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

	Core::Result<Core::LogPage> RepositoryGitPort::QueryLog(const Core::LogRequest& request)
	{
		Core::Repository* repository = RepositoryForThisThread();

		if (repository == nullptr)
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		return repository->QueryLog(request);
	}
}
