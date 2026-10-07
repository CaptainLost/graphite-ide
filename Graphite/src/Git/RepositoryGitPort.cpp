#include "Git/RepositoryGitPort.h"

#include "Core/FileSystem/FileSystem.h"

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

	Core::Result<Core::Repository*> RepositoryGitPort::RepositoryForThisThread()
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
		std::erase_if(handles, [&root](const auto& entry) { return entry.first != root; });

		if (root.empty())
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
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

		// Open's own error is handed on, not flattened into "no repository": only a
		// NotFound means there is none, and the UI offers to initialise one on exactly that
		// answer. A repository libgit2 refuses - dubious ownership, a format it does not
		// understand, a corrupt config - is still a repository, and reading it as missing
		// would invite an init over the top of it. Still not cached, so a later git init
		// in this folder is picked up by the next query.
		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		++m_OpenCount;

		const auto inserted = handles.emplace(root, CachedRepository{std::move(opened).GetValue(), generation}).first;
		return &inserted->second.Repo;
	}

	std::size_t RepositoryGitPort::GetLiveHandleCountForThisThread() const
	{
		return HandlesForThisThread().size();
	}

	Core::Result<Core::WorkingTreeStatus> RepositoryGitPort::QueryStatus()
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->QueryStatus();
	}

	Core::Result<Core::WorkingTreeStatus> RepositoryGitPort::QueryUntrackedIn(
		const std::filesystem::path& directory)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->QueryUntrackedIn(directory);
	}

	Core::Result<std::filesystem::path> RepositoryGitPort::GetWorkingDirectory()
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->GetWorkingDirectory();
	}

	Core::Result<std::vector<Core::Branch>> RepositoryGitPort::QueryBranches()
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->QueryBranches();
	}

	Core::Result<Core::LogPage> RepositoryGitPort::QueryLog(const Core::LogRequest& request)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->QueryLog(request);
	}

	Core::Result<Core::Diff> RepositoryGitPort::QueryDiff(const Core::DiffRequest& request)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->QueryDiff(request);
	}

	Core::Result<FileTexts> RepositoryGitPort::ReadFileTexts(
		const Core::Oid& commit,
		const std::filesystem::path& oldPath,
		const std::filesystem::path& newPath)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		FileTexts texts;

		auto newSide = ReadSideText(repository->ReadBlob(newPath, commit));

		if (!newSide.HasValue())
		{
			return newSide.GetError();
		}

		SideText newText = std::move(newSide).GetValue();

		// A file a stash took untracked is in none of the stash's own trees but in the commit
		// it keeps those files in. A path that names a file and is absent from the commit can
		// only be that: in any other commit's diff, an absent new side is a delete, which has
		// no new path.
		if (!newText.Present && !newPath.empty())
		{
			auto untracked = repository->UntrackedOfStash(commit);

			if (!untracked.HasValue())
			{
				return untracked.GetError();
			}

			if (untracked.GetValue().has_value())
			{
				auto stashed = ReadSideText(repository->ReadBlob(newPath, *untracked.GetValue()));

				if (!stashed.HasValue())
				{
					return stashed.GetError();
				}

				newText = std::move(stashed).GetValue();
			}
		}

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
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->Stage(paths);
	}

	Core::Result<void> RepositoryGitPort::Unstage(const std::vector<std::filesystem::path>& paths)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->Unstage(paths);
	}

	Core::Result<void> RepositoryGitPort::DiscardWorkTree(const std::vector<std::filesystem::path>& paths)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->DiscardWorkTree(paths);
	}

	Core::Result<void> RepositoryGitPort::DeleteUntracked(const std::vector<std::filesystem::path>& paths)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->DeleteUntracked(paths);
	}

	Core::Result<Core::Oid> RepositoryGitPort::Commit(const Core::CommitRequest& request)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->Commit(request);
	}

	Core::Result<void> RepositoryGitPort::CreateBranch(const std::string& name, const Core::Oid& from)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->CreateBranch(name, from);
	}

	Core::Result<void> RepositoryGitPort::RenameBranch(const std::string& from, const std::string& to)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->RenameBranch(from, to);
	}

	Core::Result<std::size_t> RepositoryGitPort::CountUnmergedCommits(const std::string& name)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->CountUnmergedCommits(name);
	}

	Core::Result<void> RepositoryGitPort::DeleteBranch(const std::string& name)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->DeleteBranch(name);
	}

	Core::Result<bool> RepositoryGitPort::StashSave(const std::string& message, Core::UntrackedFiles untrackedFiles)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->StashSave(message, untrackedFiles);
	}

	Core::Result<std::vector<Core::StashEntry>> RepositoryGitPort::QueryStashes()
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->QueryStashes();
	}

	Core::Result<void> RepositoryGitPort::StashApply(const Core::Oid& id)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->StashApply(id);
	}

	Core::Result<void> RepositoryGitPort::StashPop(const Core::Oid& id)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->StashPop(id);
	}

	Core::Result<void> RepositoryGitPort::StashDrop(const Core::Oid& id)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->StashDrop(id);
	}

	Core::Result<Core::CheckoutPlan> RepositoryGitPort::PlanCheckout(const Core::CheckoutTarget& target)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->PlanCheckout(target);
	}

	Core::CheckoutOutcome RepositoryGitPort::Checkout(const Core::CheckoutTarget& target)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return Core::CheckoutOutcome{{}, opened.GetError()};
		}

		Core::Repository* repository = opened.GetValue();

		return repository->Checkout(target);
	}

	Core::Result<void> RepositoryGitPort::DiscardAll(Core::UntrackedFiles untrackedFiles)
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->DiscardAll(untrackedFiles);
	}

	Core::Result<Core::HeadState> RepositoryGitPort::QueryHead()
	{
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->QueryHead();
	}

	Core::Result<void> RepositoryGitPort::Init()
	{
		std::string root;

		{
			const std::lock_guard<std::mutex> lock{m_RootMutex};
			root = m_Root;
		}

		if (root.empty())
		{
			return Core::Error{Core::ErrorCode::NotFound, "no folder is open"};
		}

		// Asked again here rather than trusted from the last status the UI saw: an init is
		// only safe where Open finds nothing at all. Over a repository it would rewrite
		// .git/config, and in a subfolder of one it would nest a second repository - and the
		// same holds for a repository Open finds but refuses, which still counts as there.
		{
			auto existing = Core::Repository::Open(Core::FromUtf8(root));

			if (existing.HasValue())
			{
				return Core::Error{Core::ErrorCode::InvalidArgument, "the folder is already inside a repository"};
			}

			if (existing.GetError().Code != Core::ErrorCode::NotFound)
			{
				return existing.GetError();
			}
		}

		// The handle is dropped here rather than cached: RepositoryForThisThread never
		// caches a failed open, so every worker's next query opens the new repository on
		// its own thread, which is the only thread allowed to use it.
		auto created = Core::Repository::Init(Core::FromUtf8(root));

		if (!created.HasValue())
		{
			return created.GetError();
		}

		return Core::Result<void>::Ok();
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
				// side - the same answer as a file that did not exist then. Any other
				// failure is not an absent side, and showing it as one would draw the
				// whole file as added.
				if (!head.HasValue())
				{
					if (head.GetError().Code == Core::ErrorCode::NotFound)
					{
						return SideText{};
					}

					return head.GetError();
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
		auto opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

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
		auto opened = RepositoryForThisThread();

		// No repository is an answer, not a failure: the column is simply empty. One that
		// cannot be opened draws the same empty column - the gutter has no place to say why,
		// and the Changes panel already reports the open's own error.
		if (!opened.HasValue())
		{
			return ChangeBases{};
		}

		Core::Repository* repository = opened.GetValue();

		std::error_code code;
		const std::filesystem::path relative =
			std::filesystem::relative(absolutePath, repository->GetWorkingDirectory(), code);

		if (code || relative.empty() || *relative.begin() == "..")
		{
			return ChangeBases{};
		}

		if (repository->IsIgnored(absolutePath))
		{
			return ChangeBases{};
		}

		// A file inside a submodule belongs to the submodule's repository, not this one. Read
		// here it would have neither side and so be drawn as added from top to bottom.
		if (repository->IsInsideSubmodule(relative))
		{
			return ChangeBases{};
		}

		auto head = ReadOneSide(*repository, TextSide::Head, relative);

		// A side too large to read at all is the same answer as a side that decodes to
		// nothing: the file stays Tracked, but nothing is drawn for it, rather than the
		// whole call failing over a side ReadBases was never going to diff anyway.
		if (!head.HasValue())
		{
			return head.GetError().Code == Core::ErrorCode::InvalidArgument
					   ? ChangeBases{.Tracked = true, .Unusable = true}
					   : Core::Result<ChangeBases>{head.GetError()};
		}

		auto index = ReadOneSide(*repository, TextSide::Index, relative);

		if (!index.HasValue())
		{
			return index.GetError().Code == Core::ErrorCode::InvalidArgument
					   ? ChangeBases{.Tracked = true, .Unusable = true}
					   : Core::Result<ChangeBases>{index.GetError()};
		}

		SideText headSide = std::move(head).GetValue();
		SideText indexSide = std::move(index).GetValue();

		return MakeChangeBases(
			headSide.Present ? std::optional<std::string>{std::move(headSide.Text)} : std::nullopt,
			indexSide.Present ? std::optional<std::string>{std::move(indexSide.Text)} : std::nullopt);
	}
}
