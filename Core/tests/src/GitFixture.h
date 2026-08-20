#pragma once

#include "Core/Fs/FileSystem.h"

#include "TempDirectory.h"

#include <doctest/doctest.h>

#include <git2.h>

#include <filesystem>
#include <string>
#include <system_error>

// Builds a real repository in a temporary directory. Every commit carries the same author
// and a timestamp one minute later than the last, so ordering is deterministic and a log
// printed by a failing test reads the same on every machine.
//
// MergeCommit is deliberately synthetic: it records two parents over the tree of the
// current HEAD rather than merging the two trees. Topology is what the log and the graph
// are tested against; a real three-way merge would add failure modes that belong to the
// branch-operations work, not here.
class GitFixture
{
public:
	GitFixture()
	{
		git_libgit2_init();

		REQUIRE(git_repository_init(&m_Repository, Core::ToUtf8(m_Temp.GetPath()).c_str(), 0) == 0);
	}

	~GitFixture()
	{
		if (m_Repository != nullptr)
		{
			git_repository_free(m_Repository);
		}
	}

	GitFixture(const GitFixture&) = delete;
	GitFixture& operator=(const GitFixture&) = delete;

	const std::filesystem::path& GetPath() const { return m_Temp.GetPath(); }

	void Write(const std::string& relative, const std::string& contents)
	{
		m_Temp.MakeFile(relative, contents);
	}

	void Remove(const std::string& relative)
	{
		std::error_code code;
		std::filesystem::remove(m_Temp.GetPath() / relative, code);
	}

	void Rename(const std::string& from, const std::string& to)
	{
		std::error_code code;
		std::filesystem::create_directories((m_Temp.GetPath() / to).parent_path(), code);
		std::filesystem::rename(m_Temp.GetPath() / from, m_Temp.GetPath() / to, code);
		REQUIRE_FALSE(code);
	}

	std::string Commit(const std::string& message)
	{
		git_oid treeId = StageEverything();

		git_tree* tree = nullptr;
		REQUIRE(git_tree_lookup(&tree, m_Repository, &treeId) == 0);

		git_signature* who = MakeSignature();

		git_oid parentId{};
		const bool hasParent = git_reference_name_to_id(&parentId, m_Repository, "HEAD") == 0;

		git_commit* parent = nullptr;
		if (hasParent)
		{
			REQUIRE(git_commit_lookup(&parent, m_Repository, &parentId) == 0);
		}

		const git_commit* parents[1] = {parent};

		git_oid commitId{};
		REQUIRE(git_commit_create(&commitId, m_Repository, "HEAD", who, who, "UTF-8",
								  message.c_str(), tree, hasParent ? 1u : 0u,
								  hasParent ? parents : nullptr) == 0);

		if (parent != nullptr)
		{
			git_commit_free(parent);
		}

		git_signature_free(who);
		git_tree_free(tree);

		return ToHex(commitId);
	}

	void Branch(const std::string& name)
	{
		git_oid headId{};
		REQUIRE(git_reference_name_to_id(&headId, m_Repository, "HEAD") == 0);

		git_commit* target = nullptr;
		REQUIRE(git_commit_lookup(&target, m_Repository, &headId) == 0);

		git_reference* created = nullptr;
		REQUIRE(git_branch_create(&created, m_Repository, name.c_str(), target, 0) == 0);

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

	std::string MergeCommit(const std::string& message, const std::string& otherBranch)
	{
		const git_oid treeId = StageEverything();

		git_tree* tree = nullptr;
		REQUIRE(git_tree_lookup(&tree, m_Repository, &treeId) == 0);

		git_oid firstId{};
		REQUIRE(git_reference_name_to_id(&firstId, m_Repository, "HEAD") == 0);

		git_oid secondId{};
		const std::string reference = "refs/heads/" + otherBranch;
		REQUIRE(git_reference_name_to_id(&secondId, m_Repository, reference.c_str()) == 0);

		git_commit* first = nullptr;
		git_commit* second = nullptr;
		REQUIRE(git_commit_lookup(&first, m_Repository, &firstId) == 0);
		REQUIRE(git_commit_lookup(&second, m_Repository, &secondId) == 0);

		const git_commit* parents[2] = {first, second};

		git_signature* who = MakeSignature();

		git_oid commitId{};
		REQUIRE(git_commit_create(&commitId, m_Repository, "HEAD", who, who, "UTF-8",
								  message.c_str(), tree, 2, parents) == 0);

		git_signature_free(who);
		git_commit_free(second);
		git_commit_free(first);
		git_tree_free(tree);

		return ToHex(commitId);
	}

private:
	// add_all picks up new and modified paths; update_all is what notices a deletion, so
	// a test that removes a file and commits records the removal.
	git_oid StageEverything()
	{
		git_index* index = nullptr;
		REQUIRE(git_repository_index(&index, m_Repository) == 0);
		REQUIRE(git_index_add_all(index, nullptr, GIT_INDEX_ADD_DEFAULT, nullptr, nullptr) == 0);
		REQUIRE(git_index_update_all(index, nullptr, nullptr, nullptr) == 0);
		REQUIRE(git_index_write(index) == 0);

		git_oid treeId{};
		REQUIRE(git_index_write_tree(&treeId, index) == 0);

		git_index_free(index);
		return treeId;
	}

	git_signature* MakeSignature()
	{
		git_signature* who = nullptr;
		const git_time_t when = 1000000000 + 60 * m_CommitCount;
		++m_CommitCount;

		REQUIRE(git_signature_new(&who, "Test Author", "test@example.com", when, 0) == 0);
		return who;
	}

	static std::string ToHex(const git_oid& id)
	{
		char text[GIT_OID_SHA1_HEXSIZE + 1] = {};
		git_oid_fmt(text, &id);
		return std::string(text, GIT_OID_SHA1_HEXSIZE);
	}

	TempDirectory m_Temp;
	git_repository* m_Repository = nullptr;
	int m_CommitCount = 0;
};
