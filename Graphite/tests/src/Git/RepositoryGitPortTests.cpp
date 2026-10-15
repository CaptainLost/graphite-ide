#include <doctest/doctest.h>

#include "Git/RepositoryGitPort.h"

#include "FileFixtures.h"
#include "TempDirectory.h"

#include "Core/FileSystem/FileSystem.h"

#include <git2.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <optional>
#include <process.h>
#include <string>
#include <system_error>
#include <vector>

namespace {

	class TempRepo
	{
	public:
		TempRepo()
		{
			static std::atomic<int> counter{0};
			const int directoryNumber = counter.fetch_add(1);

			// WARNING: The process id is part of the name. Two concurrent GraphiteTests processes
			// would otherwise share directory names and delete each other's repository.
			m_Path = std::filesystem::temp_directory_path() /
					 ("graphite-tests-repogitport-" + std::to_string(directoryNumber) + "-" +
					  std::to_string(::_getpid()));

			std::error_code code;
			std::filesystem::remove_all(m_Path, code);
			std::filesystem::create_directories(m_Path, code);

			git_libgit2_init();

			constexpr unsigned int kNotBare = 0;
			git_repository* repository = nullptr;
			REQUIRE(git_repository_init(&repository, Core::ToUtf8(m_Path).c_str(), kNotBare) == 0);
			git_repository_free(repository);
		}

		~TempRepo()
		{
			std::error_code code;
			std::filesystem::remove_all(m_Path, code);
		}

		TempRepo(const TempRepo&) = delete;
		TempRepo& operator=(const TempRepo&) = delete;

		const std::filesystem::path& GetPath() const { return m_Path; }

	private:
		std::filesystem::path m_Path;
	};

	Core::Oid CommitEmptyTree(const std::filesystem::path& path)
	{
		git_repository* repository = nullptr;
		REQUIRE(git_repository_open(&repository, Core::ToUtf8(path).c_str()) == 0);

		git_index* index = nullptr;
		REQUIRE(git_repository_index(&index, repository) == 0);
		git_oid treeId;
		REQUIRE(git_index_write_tree(&treeId, index) == 0);
		git_index_free(index);

		git_tree* tree = nullptr;
		REQUIRE(git_tree_lookup(&tree, repository, &treeId) == 0);

		git_signature* signature = nullptr;
		REQUIRE(git_signature_now(&signature, "Test", "test@example.com") == 0);

		constexpr int kParentCount = 0;
		git_oid commitId;
		REQUIRE(git_commit_create_v(&commitId, repository, "HEAD", signature, signature, nullptr, "first", tree,
									kParentCount) == 0);

		git_signature_free(signature);
		git_tree_free(tree);
		git_repository_free(repository);

		constexpr std::size_t kObjectIdByteCount = 20;
		Core::Oid commitOid;
		std::copy(commitId.id, commitId.id + kObjectIdByteCount, commitOid.Bytes.begin());
		return commitOid;
	}
}

TEST_CASE("Reopening the same root after a close opens a fresh handle")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	REQUIRE(port.QueryStatus().HasValue());
	CHECK(port.GetOpenCount() == 1);

	const std::filesystem::path noFolder;
	port.SetRoot(noFolder);
	port.SetRoot(repo.GetPath());

	REQUIRE(port.QueryStatus().HasValue());
	CHECK(port.GetOpenCount() == 2);
}

TEST_CASE("Switching to a different root opens a fresh handle for it")
{
	TempRepo repoA;
	TempRepo repoB;
	Graphite::RepositoryGitPort port{repoA.GetPath()};

	REQUIRE(port.QueryStatus().HasValue());
	CHECK(port.GetOpenCount() == 1);

	port.SetRoot(repoB.GetPath());

	REQUIRE(port.QueryStatus().HasValue());
	CHECK(port.GetOpenCount() == 2);
}

TEST_CASE("Switching to a different root does not leave the old root's handle live")
{
	TempRepo repoA;
	TempRepo repoB;
	Graphite::RepositoryGitPort port{repoA.GetPath()};

	REQUIRE(port.QueryStatus().HasValue());

	port.SetRoot(repoB.GetPath());
	REQUIRE(port.QueryStatus().HasValue());

	CHECK(port.GetLiveHandleCountForThisThread() == 1);
}

TEST_CASE("Closing the workspace does not leave the last root's handle live")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	REQUIRE(port.QueryStatus().HasValue());
	REQUIRE(port.GetLiveHandleCountForThisThread() == 1);

	const std::filesystem::path noFolder;
	port.SetRoot(noFolder);

	CHECK_FALSE(port.QueryStatus().HasValue());
	CHECK(port.GetLiveHandleCountForThisThread() == 0);
}

TEST_CASE("RepositoryGitPort stages through the same handle it queries with")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	{
		std::ofstream file{repo.GetPath() / "a.txt"};
		file << "one\n";
	}

	REQUIRE(port.Stage({"a.txt"}).HasValue());

	const Core::Result<Core::WorkingTreeStatus> status = port.QueryStatus();
	REQUIRE(status.HasValue());
	REQUIRE(status.GetValue().Entries.size() == 1);
	CHECK(status.GetValue().Entries.front().Path == std::filesystem::path{"a.txt"});
	CHECK_FALSE(status.GetValue().Entries.front().IsUntracked);

	CHECK(port.GetOpenCount() == 1);
}

TEST_CASE("The unstaged texts are the index against the disk")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	{
		std::ofstream file{repo.GetPath() / "a.txt", std::ios::binary};
		file << "staged\n";
	}

	REQUIRE(port.Stage({"a.txt"}).HasValue());

	{
		std::ofstream file{repo.GetPath() / "a.txt", std::ios::binary};
		file << "edited since\n";
	}

	const Core::Result<Graphite::FileTexts> texts = port.ReadWorkingTreeTexts(Core::DiffTarget::WorkTreeToIndex, "a.txt", "a.txt");
	REQUIRE(texts.HasValue());

	CHECK(texts.GetValue().HasOld);
	CHECK(texts.GetValue().Old == "staged\n");
	CHECK(texts.GetValue().HasNew);
	CHECK(texts.GetValue().New == "edited since\n");
}

TEST_CASE("An untracked file has no older side to show")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	{
		std::ofstream file{repo.GetPath() / "new.txt", std::ios::binary};
		file << "brand new\n";
	}

	const Core::Result<Graphite::FileTexts> texts = port.ReadWorkingTreeTexts(Core::DiffTarget::WorkTreeToIndex, "new.txt", "new.txt");
	REQUIRE(texts.HasValue());

	CHECK_FALSE(texts.GetValue().HasOld);
	CHECK(texts.GetValue().HasNew);
	CHECK(texts.GetValue().New == "brand new\n");
}

TEST_CASE("A file deleted from the working tree has no newer side to show")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	{
		std::ofstream file{repo.GetPath() / "a.txt", std::ios::binary};
		file << "staged\n";
	}

	REQUIRE(port.Stage({"a.txt"}).HasValue());

	std::error_code code;
	std::filesystem::remove(repo.GetPath() / "a.txt", code);
	REQUIRE_FALSE(code);

	const Core::Result<Graphite::FileTexts> texts = port.ReadWorkingTreeTexts(Core::DiffTarget::WorkTreeToIndex, "a.txt", "a.txt");
	REQUIRE(texts.HasValue());

	CHECK(texts.GetValue().HasOld);
	CHECK_FALSE(texts.GetValue().HasNew);
}

TEST_CASE("A staged new file has an index side and no HEAD side")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	{
		std::ofstream file{repo.GetPath() / "a.txt", std::ios::binary};
		file << "one\n";
	}

	REQUIRE(port.Stage({"a.txt"}).HasValue());

	const Core::Result<Graphite::ChangeBases> bases = port.ReadBases(repo.GetPath() / "a.txt");
	REQUIRE(bases.HasValue());

	CHECK(bases.GetValue().Tracked);
	CHECK_FALSE(bases.GetValue().Head.has_value());
	CHECK(bases.GetValue().Index == std::optional<std::string>{"one\n"});
}

TEST_CASE("A HEAD that cannot be read is a failure, not an absent HEAD side")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	{
		std::ofstream file{repo.GetPath() / "a.txt", std::ios::binary};
		file << "one\n";
	}

	REQUIRE(port.Stage({"a.txt"}).HasValue());

	{
		std::ofstream brokenHead{repo.GetPath() / ".git" / "HEAD", std::ios::binary};
		brokenHead << "not a reference\n";
	}

	const Core::Result<Graphite::ChangeBases> bases = port.ReadBases(repo.GetPath() / "a.txt");

	CHECK_FALSE(bases.HasValue());
}

TEST_CASE("An untracked file is tracked with neither side")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	{
		std::ofstream file{repo.GetPath() / "a.txt", std::ios::binary};
		file << "one\n";
	}

	const Core::Result<Graphite::ChangeBases> bases = port.ReadBases(repo.GetPath() / "a.txt");
	REQUIRE(bases.HasValue());

	CHECK(bases.GetValue().Tracked);
	CHECK_FALSE(bases.GetValue().Head.has_value());
	CHECK_FALSE(bases.GetValue().Index.has_value());
}

TEST_CASE("An ignored file is not tracked")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	{
		std::ofstream ignore{repo.GetPath() / ".gitignore", std::ios::binary};
		ignore << "*.log\n";
		std::ofstream file{repo.GetPath() / "build.log", std::ios::binary};
		file << "noise\n";
	}

	const Core::Result<Graphite::ChangeBases> bases = port.ReadBases(repo.GetPath() / "build.log");
	REQUIRE(bases.HasValue());

	CHECK_FALSE(bases.GetValue().Tracked);
}

TEST_CASE("A file outside the repository is not tracked")
{
	TempRepo repo;
	TempRepo elsewhere;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	const Core::Result<Graphite::ChangeBases> bases = port.ReadBases(elsewhere.GetPath() / "a.txt");
	REQUIRE(bases.HasValue());

	CHECK_FALSE(bases.GetValue().Tracked);
}

TEST_CASE("An oversized staged file is tracked but unusable rather than a failure")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	{
		std::ofstream file{repo.GetPath() / "a.txt", std::ios::binary};
		const std::string bytes(static_cast<std::size_t>(Core::MaximumDiffTextSize) + 1, 'a');
		file << bytes << "\n";
	}

	REQUIRE(port.Stage({"a.txt"}).HasValue());

	const Core::Result<Graphite::ChangeBases> bases = port.ReadBases(repo.GetPath() / "a.txt");
	REQUIRE(bases.HasValue());

	CHECK(bases.GetValue().Tracked);
	CHECK(bases.GetValue().Unusable);
}

TEST_CASE("A file inside a submodule is not tracked")
{
	TempRepo repo;
	Graphite::RepositoryGitPort port{repo.GetPath()};

	std::error_code code;
	std::filesystem::create_directories(repo.GetPath() / "vendor" / "lib", code);
	REQUIRE_FALSE(code);

	{
		std::ofstream file{repo.GetPath() / "vendor" / "lib" / "lib.cpp", std::ios::binary};
		file << "one\n";
	}

	{
		git_repository* rawRepository = nullptr;
		REQUIRE(git_repository_open(&rawRepository, Core::ToUtf8(repo.GetPath()).c_str()) == 0);

		git_index* index = nullptr;
		REQUIRE(git_repository_index(&index, rawRepository) == 0);

		git_index_entry entry{};
		entry.path = "vendor/lib";
		entry.mode = GIT_FILEMODE_COMMIT;
		REQUIRE(git_oid_fromstr(&entry.id, "0123456789abcdef0123456789abcdef01234567") == 0);

		REQUIRE(git_index_add(index, &entry) == 0);
		REQUIRE(git_index_write(index) == 0);

		git_index_free(index);
		git_repository_free(rawRepository);
	}

	const Core::Result<Graphite::ChangeBases> bases = port.ReadBases(repo.GetPath() / "vendor" / "lib" / "lib.cpp");
	REQUIRE(bases.HasValue());

	CHECK_FALSE(bases.GetValue().Tracked);
}

TEST_CASE("A file read with no repository open at all is not tracked")
{
	TempDirectory plain;
	Graphite::RepositoryGitPort port{plain.GetPath()};

	const Core::Result<Graphite::ChangeBases> bases = port.ReadBases(plain.GetPath() / "a.txt");
	REQUIRE(bases.HasValue());

	CHECK_FALSE(bases.GetValue().Tracked);
}

TEST_CASE("The port initialises a repository at its root and answers from it, the earlier failed open not cached")
{
	TempDirectory folder;
	Graphite::RepositoryGitPort port{folder.GetPath()};

	CHECK(port.QueryStatus().GetError().Code == Core::ErrorCode::NotFound);

	REQUIRE(port.Init().HasValue());

	Core::Result<Core::HeadState> head = port.QueryHead();
	REQUIRE(head.HasValue());
	CHECK(head.GetValue().Kind == Core::HeadKind::Unborn);
	CHECK(port.QueryStatus().HasValue());
}

TEST_CASE("The port has nothing to initialise with no folder open")
{
	Graphite::RepositoryGitPort port{std::filesystem::path{}};

	Core::Result<void> result = port.Init();
	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("A folder inside a repository reads the repository's head")
{
	TempRepo repo;
	std::filesystem::create_directories(repo.GetPath() / "sub");

	Graphite::RepositoryGitPort port{repo.GetPath() / "sub"};

	Core::Result<Core::HeadState> head = port.QueryHead();
	REQUIRE(head.HasValue());
	CHECK(head.GetValue().Kind == Core::HeadKind::Unborn);
}

namespace {

	void MakeUnopenable(const std::filesystem::path& root)
	{
		const std::string configWithUnknownFormatVersion = "[core]\n\trepositoryformatversion = 99\n";
		WriteFileBytes(root / ".git" / "config", configWithUnknownFormatVersion);
	}
}

TEST_CASE("A repository that cannot be opened is reported as its own failure, not as missing")
{
	TempRepo repo;
	MakeUnopenable(repo.GetPath());

	Graphite::RepositoryGitPort port{repo.GetPath()};

	Core::Result<Core::WorkingTreeStatus> status = port.QueryStatus();
	REQUIRE_FALSE(status.HasValue());
	CHECK(status.GetError().Code != Core::ErrorCode::NotFound);

	Core::Result<Core::HeadState> head = port.QueryHead();
	REQUIRE_FALSE(head.HasValue());
	CHECK(head.GetError().Code != Core::ErrorCode::NotFound);
}

TEST_CASE("The port refuses to initialise over a repository it cannot open")
{
	TempRepo repo;
	MakeUnopenable(repo.GetPath());
	const std::string before = ReadWholeFile(repo.GetPath() / ".git" / "config");

	Graphite::RepositoryGitPort port{repo.GetPath()};

	Core::Result<void> result = port.Init();
	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Code != Core::ErrorCode::NotFound);
	CHECK(ReadWholeFile(repo.GetPath() / ".git" / "config") == before);
}

TEST_CASE("The port refuses to initialise a folder that is already inside a repository")
{
	TempRepo repo;
	std::filesystem::create_directories(repo.GetPath() / "sub");

	Graphite::RepositoryGitPort port{repo.GetPath() / "sub"};

	Core::Result<void> result = port.Init();
	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Code == Core::ErrorCode::InvalidArgument);
	CHECK_FALSE(std::filesystem::exists(repo.GetPath() / "sub" / ".git"));
}

TEST_CASE("The port refuses to initialise a folder inside a repository it cannot open")
{
	TempRepo repo;
	MakeUnopenable(repo.GetPath());
	std::filesystem::create_directories(repo.GetPath() / "sub");

	Graphite::RepositoryGitPort port{repo.GetPath() / "sub"};

	Core::Result<void> result = port.Init();
	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Code != Core::ErrorCode::NotFound);
	CHECK_FALSE(std::filesystem::exists(repo.GetPath() / "sub" / ".git"));
}

TEST_CASE("The port creates a branch that the next query lists, and has no stashes")
{
	TempRepo repo;
	const Core::Oid tip = CommitEmptyTree(repo.GetPath());

	Graphite::RepositoryGitPort port{repo.GetPath()};

	REQUIRE(port.CreateBranch("feature/x", tip).HasValue());

	Core::Result<std::vector<Core::Branch>> branches = port.QueryBranches();
	REQUIRE(branches.HasValue());

	bool found = false;

	for (const Core::Branch& branch : branches.GetValue())
	{
		found = found || branch.Name == "feature/x";
	}

	CHECK(found);

	Core::Result<std::vector<Core::StashEntry>> stashes = port.QueryStashes();
	REQUIRE(stashes.HasValue());
	CHECK(stashes.GetValue().empty());
}

TEST_CASE("The port reads the user's e-mail from the folder's repository")
{
	TempRepo repo;

	git_repository* repository = nullptr;
	REQUIRE(git_repository_open(&repository, Core::ToUtf8(repo.GetPath()).c_str()) == 0);
	git_config* config = nullptr;
	REQUIRE(git_repository_config(&config, repository) == 0);
	REQUIRE(git_config_set_string(config, "user.email", "ann@example.com") == 0);
	git_config_free(config);
	git_repository_free(repository);

	Graphite::RepositoryGitPort port{repo.GetPath()};

	const Core::Result<std::optional<std::string>> email = port.ReadUserEmail();

	REQUIRE(email.HasValue());
	CHECK(email.GetValue() == std::optional<std::string>{"ann@example.com"});
}