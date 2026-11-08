#pragma once

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"
#include "Core/Git/Requests.h"

#include "TempDirectory.h"

#include <doctest/doctest.h>

#include <git2.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

inline std::string HexTextOf(const git_oid& id)
{
	char hexText[GIT_OID_SHA1_HEXSIZE + 1] = {};
	git_oid_fmt(hexText, &id);
	return std::string(hexText, GIT_OID_SHA1_HEXSIZE);
}

inline std::string BlobHexOf(const std::string& contents)
{
	git_oid id{};
	REQUIRE(git_odb_hash(&id, contents.data(), contents.size(), GIT_OBJECT_BLOB) == 0);
	return HexTextOf(id);
}

class GitFixture
{
public:
	static constexpr const char* kDefaultAuthorName = "Test Author";
	static constexpr const char* kDefaultAuthorEmail = "test@example.com";

	GitFixture()
	{
		git_libgit2_init();

		// Pinned because the ambient init.defaultBranch varies from machine to machine, and so
		// would every test that checks the initial branch out.
		git_repository_init_options options = GIT_REPOSITORY_INIT_OPTIONS_INIT;
		options.initial_head = "master";

		REQUIRE(git_repository_init_ext(&m_Repository, Core::ToUtf8(m_Directory.GetPath()).c_str(), &options) == 0);
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

	const std::filesystem::path& GetPath() const { return m_Directory.GetPath(); }

	void Write(const std::string& relativePath, const std::string& contents)
	{
		m_Directory.MakeFile(relativePath, contents);
	}

	void Remove(const std::string& relativePath)
	{
		std::error_code ignoredRemoveError;
		std::filesystem::remove(m_Directory.GetPath() / relativePath, ignoredRemoveError);
	}

	void Rename(const std::string& oldRelativePath, const std::string& newRelativePath)
	{
		std::error_code moveError;
		std::filesystem::create_directories((m_Directory.GetPath() / newRelativePath).parent_path(), moveError);
		std::filesystem::rename(m_Directory.GetPath() / oldRelativePath, m_Directory.GetPath() / newRelativePath, moveError);
		REQUIRE_FALSE(moveError);
	}

	// Not set in the constructor: a repository without an identity is a real state a user
	// can be in, and one test asserts what happens there.
	void SetIdentity(const std::string& name, const std::string& email)
	{
		git_config* config = nullptr;
		REQUIRE(git_repository_config(&config, m_Repository) == 0);
		REQUIRE(git_config_set_string(config, "user.name", name.c_str()) == 0);
		REQUIRE(git_config_set_string(config, "user.email", email.c_str()) == 0);
		git_config_free(config);
	}

	// Without this the "cannot resolve a signature" case would depend on the identity in the
	// developer's global git config, which the repository's own config shadows.
	void SetUnusableIdentity()
	{
		git_config* config = nullptr;
		REQUIRE(git_repository_config(&config, m_Repository) == 0);
		REQUIRE(git_config_set_string(config, "user.name", "") == 0);
		REQUIRE(git_config_set_string(config, "user.email", "") == 0);
		git_config_free(config);
	}

	std::string Commit(const std::string& message)
	{
		return CommitAs(message, kDefaultAuthorName, kDefaultAuthorEmail);
	}

	std::string CommitAs(const std::string& message, const std::string& authorName, const std::string& authorEmail)
	{
		return CommitWithSignatureTime(message, authorName, authorEmail, NextCommitTime());
	}

	// Like CommitAs, but author and committer time are day `day` after 2026-01-01T00:00Z, so a test can spread history over days.
	std::string CommitAt(const std::string& message, const std::string& authorEmail, int day)
	{
		const git_time_t kFirstDayStart = 1767225600;
		const git_time_t kSecondsPerDay = 86400;
		return CommitWithSignatureTime(message, kDefaultAuthorName, authorEmail, kFirstDayStart + kSecondsPerDay * day);
	}

	// Like CommitAt, but put on the branch by someone else: a hosting service merging a pull request, a maintainer
	// picking it.
	std::string CommitAtCommittedBy(const std::string& message, const std::string& authorEmail, const std::string& committerEmail, int day)
	{
		const git_time_t kFirstDayStart = 1767225600;
		const git_time_t kSecondsPerDay = 86400;
		return CommitWithSignatures(message, kDefaultAuthorName, authorEmail, committerEmail, kFirstDayStart + kSecondsPerDay * day);
	}

	void Branch(const std::string& name)
	{
		git_oid headId{};
		REQUIRE(git_reference_name_to_id(&headId, m_Repository, "HEAD") == 0);

		git_commit* target = nullptr;
		REQUIRE(git_commit_lookup(&target, m_Repository, &headId) == 0);

		const int overwriteExisting = 0;
		git_reference* createdBranch = nullptr;
		REQUIRE(git_branch_create(&createdBranch, m_Repository, name.c_str(), target, overwriteExisting) == 0);

		git_reference_free(createdBranch);
		git_commit_free(target);
	}

	void TagLightweight(const std::string& name, const std::string& commitHex)
	{
		TagObject(name, commitHex);
	}

	// Writes the tag object and refs/tags/<name> naming it; returns the tag object's hex. The target may itself be a
	// tag object, which makes a tag of a tag.
	std::string TagAnnotated(const std::string& name, const std::string& targetHex, std::int64_t taggerTime, const std::string& message)
	{
		git_oid targetId{};
		REQUIRE(git_oid_fromstr(&targetId, targetHex.c_str()) == 0);

		git_object* target = nullptr;
		REQUIRE(git_object_lookup(&target, m_Repository, &targetId, GIT_OBJECT_ANY) == 0);

		git_signature* tagger = MakeSignature(kDefaultAuthorName, kDefaultAuthorEmail, static_cast<git_time_t>(taggerTime));

		git_oid tagId{};
		REQUIRE(git_tag_annotation_create(&tagId, m_Repository, name.c_str(), target, tagger, message.c_str()) == 0);

		git_signature_free(tagger);
		git_object_free(target);

		const std::string tagHex = HexTextOf(tagId);
		TagObject(name, tagHex);
		return tagHex;
	}

	// refs/tags/<name> naming the object directly: a lightweight tag when it is a commit, and a tag of a tree or a
	// blob when it is one of those.
	void TagObject(const std::string& name, const std::string& objectHex)
	{
		git_oid objectId{};
		REQUIRE(git_oid_fromstr(&objectId, objectHex.c_str()) == 0);

		const std::string reference = "refs/tags/" + name;

		const int overwriteExisting = 1;
		const char* noReflogMessage = nullptr;
		git_reference* createdReference = nullptr;
		REQUIRE(git_reference_create(&createdReference, m_Repository, reference.c_str(), &objectId, overwriteExisting,
									 noReflogMessage) == 0);
		git_reference_free(createdReference);
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

	void Detach()
	{
		git_oid headCommitId{};
		REQUIRE(git_reference_name_to_id(&headCommitId, m_Repository, "HEAD") == 0);
		REQUIRE(git_repository_set_head_detached(m_Repository, &headCommitId) == 0);
	}

	// libgit2 attaches HEAD to a branch that does not exist yet without complaint, so the next
	// Commit has no parent and starts a history of its own.
	void StartOrphan(const std::string& name)
	{
		const std::string reference = "refs/heads/" + name;
		REQUIRE(git_repository_set_head(m_Repository, reference.c_str()) == 0);
	}

	// Records two parents over the tree of the current HEAD rather than merging the two trees:
	// the log and the graph are tested against topology, and a real three-way merge would add
	// failure modes that belong to the branch-operations tests.
	std::string MergeCommit(const std::string& message, const std::string& otherBranch)
	{
		const git_oid treeId = StageEverything();

		git_tree* tree = nullptr;
		REQUIRE(git_tree_lookup(&tree, m_Repository, &treeId) == 0);

		git_oid headCommitId{};
		REQUIRE(git_reference_name_to_id(&headCommitId, m_Repository, "HEAD") == 0);

		git_oid otherCommitId{};
		const std::string reference = "refs/heads/" + otherBranch;
		REQUIRE(git_reference_name_to_id(&otherCommitId, m_Repository, reference.c_str()) == 0);

		git_commit* headCommit = nullptr;
		git_commit* otherCommit = nullptr;
		REQUIRE(git_commit_lookup(&headCommit, m_Repository, &headCommitId) == 0);
		REQUIRE(git_commit_lookup(&otherCommit, m_Repository, &otherCommitId) == 0);

		const git_commit* parents[2] = {headCommit, otherCommit};
		const std::size_t parentCount = 2;

		git_signature* signature = MakeSignature(kDefaultAuthorName, kDefaultAuthorEmail);

		git_oid commitId{};
		REQUIRE(git_commit_create(&commitId, m_Repository, "HEAD", signature, signature, "UTF-8",
								  message.c_str(), tree, parentCount, parents) == 0);

		git_signature_free(signature);
		git_commit_free(otherCommit);
		git_commit_free(headCommit);
		git_tree_free(tree);

		return HexTextOf(commitId);
	}

	// Leaves refs/remotes/origin/<branch> and the local branch's tracking config as a fetch
	// would. git_branch_upstream maps branch.<name>.merge onto refs/remotes/* only through a
	// remote with a fetch refspec, so "origin" is created, pointing at the fixture itself and
	// never contacted; without it the branch reads as having no upstream at all.
	void PushTo(const std::string& branch, const std::string& commit)
	{
		git_remote* remote = nullptr;

		if (git_remote_lookup(&remote, m_Repository, "origin") != 0)
		{
			REQUIRE(git_remote_create(&remote, m_Repository, "origin",
									  Core::ToUtf8(m_Directory.GetPath()).c_str()) == 0);
		}

		git_remote_free(remote);

		git_oid commitId{};
		REQUIRE(git_oid_fromstr(&commitId, commit.c_str()) == 0);

		const std::string reference = "refs/remotes/origin/" + branch;

		const int overwriteExisting = 1;
		const char* noReflogMessage = nullptr;
		git_reference* createdReference = nullptr;
		REQUIRE(git_reference_create(&createdReference, m_Repository, reference.c_str(), &commitId,
									 overwriteExisting, noReflogMessage) == 0);
		git_reference_free(createdReference);

		git_config* config = nullptr;
		REQUIRE(git_repository_config(&config, m_Repository) == 0);

		const std::string remoteKey = "branch." + branch + ".remote";
		const std::string mergeKey = "branch." + branch + ".merge";

		REQUIRE(git_config_set_string(config, remoteKey.c_str(), "origin") == 0);
		REQUIRE(git_config_set_string(config, mergeKey.c_str(),
									  ("refs/heads/" + branch).c_str()) == 0);

		git_config_free(config);
	}

	// Only a clone writes refs/remotes/origin/HEAD; a fetch or a push never does.
	void SetRemoteHead(const std::string& branch)
	{
		const std::string target = "refs/remotes/origin/" + branch;

		const int overwriteExisting = 1;
		const char* noReflogMessage = nullptr;
		git_reference* createdReference = nullptr;
		REQUIRE(git_reference_symbolic_create(&createdReference, m_Repository, "refs/remotes/origin/HEAD",
											  target.c_str(), overwriteExisting, noReflogMessage) == 0);
		git_reference_free(createdReference);
	}

	// Records a shallow boundary the way a depth-limited fetch does: the commit's hex id on a line of its own in
	// <git dir>/shallow.
	void MarkShallow(const std::string& commit)
	{
		const std::filesystem::path shallowFile = std::filesystem::path(git_repository_path(m_Repository)) / "shallow";
		std::ofstream file{shallowFile, std::ios::binary | std::ios::app};
		REQUIRE(file.is_open());
		file << commit << '\n';
	}

	void AddRemote(const std::string& name)
	{
		git_remote* remote = nullptr;
		REQUIRE(git_remote_create(&remote, m_Repository, name.c_str(), Core::ToUtf8(m_Directory.GetPath()).c_str()) == 0);
		git_remote_free(remote);
	}

	// A bare repository of its own, which lives as long as the fixture, added as the remote `name`; returns its path. It
	// is a path, never a url, so nothing is contacted.
	std::filesystem::path MakeBareRemote(const std::string& name)
	{
		m_BareRemotes.push_back(std::make_unique<TempDirectory>());
		const std::filesystem::path barePath = m_BareRemotes.back()->GetPath();

		git_repository_init_options options = GIT_REPOSITORY_INIT_OPTIONS_INIT;
		options.flags = GIT_REPOSITORY_INIT_BARE | GIT_REPOSITORY_INIT_MKPATH;
		options.initial_head = "master";

		git_repository* bare = nullptr;
		REQUIRE(git_repository_init_ext(&bare, Core::ToUtf8(barePath).c_str(), &options) == 0);
		git_repository_free(bare);

		AddRemoteWithUrl(name, Core::ToUtf8(barePath));
		return barePath;
	}

	void AddRemoteWithUrl(const std::string& name, const std::string& url)
	{
		git_remote* remote = nullptr;
		REQUIRE(git_remote_create(&remote, m_Repository, name.c_str(), url.c_str()) == 0);
		git_remote_free(remote);
	}

	// A remote that only has a push url: it is listed, and has no fetch url.
	void AddRemoteWithPushUrlOnly(const std::string& name, const std::string& pushUrl)
	{
		git_config* config = nullptr;
		REQUIRE(git_repository_config(&config, m_Repository) == 0);
		REQUIRE(git_config_set_string(config, ("remote." + name + ".pushurl").c_str(), pushUrl.c_str()) == 0);
		git_config_free(config);
	}

	// Writes only the tracking config: the remote branch it names may or may not exist.
	void SetUpstream(const std::string& branch, const std::string& remote, const std::string& remoteBranch)
	{
		git_config* config = nullptr;
		REQUIRE(git_repository_config(&config, m_Repository) == 0);
		REQUIRE(git_config_set_string(config, ("branch." + branch + ".remote").c_str(), remote.c_str()) == 0);
		REQUIRE(git_config_set_string(config, ("branch." + branch + ".merge").c_str(),
									  ("refs/heads/" + remoteBranch).c_str()) == 0);
		git_config_free(config);
	}

	void SetRemoteBranch(const std::string& remote, const std::string& branch, const std::string& commit)
	{
		git_oid commitId{};
		REQUIRE(git_oid_fromstr(&commitId, commit.c_str()) == 0);

		const std::string reference = "refs/remotes/" + remote + "/" + branch;

		const int overwriteExisting = 1;
		const char* noReflogMessage = nullptr;
		git_reference* createdReference = nullptr;
		REQUIRE(git_reference_create(&createdReference, m_Repository, reference.c_str(), &commitId, overwriteExisting,
									 noReflogMessage) == 0);
		git_reference_free(createdReference);
	}

	void StartConflict(const std::string& branchChangingTheSameFile)
	{
		const std::string reference = "refs/heads/" + branchChangingTheSameFile;

		git_reference* branch = nullptr;
		REQUIRE(git_reference_lookup(&branch, m_Repository, reference.c_str()) == 0);

		git_annotated_commit* theirCommit = nullptr;
		REQUIRE(git_annotated_commit_from_ref(&theirCommit, m_Repository, branch) == 0);

		git_merge_options mergeOptions = GIT_MERGE_OPTIONS_INIT;
		git_checkout_options checkoutOptions = GIT_CHECKOUT_OPTIONS_INIT;
		checkoutOptions.checkout_strategy = GIT_CHECKOUT_FORCE;

		const git_annotated_commit* mergeHeads[1] = {theirCommit};
		const std::size_t mergeHeadCount = 1;
		REQUIRE(git_merge(m_Repository, mergeHeads, mergeHeadCount, &mergeOptions, &checkoutOptions) == 0);

		git_annotated_commit_free(theirCommit);
		git_reference_free(branch);
	}

	// Deletes the loose object file, as a clone that left the object out never had it.
	void RemoveObject(const std::string& hex)
	{
		const std::filesystem::path file = m_Directory.GetPath() / ".git" / "objects" / hex.substr(0, 2) / hex.substr(2);
		REQUIRE(std::filesystem::exists(file));

		std::error_code removeError;
		std::filesystem::permissions(file, std::filesystem::perms::owner_write, std::filesystem::perm_options::add, removeError);
		REQUIRE(std::filesystem::remove(file, removeError));
	}

	// Every file under .git/objects, packs included.
	std::size_t ObjectFileCount() const
	{
		std::size_t count = 0;
		std::error_code listError;

		for (const std::filesystem::directory_entry& entry :
			 std::filesystem::recursive_directory_iterator(m_Directory.GetPath() / ".git" / "objects", listError))
		{
			if (entry.is_regular_file())
			{
				++count;
			}
		}

		return count;
	}

private:
	std::string CommitWithSignatureTime(const std::string& message, const std::string& authorName, const std::string& authorEmail, git_time_t commitTime)
	{
		return CommitWithSignatures(message, authorName, authorEmail, authorEmail, commitTime);
	}

	std::string CommitWithSignatures(const std::string& message, const std::string& authorName, const std::string& authorEmail,
									 const std::string& committerEmail, git_time_t commitTime)
	{
		git_oid treeId = StageEverything();

		git_tree* tree = nullptr;
		REQUIRE(git_tree_lookup(&tree, m_Repository, &treeId) == 0);

		git_signature* author = MakeSignature(authorName, authorEmail, commitTime);
		git_signature* committer = MakeSignature(authorName, committerEmail, commitTime);

		git_oid parentId{};
		const bool hasParent = git_reference_name_to_id(&parentId, m_Repository, "HEAD") == 0;

		git_commit* parent = nullptr;
		if (hasParent)
		{
			REQUIRE(git_commit_lookup(&parent, m_Repository, &parentId) == 0);
		}

		const git_commit* parents[1] = {parent};
		const std::size_t parentCount = hasParent ? 1u : 0u;
		const git_commit** parentsOrNone = hasParent ? parents : nullptr;

		git_oid commitId{};
		REQUIRE(git_commit_create(&commitId, m_Repository, "HEAD", author, committer, "UTF-8",
								  message.c_str(), tree, parentCount, parentsOrNone) == 0);

		if (parent != nullptr)
		{
			git_commit_free(parent);
		}

		git_signature_free(committer);
		git_signature_free(author);
		git_tree_free(tree);

		return HexTextOf(commitId);
	}

	// git_index_add_all picks up only new and modified paths; git_index_update_all is what
	// notices a deletion.
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

	// A clock that moves one minute per commit, rather than the real time, keeps the commit order and the log a
	// failing test prints the same on every run.
	git_time_t NextCommitTime()
	{
		const git_time_t kFirstCommitTime = 1000000000;
		const git_time_t kSecondsBetweenCommits = 60;

		const git_time_t commitTime = kFirstCommitTime + kSecondsBetweenCommits * m_CommitCount;
		++m_CommitCount;
		return commitTime;
	}

	// The caller names the author.
	git_signature* MakeSignature(const std::string& authorName, const std::string& authorEmail)
	{
		return MakeSignature(authorName, authorEmail, NextCommitTime());
	}

	git_signature* MakeSignature(const std::string& authorName, const std::string& authorEmail, git_time_t commitTime)
	{
		const int kUtcOffsetMinutes = 0;

		git_signature* signature = nullptr;
		REQUIRE(git_signature_new(&signature, authorName.c_str(), authorEmail.c_str(), commitTime, kUtcOffsetMinutes) == 0);
		return signature;
	}

	TempDirectory m_Directory;
	std::vector<std::unique_ptr<TempDirectory>> m_BareRemotes;
	git_repository* m_Repository = nullptr;
	int m_CommitCount = 0;
};

inline Core::Repository OpenRepositoryAt(const std::filesystem::path& root)
{
	Core::Result<Core::Repository> opened = Core::Repository::Open(root);
	REQUIRE(opened.HasValue());
	return std::move(opened).GetValue();
}

inline Core::Oid HeadCommitId(const Core::Repository& repository)
{
	const Core::Result<Core::LogPage> log = repository.QueryLog(Core::LogRequest{});
	REQUIRE(log.HasValue());
	REQUIRE_FALSE(log.GetValue().Commits.empty());
	return log.GetValue().Commits.front().Id;
}

// core.autocrlf may check a file out with CRLF line ends; the tests compare content.
inline std::string ReadWithoutCarriageReturns(const std::filesystem::path& path)
{
	std::ifstream file{path, std::ios::binary};
	std::stringstream contentBuffer;
	contentBuffer << file.rdbuf();
	std::string content = contentBuffer.str();
	std::erase(content, '\r');
	return content;
}
