#pragma once

#include "TempDirectory.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Oid.h"

#include <doctest/doctest.h>

#include <git2.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

class ReviewRepository
{
public:
	ReviewRepository()
	{
		git_libgit2_init();

		git_repository_init_options options = GIT_REPOSITORY_INIT_OPTIONS_INIT;
		options.initial_head = "master";

		REQUIRE(git_repository_init_ext(&m_Repository, Core::ToUtf8(m_Directory.GetPath()).c_str(), &options) == 0);
	}

	~ReviewRepository()
	{
		git_repository_free(m_Repository);
	}

	ReviewRepository(const ReviewRepository&) = delete;
	ReviewRepository& operator=(const ReviewRepository&) = delete;

	const std::filesystem::path& GetPath() const { return m_Directory.GetPath(); }

	void Write(const std::string& relativePath, const std::string& contents)
	{
		WriteFileAt(m_Directory.GetPath() / relativePath, contents);
	}

	static void WriteFileAt(const std::filesystem::path& path, const std::string& contents)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file{path, std::ios::binary | std::ios::trunc};
		file << contents;
	}

	Core::Oid Commit(const std::string& message)
	{
		git_index* index = nullptr;
		REQUIRE(git_repository_index(&index, m_Repository) == 0);
		REQUIRE(git_index_add_all(index, nullptr, GIT_INDEX_ADD_DEFAULT, nullptr, nullptr) == 0);
		REQUIRE(git_index_update_all(index, nullptr, nullptr, nullptr) == 0);
		REQUIRE(git_index_write(index) == 0);

		git_oid treeId{};
		REQUIRE(git_index_write_tree(&treeId, index) == 0);
		git_index_free(index);

		git_tree* tree = nullptr;
		REQUIRE(git_tree_lookup(&tree, m_Repository, &treeId) == 0);

		const git_time_t kFirstCommitTime = 1000000000;
		const git_time_t kSecondsBetweenCommits = 60;
		const int kUtcOffsetMinutes = 0;
		git_signature* signature = nullptr;
		REQUIRE(git_signature_new(&signature, "Test Author", "test@example.com",
								  kFirstCommitTime + kSecondsBetweenCommits * m_CommitCount, kUtcOffsetMinutes) == 0);
		++m_CommitCount;

		git_oid parentId{};
		const bool hasParent = git_reference_name_to_id(&parentId, m_Repository, "HEAD") == 0;
		git_commit* parent = nullptr;

		if (hasParent)
		{
			REQUIRE(git_commit_lookup(&parent, m_Repository, &parentId) == 0);
		}

		const git_commit* parents[1] = {parent};
		const std::size_t parentCount = hasParent ? 1u : 0u;

		git_oid commitId{};
		REQUIRE(git_commit_create(&commitId, m_Repository, "HEAD", signature, signature, "UTF-8", message.c_str(), tree,
								  parentCount, hasParent ? parents : nullptr) == 0);

		git_commit_free(parent);
		git_signature_free(signature);
		git_tree_free(tree);

		Core::Oid committed;
		std::copy_n(commitId.id, Core::Oid::kByteCount, committed.Bytes.begin());
		return committed;
	}

	void Branch(const std::string& name)
	{
		git_oid headId{};
		REQUIRE(git_reference_name_to_id(&headId, m_Repository, "HEAD") == 0);

		git_commit* target = nullptr;
		REQUIRE(git_commit_lookup(&target, m_Repository, &headId) == 0);

		const int keepExisting = 0;
		git_reference* created = nullptr;
		REQUIRE(git_branch_create(&created, m_Repository, name.c_str(), target, keepExisting) == 0);

		git_reference_free(created);
		git_commit_free(target);
	}

	void Checkout(const std::string& name)
	{
		const std::string reference = "refs/heads/" + name;

		git_object* target = nullptr;
		REQUIRE(git_revparse_single(&target, m_Repository, reference.c_str()) == 0);

		git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
		options.checkout_strategy = GIT_CHECKOUT_FORCE;

		REQUIRE(git_checkout_tree(m_Repository, target, &options) == 0);
		REQUIRE(git_repository_set_head(m_Repository, reference.c_str()) == 0);

		git_object_free(target);
	}

	void StartOrphan(const std::string& name)
	{
		const std::string reference = "refs/heads/" + name;
		REQUIRE(git_repository_set_head(m_Repository, reference.c_str()) == 0);
	}

	std::filesystem::path AddWorktree(const std::string& branch)
	{
		const std::filesystem::path worktreePath = m_Outside.GetPath() / (branch + "-tree");

		git_reference* branchReference = nullptr;
		REQUIRE(git_branch_lookup(&branchReference, m_Repository, branch.c_str(), GIT_BRANCH_LOCAL) == 0);

		git_worktree_add_options options = GIT_WORKTREE_ADD_OPTIONS_INIT;
		options.ref = branchReference;

		git_worktree* worktree = nullptr;
		const std::string worktreeName = branch + "-tree";
		REQUIRE(git_worktree_add(&worktree, m_Repository, worktreeName.c_str(), Core::ToUtf8(worktreePath).c_str(),
								 &options) == 0);

		git_worktree_free(worktree);
		git_reference_free(branchReference);
		return worktreePath;
	}

private:
	TempDirectory m_Directory;
	TempDirectory m_Outside;
	git_repository* m_Repository = nullptr;
	int m_CommitCount = 0;
};
