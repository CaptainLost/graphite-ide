#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>
#include <git2/sys/mempack.h>

#include <format>
#include <utility>

namespace Core {

	namespace {

		// Higher than the backends the repository opens with, so every write lands in memory.
		constexpr int kMempackPriority = 1000;

		bool IsMissingObject(int result)
		{
			if (result == GIT_ENOTFOUND)
			{
				return true;
			}

			const git_error* const lastError = git_error_last();
			return lastError != nullptr && lastError->klass == GIT_ERROR_ODB;
		}
	}

	Result<void> Repository::AttachMemoryBackend()
	{
		git_odb* database = nullptr;

		if (git_repository_odb(&database, m_Handle.get()) != 0)
		{
			return Error{ErrorCode::IoFailure, "cannot open the object database: " + LastLibgit2Message()};
		}

		git_odb_backend* backend = nullptr;

		if (git_mempack_new(&backend) != 0)
		{
			git_odb_free(database);
			return Error{ErrorCode::IoFailure, "cannot create an in-memory object store: " + LastLibgit2Message()};
		}

		// The database owns the backend from here on, also when adding fails.
		const int added = git_odb_add_backend(database, backend, kMempackPriority);
		git_odb_free(database);

		if (added != 0)
		{
			return Error{ErrorCode::IoFailure, "cannot add an in-memory object store: " + LastLibgit2Message()};
		}

		m_MemoryBackend = backend;
		return Result<void>{};
	}

	void Repository::DropObjectsInMemory() const
	{
		if (m_MemoryBackend != nullptr)
		{
			git_mempack_reset(m_MemoryBackend);
		}
	}

	Result<ContentAnswer> Repository::MergeChangesNothing(const Oid& lineTip, const Oid& branchTip) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		if (m_MemoryBackend == nullptr)
		{
			return Error{ErrorCode::InvalidArgument, "the repository was not opened to keep its objects in memory"};
		}

		Result<CommitHandle> line = LookupCommit(m_Handle.get(), lineTip);
		Result<CommitHandle> branch = LookupCommit(m_Handle.get(), branchTip);

		if (!line.HasValue() || !branch.HasValue())
		{
			return ContentAnswer::Unknown;
		}

		git_merge_options options = GIT_MERGE_OPTIONS_INIT;
		options.flags = GIT_MERGE_FAIL_ON_CONFLICT | GIT_MERGE_SKIP_REUC;

		IndexHandle merged;
		const int mergeResult =
			git_merge_commits(Out(merged), m_Handle.get(), line.GetValue().get(), branch.GetValue().get(), &options);

		ContentAnswer answer = ContentAnswer::No;
		bool failed = false;

		if (mergeResult == GIT_EMERGECONFLICT)
		{
			answer = ContentAnswer::No;
		}
		else if (mergeResult != 0)
		{
			if (IsMissingObject(mergeResult))
			{
				answer = ContentAnswer::Unknown;
			}
			else
			{
				failed = true;
			}
		}
		else if (git_index_has_conflicts(merged.get()) != 0)
		{
			answer = ContentAnswer::No;
		}
		else
		{
			git_oid mergedTree{};
			const int writeResult = git_index_write_tree_to(&mergedTree, merged.get(), m_Handle.get());

			if (writeResult == 0)
			{
				const bool sameTree = git_oid_equal(&mergedTree, git_commit_tree_id(line.GetValue().get())) != 0;
				answer = sameTree ? ContentAnswer::Yes : ContentAnswer::No;
			}
			else if (IsMissingObject(writeResult))
			{
				answer = ContentAnswer::Unknown;
			}
			else
			{
				failed = true;
			}
		}

		const std::string failureMessage = failed ? LastLibgit2Message() : std::string{};
		DropObjectsInMemory();

		if (failed)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot merge in memory: {}", failureMessage)};
		}

		return answer;
	}
}
