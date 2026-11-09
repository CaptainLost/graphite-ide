#include "Git/RepositoryGitPort.h"

#include "Git/Worktrees/WorktreeOfFile.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Command/GitExecutable.h"
#include "Core/Git/Command/GitNetwork.h"
#include "Core/Git/Content/ContentCheck.h"
#include "Core/Git/Content/ContentThresholds.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace Graphite {

	namespace {

		Core::NetworkOutcome NetworkFailure(const char* sentence)
		{
			return Core::NetworkOutcome{.Result = Core::NetworkResult::Failed, .Failure = {.Lines = {sentence}, .Errors = sentence}};
		}

		std::filesystem::path ShallowFileOf(const Core::Repository& repository)
		{
			return repository.CommonDirectory() / "shallow";
		}

		// One look at the file's attributes. A file that cannot be looked at counts as absent: at worst a handle is opened
		// once more.
		ShallowStamp ShallowStampOf(const std::filesystem::path& shallowFile)
		{
			std::error_code error;
			const std::filesystem::directory_entry entry{shallowFile, error};

			if (error || !entry.exists(error))
			{
				return ShallowStamp{};
			}

			return ShallowStamp{.Exists = true, .Size = entry.file_size(error), .Written = entry.last_write_time(error)};
		}

		struct CachedRepository
		{
			Core::Repository Repo;
			std::uint64_t Generation;

			// WARNING: libgit2 keeps every commit it has parsed, and one parsed while the clone was shallow keeps its cut
			// parents after a fetch deepens the clone, whoever ran it. A handle whose shallow file has changed since it was
			// opened is dropped, so the history and the lines are read from the commits as they are now.
			std::filesystem::path ShallowFile;
			ShallowStamp Shallow;
		};

		using HandleMap = std::unordered_map<std::string, CachedRepository>;

		// WARNING: Kept apart per port, by the port's number: the open folder's port and the looked-at worktree's share
		// the workers, and one map for both would have each evict and reopen the other's handle on every query, losing
		// every commit its handle had parsed. Numbers are never reused, so a port made where another was destroyed never
		// takes over its handles.
		using HandleMapsByPort = std::unordered_map<std::uint64_t, HandleMap>;

		HandleMapsByPort& HandleMapsForThisThread()
		{
			static thread_local HandleMapsByPort handlesByPort;
			return handlesByPort;
		}

		// Opened with ObjectWrites::InMemory: what a content check's merge writes never reaches the repository.
		HandleMapsByPort& ContentHandleMapsForThisThread()
		{
			static thread_local HandleMapsByPort contentHandlesByPort;
			return contentHandlesByPort;
		}

		HandleMap& HandlesForThisThread(std::uint64_t port)
		{
			return HandleMapsForThisThread()[port];
		}

		HandleMap& ContentHandlesForThisThread(std::uint64_t port)
		{
			return ContentHandleMapsForThisThread()[port];
		}

		HandleMap& HandlesForThisThreadThatWrite(std::uint64_t port, Core::ObjectWrites writes)
		{
			if (writes == Core::ObjectWrites::InMemory)
			{
				return ContentHandlesForThisThread(port);
			}

			return HandlesForThisThread(port);
		}

		std::size_t HandleCountForThisThread(const HandleMapsByPort& handlesByPort, std::uint64_t port)
		{
			const HandleMapsByPort::const_iterator found = handlesByPort.find(port);
			return found == handlesByPort.end() ? 0 : found->second.size();
		}

		std::uint64_t NextPortNumber()
		{
			static std::atomic<std::uint64_t> lastPortNumber{0};
			return ++lastPortNumber;
		}

		// WARNING: SetRoot runs on the UI thread and cannot reach another thread's thread_local map,
		// so each thread drops handles of other roots on its next call, and callers must run this
		// before returning for an empty root. On Windows an open handle keeps the packfiles of a
		// closed folder mapped. A workspace closed and never queried again on a thread keeps that
		// thread's handle until the thread's next call or exit.
		void DropHandlesOfOtherRoots(HandleMap& handlesByRoot, const std::string& root)
		{
			HandleMap::iterator entry = handlesByRoot.begin();

			while (entry != handlesByRoot.end())
			{
				auto& [entryRoot, entryRepository] = *entry;

				if (entryRoot != root)
				{
					entry = handlesByRoot.erase(entry);
				}
				else
				{
					++entry;
				}
			}
		}

		// WARNING: A stale handle is erased, not bypassed. On Windows libgit2 keeps packfiles
		// mapped, and a handle left open would block deleting the .git directory.
		void DropStaleHandle(HandleMap& handlesByRoot, const std::string& root, std::uint64_t generation)
		{
			const HandleMap::iterator cachedEntry = handlesByRoot.find(root);

			if (cachedEntry == handlesByRoot.end())
			{
				return;
			}

			const auto& [cachedRoot, cachedRepository] = *cachedEntry;

			if (cachedRepository.Generation != generation)
			{
				handlesByRoot.erase(cachedEntry);
			}
		}
	}

	RepositoryGitPort::RepositoryGitPort(std::filesystem::path root, std::filesystem::path lineStoreRoot)
		: m_Number(NextPortNumber()), m_Root(Core::ToUtf8(root)), m_LineStoreRoot(std::move(lineStoreRoot))
	{
	}

	RepositoryGitPort::~RepositoryGitPort()
	{
		// The destroying thread's handles only: every worker that queried the port is joined before it goes, and its
		// handles went with its thread.
		HandleMapsForThisThread().erase(m_Number);
		ContentHandleMapsForThisThread().erase(m_Number);
	}

	void RepositoryGitPort::SetRoot(std::filesystem::path root)
	{
		const std::lock_guard<std::mutex> lock{m_RootMutex};
		m_Root = Core::ToUtf8(root);

		// WARNING: The generation is bumped even when the path text is unchanged, because the
		// repository at that path may have been deleted and recreated, and a cached handle to the
		// old one must not be mistaken for current.
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

		return OpenForThisThread(root, generation);
	}

	Core::Result<Core::Repository*> RepositoryGitPort::RepositoryForThisThreadOf(std::uint64_t generation, Core::ObjectWrites writes)
	{
		std::string root;

		{
			const std::lock_guard<std::mutex> lock{m_RootMutex};

			if (m_Generation != generation)
			{
				return Core::Error{Core::ErrorCode::Cancelled, "The folder changed since this was asked, so nothing was read."};
			}

			root = m_Root;
		}

		return OpenForThisThread(root, generation, writes);
	}

	Core::Result<Core::Repository*> RepositoryGitPort::OpenForThisThread(const std::string& root, std::uint64_t generation,
																		 Core::ObjectWrites writes)
	{
		// Both kinds of handle of a root or generation no longer open go, whichever is asked for: each keeps its packfiles
		// mapped.
		for (HandleMap* handles : {&HandlesForThisThread(m_Number), &ContentHandlesForThisThread(m_Number)})
		{
			DropHandlesOfOtherRoots(*handles, root);
			DropStaleHandle(*handles, root, generation);
		}

		HandleMap& handlesByRoot = HandlesForThisThreadThatWrite(m_Number, writes);

		if (root.empty())
		{
			return Core::Error{Core::ErrorCode::NotFound, "no repository is open"};
		}

		HandleMap::iterator cachedEntry = handlesByRoot.find(root);

		if (cachedEntry != handlesByRoot.end())
		{
			auto& [cachedRoot, cachedRepository] = *cachedEntry;

			if (cachedRepository.Shallow == ShallowStampOf(cachedRepository.ShallowFile))
			{
				return &cachedRepository.Repo;
			}

			handlesByRoot.erase(cachedEntry);
		}

		Core::Result<Core::Repository> opened = Core::Repository::Open(Core::FromUtf8(root), writes);

		// WARNING: Open's error is returned as is. Only NotFound means there is no repository, and
		// the UI offers to initialise one on exactly that answer. A failed open is not cached, so a
		// later git init in this folder is picked up by the next query.
		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		++m_OpenCount;

		// Taken before the handle has parsed a commit: a change after it is one the handle may have missed, and drops it.
		std::filesystem::path shallowFile = ShallowFileOf(opened.GetValue());
		const ShallowStamp shallow = ShallowStampOf(shallowFile);

		std::pair<HandleMap::iterator, bool> insertion =
			handlesByRoot.emplace(root, CachedRepository{std::move(opened).GetValue(), generation, std::move(shallowFile), shallow});
		auto& [insertedEntry, wasInserted] = insertion;
		auto& [insertedRoot, insertedRepository] = *insertedEntry;
		return &insertedRepository.Repo;
	}

	void RepositoryGitPort::LetGoOfHandlesNoLongerCurrent()
	{
		std::string root;
		std::uint64_t generation = 0;

		{
			const std::lock_guard<std::mutex> lock{m_RootMutex};
			root = m_Root;
			generation = m_Generation;
		}

		for (HandleMap* handlesByRoot : {&HandlesForThisThread(m_Number), &ContentHandlesForThisThread(m_Number)})
		{
			DropHandlesOfOtherRoots(*handlesByRoot, root);
			DropStaleHandle(*handlesByRoot, root, generation);
		}
	}

	std::size_t RepositoryGitPort::GetLiveHandleCountForThisThread() const
	{
		return HandleCountForThisThread(HandleMapsForThisThread(), m_Number) +
			   HandleCountForThisThread(ContentHandleMapsForThisThread(), m_Number);
	}

	namespace {
		// WARNING: Asks the disk once per untracked directory, here on the thread that read the status, so that no frame
		// does: the decorations answer for a nested checkout from the status alone.
		void MarkNestedCheckouts(const std::filesystem::path& workingDirectory, Core::WorkingTreeStatus& status)
		{
			for (Core::FileStatus& entry : status.Entries)
			{
				if (entry.IsUntracked && entry.IsDirectory)
				{
					std::error_code error;
					entry.HoldsCheckout = std::filesystem::exists(workingDirectory / entry.Path / ".git", error);
				}
			}
		}
	}

	Core::Result<Core::WorkingTreeStatus> RepositoryGitPort::QueryStatus()
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		Core::Result<Core::WorkingTreeStatus> status = repository->QueryStatus();

		if (status.HasValue())
		{
			MarkNestedCheckouts(repository->GetWorkingDirectory(), status.GetValue());
		}

		return status;
	}

	Core::Result<Core::WorkingTreeStatus> RepositoryGitPort::QueryUntrackedIn(
		const std::filesystem::path& directory)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->QueryUntrackedIn(directory);
	}

	Core::Result<std::filesystem::path> RepositoryGitPort::GetWorkingDirectory()
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->GetWorkingDirectory();
	}

	Core::Result<std::vector<Core::Branch>> RepositoryGitPort::QueryBranches()
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->QueryBranches();
	}

	Core::Result<std::vector<Core::Tag>> RepositoryGitPort::QueryTags(Core::TagCache& cache)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->QueryTags(cache);
	}

	Core::Result<std::vector<Core::TipAuthors>> RepositoryGitPort::QueryOwnAuthors(const Core::OwnAuthorsRequest& request)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->QueryOwnAuthors(request);
	}

	Core::Result<std::optional<std::string>> RepositoryGitPort::ReadUserEmail()
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->ReadUserEmail();
	}

	Core::Result<std::vector<Core::Remote>> RepositoryGitPort::QueryRemotes()
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		return opened.GetValue()->QueryRemotes();
	}

	Core::Result<Core::LogPage> RepositoryGitPort::QueryLog(const Core::LogRequest& request)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->QueryLog(request);
	}

	Core::Result<CommitCount> RepositoryGitPort::CountCommits(const std::optional<std::string>& ref,
															  Core::FollowedParents parents,
															  const std::optional<CommitCount>& known)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		Core::Result<Core::Oid> tip = ref.has_value() ? repository->ResolveReference(*ref) : repository->HeadCommit();

		if (!tip.HasValue())
		{
			return tip.GetError();
		}

		if (known.has_value() && known->Tip == tip.GetValue())
		{
			return *known;
		}

		Core::Result<std::size_t> count = repository->CountCommits(tip.GetValue(), parents);

		if (!count.HasValue())
		{
			return count.GetError();
		}

		return CommitCount{.Tip = tip.GetValue(), .Count = count.GetValue()};
	}

	Core::Result<Core::Diff> RepositoryGitPort::QueryDiff(const Core::DiffRequest& request)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

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
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		FileTexts texts;

		Core::Result<SideText> newSide = ReadSideText(repository->ReadBlob(newPath, commit));

		if (!newSide.HasValue())
		{
			return newSide.GetError();
		}

		SideText newText = std::move(newSide).GetValue();

		// WARNING: A file a stash took untracked is in none of the stash's own trees but in the
		// commit that holds those files. In any other commit an absent new side is a delete,
		// which has no new path.
		if (!newText.Present && !newPath.empty())
		{
			Core::Result<std::optional<Core::Oid>> untracked = repository->UntrackedOfStash(commit);

			if (!untracked.HasValue())
			{
				return untracked.GetError();
			}

			if (untracked.GetValue().has_value())
			{
				Core::Result<SideText> stashed = ReadSideText(repository->ReadBlob(newPath, *untracked.GetValue()));

				if (!stashed.HasValue())
				{
					return stashed.GetError();
				}

				newText = std::move(stashed).GetValue();
			}
		}

		texts.HasNew = newText.Present;
		texts.New = std::move(newText.Text);

		Core::Result<std::optional<Core::Oid>> parent = repository->FirstParentOf(commit);

		if (!parent.HasValue())
		{
			return parent.GetError();
		}

		const bool isRootCommit = !parent.GetValue().has_value();
		Core::Result<SideText> oldSide = isRootCommit
											 ? Core::Result<SideText>{SideText{}}
											 : ReadSideText(repository->ReadBlob(oldPath, *parent.GetValue()));

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
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->Stage(paths);
	}

	Core::Result<void> RepositoryGitPort::Unstage(const std::vector<std::filesystem::path>& paths)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->Unstage(paths);
	}

	Core::Result<void> RepositoryGitPort::DiscardWorkTree(const std::vector<std::filesystem::path>& paths)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->DiscardWorkTree(paths);
	}

	Core::Result<void> RepositoryGitPort::DeleteUntracked(const std::vector<std::filesystem::path>& paths)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->DeleteUntracked(paths);
	}

	Core::Result<Core::Oid> RepositoryGitPort::Commit(const Core::CommitRequest& request)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->Commit(request);
	}

	Core::Result<void> RepositoryGitPort::CreateBranch(const std::string& name, const Core::Oid& from)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->CreateBranch(name, from);
	}

	Core::Result<void> RepositoryGitPort::RenameBranch(const std::string& from, const std::string& to)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->RenameBranch(from, to);
	}

	Core::Result<std::size_t> RepositoryGitPort::CountUnmergedCommits(const std::string& name)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->CountUnmergedCommits(name);
	}

	Core::Result<void> RepositoryGitPort::DeleteBranch(const std::string& name)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->DeleteBranch(name);
	}

	Core::Result<bool> RepositoryGitPort::StashSave(const std::string& message, Core::UntrackedFiles untrackedFiles)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->StashSave(message, untrackedFiles);
	}

	Core::Result<std::vector<Core::StashEntry>> RepositoryGitPort::QueryStashes()
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->QueryStashes();
	}

	Core::Result<void> RepositoryGitPort::StashApply(const Core::Oid& id)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->StashApply(id);
	}

	Core::Result<void> RepositoryGitPort::StashPop(const Core::Oid& id)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->StashPop(id);
	}

	Core::Result<void> RepositoryGitPort::StashDrop(const Core::Oid& id)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->StashDrop(id);
	}

	Core::Result<Core::CheckoutPlan> RepositoryGitPort::PlanCheckout(const Core::CheckoutTarget& target)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->PlanCheckout(target);
	}

	Core::CheckoutOutcome RepositoryGitPort::Checkout(const Core::CheckoutTarget& target)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return Core::CheckoutOutcome{{}, opened.GetError()};
		}

		Core::Repository* repository = opened.GetValue();

		return repository->Checkout(target);
	}

	Core::Result<void> RepositoryGitPort::DiscardAll(Core::UntrackedFiles untrackedFiles)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		return repository->DiscardAll(untrackedFiles);
	}

	Core::Result<Core::HeadState> RepositoryGitPort::QueryHead()
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

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

		// WARNING: Open is asked again here, not trusted from the last status the UI saw. An init
		// is safe only where Open finds nothing: over a repository it rewrites .git/config, inside
		// one it nests a second, and a repository Open refuses still counts as there.
		{
			Core::Result<Core::Repository> existing = Core::Repository::Open(Core::FromUtf8(root));

			if (existing.HasValue())
			{
				return Core::Error{Core::ErrorCode::InvalidArgument, "the folder is already inside a repository"};
			}

			if (existing.GetError().Code != Core::ErrorCode::NotFound)
			{
				return existing.GetError();
			}
		}

		// WARNING: The created handle is dropped, not cached: each worker's next query opens the
		// new repository on its own thread, the only thread allowed to use that handle.
		Core::Result<Core::Repository> created = Core::Repository::Init(Core::FromUtf8(root));

		if (!created.HasValue())
		{
			return created.GetError();
		}

		return Core::Result<void>::Ok();
	}

	namespace {

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
				Core::Result<Core::Oid> head = repository.HeadCommit();

				// WARNING: An unborn HEAD has no older side. Any other failure must stay a failure,
				// because showing it as an absent side would draw the whole file as added.
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

		// WARNING: InvalidArgument means the side is too large to read. The file stays Tracked with
		// nothing drawn, rather than failing the whole call over a side that would never be diffed.
		Core::Result<ChangeBases> BasesForUnreadableSide(const Core::Error& error)
		{
			if (error.Code == Core::ErrorCode::InvalidArgument)
			{
				return ChangeBases{.Tracked = true, .Unusable = true};
			}

			return error;
		}
	}

	Core::Result<FileTexts> RepositoryGitPort::ReadWorkingTreeTexts(
		Core::DiffTarget target,
		const std::filesystem::path& oldPath,
		const std::filesystem::path& newPath)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		const WorkingTreeSides sides = SidesFor(target);

		Core::Result<SideText> oldSide = ReadOneSide(*repository, sides.Old, oldPath);

		if (!oldSide.HasValue())
		{
			return oldSide.GetError();
		}

		Core::Result<SideText> newSide = ReadOneSide(*repository, sides.New, newPath);

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

	namespace {

		// Empty when the file is outside the working directory, ignored, inside a submodule or inside a nested checkout.
		std::optional<std::filesystem::path> RelativePathOfTrackableFile(Core::Repository& repository,
																		 const std::filesystem::path& absolutePath)
		{
			std::error_code relativeError;
			const std::filesystem::path relative =
				std::filesystem::relative(absolutePath, repository.GetWorkingDirectory(), relativeError);

			if (relativeError || relative.empty() || *relative.begin() == "..")
			{
				return std::nullopt;
			}

			if (repository.IsIgnored(absolutePath))
			{
				return std::nullopt;
			}

			// WARNING: A checkout nested in this one, such as a worktree under the working directory that no rule
			// ignores, answers for its own files; this repository would read them as untracked or missing.
			if (IsInsideNestedCheckout(repository.GetWorkingDirectory(), relative))
			{
				return std::nullopt;
			}

			// WARNING: A file inside a submodule belongs to the submodule's repository. Read here it
			// would have neither side and be drawn as added from top to bottom.
			if (repository.IsInsideSubmodule(relative))
			{
				return std::nullopt;
			}

			return relative;
		}
	}

	Core::Result<ChangeBases> RepositoryGitPort::ReadBases(const std::filesystem::path& absolutePath)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		// WARNING: No repository, or one that cannot be opened, draws an empty column rather than
		// failing: the gutter has no place to say why, and the Changes panel reports the open error.
		if (!opened.HasValue())
		{
			return ChangeBases{};
		}

		Core::Repository* repository = opened.GetValue();

		const std::optional<std::filesystem::path> trackable = RelativePathOfTrackableFile(*repository, absolutePath);

		if (!trackable.has_value())
		{
			return ChangeBases{};
		}

		const std::filesystem::path& relative = *trackable;

		Core::Result<SideText> head = ReadOneSide(*repository, TextSide::Head, relative);

		if (!head.HasValue())
		{
			return BasesForUnreadableSide(head.GetError());
		}

		Core::Result<SideText> index = ReadOneSide(*repository, TextSide::Index, relative);

		if (!index.HasValue())
		{
			return BasesForUnreadableSide(index.GetError());
		}

		SideText headSide = std::move(head).GetValue();
		SideText indexSide = std::move(index).GetValue();

		return MakeChangeBases(
			headSide.Present ? std::optional<std::string>{std::move(headSide.Text)} : std::nullopt,
			indexSide.Present ? std::optional<std::string>{std::move(indexSide.Text)} : std::nullopt);
	}

	Core::Result<HeadBlame> RepositoryGitPort::BlameAtHead(const std::filesystem::path& absolutePath)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		// WARNING: No repository, or one that cannot be opened, leaves the file untracked rather than
		// failing: the editor has no place to say why, and the Git panel reports the open error.
		if (!opened.HasValue())
		{
			return HeadBlame{};
		}

		Core::Repository* repository = opened.GetValue();

		const std::optional<std::filesystem::path> trackable = RelativePathOfTrackableFile(*repository, absolutePath);

		if (!trackable.has_value())
		{
			return HeadBlame{};
		}

		const Core::Result<Core::Oid> head = repository->HeadCommit();

		if (!head.HasValue())
		{
			if (head.GetError().Code == Core::ErrorCode::NotFound)
			{
				return HeadBlame{};
			}

			return head.GetError();
		}

		Core::Result<std::string> bytes = repository->ReadBlob(*trackable, head.GetValue());

		if (!bytes.HasValue())
		{
			if (bytes.GetError().Code == Core::ErrorCode::NotFound)
			{
				return HeadBlame{};
			}

			return bytes.GetError();
		}

		std::optional<std::string> text = DecodeBaseText(bytes.GetValue());

		HeadBlame result;
		result.Tracked = true;

		if (!text.has_value())
		{
			result.Unusable = true;
			return result;
		}

		result.HeadText = std::move(*text);

		// An empty file has no line to blame, and the blame engine is not asked about one.
		if (result.HeadText.empty())
		{
			Core::Blame empty;
			empty.At = head.GetValue();
			empty.File = *trackable;
			result.Blame = Core::CreateRef<Core::Blame>(std::move(empty));
			return result;
		}

		Core::Result<Core::Blame> blame = repository->QueryBlame(Core::BlameRequest{*trackable, head.GetValue()});

		if (!blame.HasValue())
		{
			return blame.GetError();
		}

		result.Blame = Core::CreateRef<Core::Blame>(std::move(blame).GetValue());
		return result;
	}

	Core::Result<Core::LineHistory> RepositoryGitPort::QueryLineHistory(const Core::LineHistoryRequest& request)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		return opened.GetValue()->QueryLineHistory(request);
	}

	namespace {

		constexpr std::size_t kMostCommitsListedInRange = 500;

		bool IsNotFound(const Core::Error& error)
		{
			return error.Code == Core::ErrorCode::NotFound;
		}

		Core::Result<SideText> ReadBlobSide(Core::Repository& repository, const std::filesystem::path& path,
											const Core::Oid& revision)
		{
			if (path.empty())
			{
				return SideText{};
			}

			return ReadSideText(repository.ReadBlob(path, revision));
		}

		FileTexts TextsOf(SideText oldText, SideText newText)
		{
			FileTexts texts;
			texts.HasOld = oldText.Present;
			texts.Old = std::move(oldText.Text);
			texts.HasNew = newText.Present;
			texts.New = std::move(newText.Text);
			return texts;
		}

		Core::Result<std::string> ReadCurrentVersion(Core::Repository& repository, const SinceViewedRequest& request)
		{
			if (request.Tip.has_value())
			{
				return repository.ReadBlob(request.Path, *request.Tip);
			}

			if (!request.Worktree.has_value())
			{
				return Core::Error{Core::ErrorCode::InvalidArgument, "a since-viewed request names neither a tip nor a worktree"};
			}

			Core::Result<Core::Repository> worktree = Core::Repository::OpenExactly(*request.Worktree);

			if (!worktree.HasValue())
			{
				return worktree.GetError();
			}

			return worktree.GetValue().ReadWorkingTreeFile(request.Path);
		}

		// WARNING: A since-viewed answer keeps NotFound for the viewed blob alone. A current version
		// that is not found, such as a file deleted since the diff or a removed worktree, is a failed read.
		Core::Error ReclassifyMissingCurrentVersionAsIoFailure(Core::Error error)
		{
			if (error.Code == Core::ErrorCode::NotFound)
			{
				error.Code = Core::ErrorCode::IoFailure;
			}

			return error;
		}
	}

	Core::Result<ReviewRange> RepositoryGitPort::ResolveRange(const ReviewKey& key)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();
		ReviewRange range;

		const Core::Result<Core::Oid> tip = repository->ResolveReference(key.Branch);

		if (!tip.HasValue())
		{
			if (!IsNotFound(tip.GetError()))
			{
				return tip.GetError();
			}

			range.Problem = RangeProblem::BranchMissing;
			return range;
		}

		// WARNING: A failed lookup reads as held nowhere. The review still works; only the
		// uncommitted toggle is not offered.
		const Core::Result<std::optional<std::filesystem::path>> holding = repository->WorktreeHolding(key.Branch);

		if (holding.HasValue())
		{
			range.Worktree = holding.GetValue();
		}

		const Core::Result<Core::Oid> baseTip = repository->ResolveReference(key.Base);

		if (!baseTip.HasValue())
		{
			if (!IsNotFound(baseTip.GetError()))
			{
				return baseTip.GetError();
			}

			range.Problem = RangeProblem::BaseMissing;
			return range;
		}

		const Core::Result<Core::Oid> mergeBase = repository->MergeBase(tip.GetValue(), baseTip.GetValue());

		if (!mergeBase.HasValue())
		{
			if (!IsNotFound(mergeBase.GetError()))
			{
				return mergeBase.GetError();
			}

			range.Problem = RangeProblem::NoCommonAncestor;
			return range;
		}

		range.Ends = RangeEnds{.MergeBase = mergeBase.GetValue(), .Tip = tip.GetValue()};

		Core::LogRequest commitsSinceMergeBase;
		commitsSinceMergeBase.Ref = key.Branch;
		commitsSinceMergeBase.Exclude = range.Ends.MergeBase;
		commitsSinceMergeBase.MaxCount = kMostCommitsListedInRange;

		Core::Result<Core::LogPage> page = repository->QueryLog(commitsSinceMergeBase);

		if (!page.HasValue())
		{
			return page.GetError();
		}

		Core::LogPage commits = std::move(page).GetValue();
		range.Commits = std::move(commits.Commits);
		range.CommitsComplete = commits.Complete;

		return range;
	}

	Core::Result<Core::Diff> RepositoryGitPort::QueryRangeDiff(const RangeEnds& ends, Core::WhitespaceInDiff whitespace)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::DiffRequest request;
		request.Target = Core::DiffTarget::RevisionToRevision;
		request.To = ends.Tip;
		request.Whitespace = whitespace;

		// WARNING: A From left unset is the empty tree. A zero id would be looked up as a commit
		// and not found.
		if (!ends.MergeBase.IsZero())
		{
			request.From = ends.MergeBase;
		}

		return opened.GetValue()->QueryDiff(request);
	}

	Core::Result<FileTexts> RepositoryGitPort::ReadRangeTexts(const RangeEnds& ends,
															  const std::filesystem::path& oldPath,
															  const std::filesystem::path& newPath)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		Core::Result<SideText> oldSide = ReadBlobSide(*repository, oldPath, ends.MergeBase);

		if (!oldSide.HasValue())
		{
			return oldSide.GetError();
		}

		Core::Result<SideText> newSide = ReadBlobSide(*repository, newPath, ends.Tip);

		if (!newSide.HasValue())
		{
			return newSide.GetError();
		}

		return TextsOf(std::move(oldSide).GetValue(), std::move(newSide).GetValue());
	}

	Core::Result<Core::Diff> RepositoryGitPort::QueryWorktreeDiff(const std::filesystem::path& worktree,
																  Core::WhitespaceInDiff whitespace)
	{
		// WARNING: Not cached per thread, because a cached handle would hold an index the worktree's
		// own git has since rewritten. Opened exactly, because a folder that lost its .git file
		// would otherwise be read as the repository around it.
		Core::Result<Core::Repository> opened = Core::Repository::OpenExactly(worktree);

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		const Core::Repository repository = std::move(opened).GetValue();

		Core::DiffRequest request;
		request.Target = Core::DiffTarget::WorkTreeToHead;
		request.Whitespace = whitespace;

		return repository.QueryDiff(request);
	}

	Core::Result<FileTexts> RepositoryGitPort::ReadWorktreeTexts(const std::filesystem::path& worktree,
																 const std::filesystem::path& oldPath,
																 const std::filesystem::path& newPath)
	{
		Core::Result<Core::Repository> opened = Core::Repository::OpenExactly(worktree);

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository repository = std::move(opened).GetValue();

		Core::Result<SideText> oldSide = ReadOneSide(repository, TextSide::Head, oldPath);

		if (!oldSide.HasValue())
		{
			return oldSide.GetError();
		}

		Core::Result<SideText> newSide = ReadOneSide(repository, TextSide::WorkingTree, newPath);

		if (!newSide.HasValue())
		{
			return newSide.GetError();
		}

		return TextsOf(std::move(oldSide).GetValue(), std::move(newSide).GetValue());
	}

	Core::Result<std::vector<Core::WorktreeEntry>> RepositoryGitPort::QueryWorktrees()
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		return opened.GetValue()->QueryWorktrees();
	}

	namespace {

		std::optional<std::chrono::sys_seconds> ModifiedAt(const std::filesystem::path& path)
		{
			std::error_code code;
			const std::filesystem::file_time_type written = std::filesystem::last_write_time(path, code);

			if (code)
			{
				return std::nullopt;
			}

			return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::clock_cast<std::chrono::system_clock>(written));
		}
	}

	Core::Result<WorktreeActivity> RepositoryGitPort::QueryActivity(const std::filesystem::path& worktree)
	{
		// WARNING: Opened exactly and not cached, for the reason QueryWorktreeDiff gives. The git directory is asked of
		// libgit2 because a linked worktree keeps its HEAD, index and reflog in <common>/worktrees/<name>.
		Core::Result<Core::Repository> opened = Core::Repository::OpenExactly(worktree);

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		const Core::Repository repository = std::move(opened).GetValue();
		const std::filesystem::path gitDirectory = repository.GitDirectory();

		WorktreeActivity activity;
		activity.Index = ModifiedAt(gitDirectory / "index");

		// WARNING: The newest entry's time, not logs/HEAD's modification time: git's reflog expiry rewrites the file of
		// every worktree at once without adding an entry, which would make all of them look active that second.
		const Core::Result<std::chrono::sys_seconds> reflogTime = repository.HeadReflogTime();

		if (reflogTime.HasValue())
		{
			activity.Reflog = reflogTime.GetValue();
		}
		else if (reflogTime.GetError().Code != Core::ErrorCode::NotFound)
		{
			return reflogTime.GetError();
		}

		const Core::Result<std::chrono::sys_seconds> headTime = repository.HeadCommitTime();

		if (headTime.HasValue())
		{
			activity.HeadCommit = headTime.GetValue();
		}
		else if (headTime.GetError().Code != Core::ErrorCode::NotFound)
		{
			// Only an unborn HEAD has no commit time; anything else is a failed read, not an absence.
			return headTime.GetError();
		}

		return activity;
	}

	Core::Result<WorktreeCount> RepositoryGitPort::CountUncommitted(const std::filesystem::path& worktree)
	{
		// WARNING: Not cached per thread, for the reason QueryWorktreeDiff gives. A wholly untracked folder
		// is one entry, as QueryStatus collapses it.
		Core::Result<Core::Repository> opened = Core::Repository::OpenExactly(worktree);

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		const Core::Repository repository = std::move(opened).GetValue();
		Core::Result<Core::WorkingTreeStatus> status = repository.QueryStatus(Core::SubmoduleWalk::Skipped);

		if (!status.HasValue())
		{
			return status.GetError();
		}

		WorktreeCount count;
		count.Changed = status.GetValue().Entries.size();

		for (const Core::FileStatus& entry : status.GetValue().Entries)
		{
			const std::optional<std::chrono::sys_seconds> written = ModifiedAt(worktree / entry.Path);

			if (written.has_value() && (!count.NewestChange.has_value() || *written > *count.NewestChange))
			{
				count.NewestChange = written;
			}
		}

		return count;
	}

	Core::Result<SinceViewedAnswer> RepositoryGitPort::ReadSinceViewed(const SinceViewedRequest& request)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		Core::Repository* repository = opened.GetValue();

		// WARNING: A linked worktree shares this repository's object database, so the viewed blob is
		// read here whichever side the current version comes from.
		Core::Result<std::string> viewed = repository->ReadBlobById(request.ViewedBlob);

		if (!viewed.HasValue())
		{
			return viewed.GetError();
		}

		Core::Result<std::string> current = ReadCurrentVersion(*repository, request);

		if (!current.HasValue())
		{
			return ReclassifyMissingCurrentVersionAsIoFailure(current.GetError());
		}

		Core::Result<Core::FileDiff> diff =
			Core::Repository::DiffTexts(viewed.GetValue(), current.GetValue(), request.Path, request.Whitespace);

		if (!diff.HasValue())
		{
			return diff.GetError();
		}

		SinceViewedAnswer answer;
		answer.Diff = std::move(diff).GetValue();
		answer.Diff.OldId = request.ViewedBlob;
		answer.Diff.NewId = request.CurrentBlob;
		answer.Texts.Old = std::move(viewed).GetValue();
		answer.Texts.HasOld = true;
		answer.Texts.New = std::move(current).GetValue();
		answer.Texts.HasNew = true;
		return answer;
	}

	std::uint64_t RepositoryGitPort::RepositoryNow()
	{
		const std::lock_guard<std::mutex> lock{m_RootMutex};
		return m_Generation;
	}

	Core::Result<LineRead> RepositoryGitPort::ReadLines(std::uint64_t repository, const LineReadInput& input)
	{
		// The handle is of the root that generation named, so the store resolved from it is the snapshot's repository's
		// even when the folder changes while the read runs.
		Core::Result<Core::Repository*> opened = RepositoryForThisThreadOf(repository);

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		const Core::Repository& snapshotRepository = *opened.GetValue();

		return Graphite::ReadLines(snapshotRepository, LineStoreFor(snapshotRepository.CommonDirectory(), m_LineStoreRoot), input);
	}

	Core::Result<LineRead> RepositoryGitPort::RecomputeLines(const LineRead& last, const LineAnswerChange& change, std::int64_t now)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		const Core::Repository* repository = opened.GetValue();

		return Graphite::RecomputeLines(last, LineStoreFor(repository->CommonDirectory(), m_LineStoreRoot), change, now);
	}

	Core::Result<Core::ContentVerdict> RepositoryGitPort::CheckContent(std::uint64_t repository, const Core::Oid& branchTip,
																	   const Core::Oid& lineTip, Core::PatchIdCache& patchIds)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThreadOf(repository, Core::ObjectWrites::InMemory);

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		return Core::CheckMergedByContent(*opened.GetValue(), branchTip, lineTip, patchIds);
	}

	Core::Result<Core::PatchTwinSearch> RepositoryGitPort::FindTwin(std::uint64_t repository, const Core::Oid& commit,
																	const Core::Oid& lineTip, std::size_t budget,
																	Core::PatchIdCache& patchIds)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThreadOf(repository, Core::ObjectWrites::InMemory);

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		return opened.GetValue()->FindPatchTwin(commit, lineTip, budget, patchIds);
	}

	// On the handle the checks use, which the worker holds already; trees are only read.
	Core::Result<std::vector<std::string>> RepositoryGitPort::PathsChangedBetween(std::uint64_t repository, const Core::Oid& older,
																				  const Core::Oid& newer)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThreadOf(repository, Core::ObjectWrites::InMemory);

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		return opened.GetValue()->PathsChangedBetween(older, newer);
	}

	Core::Result<bool> RepositoryGitPort::TreesDifferAt(std::uint64_t repository, const Core::Oid& from, const Core::Oid& to,
														const std::vector<std::string>& paths)
	{
		Core::Result<Core::Repository*> opened = RepositoryForThisThreadOf(repository, Core::ObjectWrites::InMemory);

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		return opened.GetValue()->TreesDifferAt(from, to, paths);
	}

	NetworkSetup RepositoryGitPort::QueryNetworkSetup()
	{
		NetworkSetup setup;
		const Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return setup;
		}

		const Core::Repository* repository = opened.GetValue();
		setup.Git = Core::FindGit();
		setup.WorkingDirectory = repository->GetWorkingDirectory();
		setup.CommonGitDir = repository->CommonDirectory();

		return setup;
	}

	std::optional<std::chrono::sys_seconds> RepositoryGitPort::QueryLastFetch()
	{
		const Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return std::nullopt;
		}

		return Core::LastFetchTime(opened.GetValue()->CommonDirectory());
	}

	ShallowStamp RepositoryGitPort::QueryShallowStamp()
	{
		const Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return ShallowStamp{};
		}

		return ShallowStampOf(ShallowFileOf(*opened.GetValue()));
	}

	Core::NetworkOutcome RepositoryGitPort::WithNetworkRun(const std::atomic<bool>& cancelled,
														   const std::function<Core::NetworkOutcome(const Core::NetworkRun&)>& operation)
	{
		const Core::Result<Core::Repository*> opened = RepositoryForThisThread();

		if (!opened.HasValue())
		{
			return NetworkFailure("The folder is not a git repository.");
		}

		const Core::Repository* repository = opened.GetValue();

		if (repository->GetWorkingDirectory().empty())
		{
			return NetworkFailure("This repository has no work tree.");
		}

		const std::optional<std::filesystem::path> git = Core::FindGit();

		if (!git.has_value())
		{
			return NetworkFailure("git was not found");
		}

		// WARNING: Asked of git, not read through libgit2, which cannot follow every include git does: an ssh command it
		// missed would be replaced by our batch-mode one, and the user's key with it.
		const Core::NetworkRun run{
			.Git = *git,
			.WorkingDirectory = repository->GetWorkingDirectory(),
			.SshCommandConfigured = Core::IsSshCommandConfigured(*git, repository->GetWorkingDirectory(), cancelled)};

		return operation(run);
	}

	Core::NetworkOutcome RepositoryGitPort::FetchAll(const std::atomic<bool>& cancelled, const Core::ProgressSink& progress)
	{
		return WithNetworkRun(cancelled, [&](const Core::NetworkRun& run) { return Core::FetchAll(run, cancelled, progress); });
	}

	Core::NetworkOutcome RepositoryGitPort::FetchRemote(const std::atomic<bool>& cancelled, const Core::ProgressSink& progress,
														const std::string& remote)
	{
		return WithNetworkRun(cancelled, [&](const Core::NetworkRun& run) { return Core::FetchRemote(run, cancelled, progress, remote); });
	}

	Core::NetworkOutcome RepositoryGitPort::FetchFullHistory(const std::atomic<bool>& cancelled, const Core::ProgressSink& progress)
	{
		return WithNetworkRun(cancelled, [&](const Core::NetworkRun& run) { return Core::FetchFullHistory(run, cancelled, progress); });
	}

	Core::NetworkOutcome RepositoryGitPort::FastForward(const std::atomic<bool>& cancelled, const Core::ProgressSink& progress,
														const std::string& upstreamFullName)
	{
		return WithNetworkRun(cancelled, [&](const Core::NetworkRun& run) { return Core::FastForward(run, cancelled, progress, upstreamFullName); });
	}

	Core::NetworkOutcome RepositoryGitPort::PushTo(const std::atomic<bool>& cancelled, const Core::ProgressSink& progress,
												   const std::string& remote, const std::optional<std::string>& upstreamBranch)
	{
		return WithNetworkRun(cancelled, [&](const Core::NetworkRun& run) { return Core::PushTo(run, cancelled, progress, remote, upstreamBranch); });
	}

	Core::NetworkOutcome RepositoryGitPort::ForcePushTo(const std::atomic<bool>& cancelled, const Core::ProgressSink& progress,
														const std::string& remote, const std::string& upstreamBranch)
	{
		return WithNetworkRun(cancelled, [&](const Core::NetworkRun& run) {
			return Core::ForcePushWithLease(run, cancelled, progress, remote, upstreamBranch);
		});
	}

	Core::NetworkOutcome RepositoryGitPort::SetRemoteHeadAuto(const std::atomic<bool>& cancelled, const Core::ProgressSink& progress,
															  const std::string& remote)
	{
		return WithNetworkRun(cancelled, [&](const Core::NetworkRun& run) { return Core::SetRemoteHeadAuto(run, cancelled, progress, remote); });
	}
}
