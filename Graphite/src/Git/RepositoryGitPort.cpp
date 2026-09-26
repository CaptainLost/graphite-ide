#include "Git/RepositoryGitPort.h"

#include "Core/Fs/FileSystem.h"

#include <system_error>
#include <unordered_map>
#include <utility>

namespace Graphite {

	namespace {

		struct CachedRepository
		{
			Core::Repository Repo;
			std::uint64_t Generation;
		};

		// One map per thread, shared by every RepositoryGitPort instance that happens to
		// run on that thread - see the hazard this creates, described where
		// RepositoryForThisThread reads from it below. Factored out of that function so
		// GetLiveHandleCountForThisThread can read the same storage without duplicating
		// the thread_local declaration.
		std::unordered_map<std::string, CachedRepository>& HandlesForThisThread()
		{
			static thread_local std::unordered_map<std::string, CachedRepository> handles;
			return handles;
		}
	}

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
		// This map is `thread_local`, shared by every RepositoryGitPort instance that
		// happens to run on this thread, keyed by root text while the generation used to
		// judge staleness comes from `this`. With the single instance the layer owns, that
		// is harmless. Two instances on the same thread would not be, and the unconditional
		// sweep below makes that worse than it used to be: two instances at the *same* root
		// would fight over one slot, each reusing or evicting it by its own generation
		// counter as before, but two instances at *different* roots now also collide - each
		// call's sweep sees the other instance's root as "not the current one" and evicts
		// it, so the two would take turns evicting and reopening each other's handle on
		// every query instead of each keeping its own. Not reachable today: the layer owns
		// exactly one instance.
		auto& handles = HandlesForThisThread();

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

	std::size_t RepositoryGitPort::GetLiveHandleCountForThisThread() const
	{
		return HandlesForThisThread().size();
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

	Core::Result<Core::WorkingTreeStatus> RepositoryGitPort::QueryUntrackedIn(
		const std::filesystem::path& directory)
	{
		Core::Repository* repository = RepositoryForThisThread();

		if (repository == nullptr)
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		return repository->QueryUntrackedIn(directory);
	}

	Core::Result<std::filesystem::path> RepositoryGitPort::GetWorkingDirectory()
	{
		Core::Repository* repository = RepositoryForThisThread();

		if (repository == nullptr)
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		return repository->GetWorkingDirectory();
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

	Core::Result<Core::Diff> RepositoryGitPort::QueryDiff(const Core::DiffRequest& request)
	{
		Core::Repository* repository = RepositoryForThisThread();

		if (repository == nullptr)
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		return repository->QueryDiff(request);
	}

	Core::Result<FileTexts> RepositoryGitPort::ReadFileTexts(
		const Core::Oid& commit,
		const std::filesystem::path& oldPath,
		const std::filesystem::path& newPath)
	{
		Core::Repository* repository = RepositoryForThisThread();

		if (repository == nullptr)
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		FileTexts texts;

		auto newSide = ReadSideText(repository->ReadBlob(newPath, commit));

		if (!newSide.HasValue())
		{
			return newSide.GetError();
		}

		SideText newText = std::move(newSide).GetValue();
		texts.HasNew = newText.Present;
		texts.New = std::move(newText.Text);

		auto parent = repository->FirstParentOf(commit);

		if (!parent.HasValue())
		{
			return parent.GetError();
		}

		// A root commit has no parent, and FirstParentOf says so with an empty optional
		// rather than an error - the old side is then simply absent, the same SideText{}
		// an absent file's NotFound produces.
		Core::Result<SideText> oldSide = parent.GetValue().has_value()
			? ReadSideText(repository->ReadBlob(oldPath, *parent.GetValue()))
			: Core::Result<SideText>{SideText{}};

		if (!oldSide.HasValue())
		{
			return oldSide.GetError();
		}

		SideText oldText = std::move(oldSide).GetValue();
		texts.HasOld = oldText.Present;
		texts.Old = std::move(oldText.Text);

		return texts;
	}

	Core::Result<void> RepositoryGitPort::Stage(const std::vector<std::filesystem::path>& paths)
	{
		Core::Repository* repository = RepositoryForThisThread();

		if (repository == nullptr)
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		return repository->Stage(paths);
	}

	Core::Result<void> RepositoryGitPort::Unstage(const std::vector<std::filesystem::path>& paths)
	{
		Core::Repository* repository = RepositoryForThisThread();

		if (repository == nullptr)
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		return repository->Unstage(paths);
	}

	Core::Result<void> RepositoryGitPort::DiscardWorkTree(const std::vector<std::filesystem::path>& paths)
	{
		Core::Repository* repository = RepositoryForThisThread();

		if (repository == nullptr)
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		return repository->DiscardWorkTree(paths);
	}

	Core::Result<void> RepositoryGitPort::DeleteUntracked(const std::vector<std::filesystem::path>& paths)
	{
		Core::Repository* repository = RepositoryForThisThread();

		if (repository == nullptr)
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		return repository->DeleteUntracked(paths);
	}

	Core::Result<Core::Oid> RepositoryGitPort::Commit(const Core::CommitRequest& request)
	{
		Core::Repository* repository = RepositoryForThisThread();

		if (repository == nullptr)
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		return repository->Commit(request);
	}

	namespace {

		// One side, read from wherever the target says it lives. An Absent side is not read at
		// all and comes back empty, which is the same shape a missing file produces.
		Core::Result<SideText> ReadOneSide(Core::Repository& repository, TextSide side,
										   const std::filesystem::path& path)
		{
			if (side == TextSide::Absent || path.empty())
			{
				return SideText{};
			}

			switch (side)
			{
			case TextSide::Head:
			{
				auto head = repository.HeadCommit();

				// An unborn HEAD is a repository with no commit yet, so there is no older
				// side - the same answer as a file that did not exist then.
				if (!head.HasValue())
				{
					return SideText{};
				}

				return ReadSideText(repository.ReadBlob(path, head.GetValue()));
			}

			case TextSide::Index:
				return ReadSideText(repository.ReadIndexBlob(path));

			case TextSide::WorkingTree:
				return ReadSideText(repository.ReadWorkingTreeFile(path));

			case TextSide::Absent:
				break;
			}

			return SideText{};
		}
	}

	Core::Result<FileTexts> RepositoryGitPort::ReadWorkingTreeTexts(
		Core::DiffTarget target,
		const std::filesystem::path& oldPath,
		const std::filesystem::path& newPath)
	{
		Core::Repository* repository = RepositoryForThisThread();

		if (repository == nullptr)
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		const WorkingTreeSides sides = SidesFor(target);

		auto oldSide = ReadOneSide(*repository, sides.Old, oldPath);

		if (!oldSide.HasValue())
		{
			return oldSide.GetError();
		}

		auto newSide = ReadOneSide(*repository, sides.New, newPath);

		if (!newSide.HasValue())
		{
			return newSide.GetError();
		}

		SideText oldText = std::move(oldSide).GetValue();
		SideText newText = std::move(newSide).GetValue();

		FileTexts texts;
		texts.HasOld = oldText.Present;
		texts.Old = std::move(oldText.Text);
		texts.HasNew = newText.Present;
		texts.New = std::move(newText.Text);

		return texts;
	}

	Core::Result<ChangeBases> RepositoryGitPort::ReadBases(const std::filesystem::path& absolutePath)
	{
		Core::Repository* repository = RepositoryForThisThread();

		// No repository is an answer, not a failure: the column is simply empty.
		if (repository == nullptr)
			return ChangeBases{};

		std::error_code code;
		const std::filesystem::path relative =
			std::filesystem::relative(absolutePath, repository->GetWorkingDirectory(), code);

		if (code || relative.empty() || *relative.begin() == "..")
			return ChangeBases{};

		if (repository->IsIgnored(absolutePath))
			return ChangeBases{};

		auto head = ReadOneSide(*repository, TextSide::Head, relative);

		if (!head.HasValue())
			return head.GetError();

		auto index = ReadOneSide(*repository, TextSide::Index, relative);

		if (!index.HasValue())
			return index.GetError();

		SideText headSide = std::move(head).GetValue();
		SideText indexSide = std::move(index).GetValue();

		return MakeChangeBases(
			headSide.Present ? std::optional<std::string>{std::move(headSide.Text)} : std::nullopt,
			indexSide.Present ? std::optional<std::string>{std::move(indexSide.Text)} : std::nullopt);
	}
}
