#include <doctest/doctest.h>

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"

#include "FileFixtures.h"
#include "TempDirectory.h"

#include <git2.h>

#include <cctype>
#include <fstream>
#include <string>
#include <vector>

using Core::Repository;
using Core::Result;

namespace {
	void InitRepository(const std::filesystem::path& path)
	{
		git_libgit2_init();

		git_repository* repository = nullptr;
		const unsigned int isBare = 0;
		const int initResult = git_repository_init(&repository, Core::ToUtf8(path).c_str(), isBare);

		REQUIRE(initResult == 0);
		git_repository_free(repository);
	}

	// The gitlink names a commit of the submodule's own repository, which never exists in the
	// superproject's object store, so any id will do.
	void AddSubmoduleGitlink(const std::filesystem::path& root, const std::string& path)
	{
		git_repository* repository = nullptr;
		REQUIRE(git_repository_open(&repository, Core::ToUtf8(root).c_str()) == 0);

		git_index* index = nullptr;
		REQUIRE(git_repository_index(&index, repository) == 0);

		git_index_entry entry{};
		entry.path = path.c_str();
		entry.mode = GIT_FILEMODE_COMMIT;
		const char* const anySubmoduleCommit = "0123456789abcdef0123456789abcdef01234567";
		REQUIRE(git_oid_fromstr(&entry.id, anySubmoduleCommit) == 0);

		REQUIRE(git_index_add(index, &entry) == 0);
		REQUIRE(git_index_write(index) == 0);

		git_index_free(index);
		git_repository_free(repository);
	}
}

TEST_CASE("A file under a gitlink is inside a submodule")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile("vendor/lib/src/lib.cpp", "x");
	temp.MakeFile("vendor/other.txt", "x");
	temp.MakeFile("main.cpp", "x");
	AddSubmoduleGitlink(temp.GetPath(), "vendor/lib");

	Result<Repository> opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());
	const Repository& repository = opened.GetValue();

	CHECK(repository.IsInsideSubmodule(std::filesystem::path{"vendor"} / "lib" / "src" / "lib.cpp"));
	CHECK(repository.IsInsideSubmodule(std::filesystem::path{"vendor"} / "lib" / "lib.h"));

	CHECK_FALSE(repository.IsInsideSubmodule(std::filesystem::path{"vendor"} / "lib"));
	CHECK_FALSE(repository.IsInsideSubmodule(std::filesystem::path{"vendor"} / "other.txt"));
	CHECK_FALSE(repository.IsInsideSubmodule("main.cpp"));
	CHECK_FALSE(repository.IsInsideSubmodule(std::filesystem::path{"vendor"} / "library" / "x.cpp"));
}

TEST_CASE("A repository with no submodules has nothing inside one")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile("a/b/c.txt", "x");

	Result<Repository> opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	CHECK_FALSE(opened.GetValue().IsInsideSubmodule(std::filesystem::path{"a"} / "b" / "c.txt"));
	CHECK_FALSE(opened.GetValue().IsInsideSubmodule(std::filesystem::path{".."} / "a.txt"));
}

TEST_CASE("Opening a directory that is not a repository fails")
{
	TempDirectory temp;

	Result<Repository> opened = Repository::Open(temp.GetPath());

	REQUIRE_FALSE(opened.HasValue());
	CHECK(opened.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("Opening succeeds from the repository root")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());

	Result<Repository> opened = Repository::Open(temp.GetPath());

	REQUIRE(opened.HasValue());
}

TEST_CASE("Opening succeeds from a directory inside the repository")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeDirectory("deep/nested");

	Result<Repository> opened = Repository::Open(temp.GetPath() / "deep" / "nested");

	REQUIRE(opened.HasValue());
}

TEST_CASE("Opening exactly a repository's own directory succeeds")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());

	Result<Repository> opened = Repository::OpenExactly(temp.GetPath());

	REQUIRE(opened.HasValue());
	CHECK(std::filesystem::equivalent(opened.GetValue().GetWorkingDirectory(), temp.GetPath()));
}

TEST_CASE("Opening exactly a directory inside a repository does not find the repository around it")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeDirectory("deep/nested");

	Result<Repository> opened = Repository::OpenExactly(temp.GetPath() / "deep" / "nested");

	REQUIRE_FALSE(opened.HasValue());
	CHECK(opened.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("A file matching a rule is reported as ignored")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile(".gitignore", "*.log\n");
	temp.MakeFile("error.log", "x");
	temp.MakeFile("main.cpp", "x");

	Result<Repository> opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	const Repository& repository = opened.GetValue();

	CHECK(repository.IsIgnored(temp.GetPath() / "error.log"));
	CHECK_FALSE(repository.IsIgnored(temp.GetPath() / "main.cpp"));
}

TEST_CASE("A nested rules file applies inside its own directory only")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeDirectory("sub");
	temp.MakeFile("sub/.gitignore", "secret.txt\n");
	temp.MakeFile("sub/secret.txt", "x");
	temp.MakeFile("secret.txt", "x");

	Result<Repository> opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	const Repository& repository = opened.GetValue();

	CHECK(repository.IsIgnored(temp.GetPath() / "sub" / "secret.txt"));
	CHECK_FALSE(repository.IsIgnored(temp.GetPath() / "secret.txt"));
}

TEST_CASE("A wildcard-free negation in a nested file does not override a parent rule")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile(".gitignore", "*.log\n");
	temp.MakeDirectory("sub");
	temp.MakeFile("sub/.gitignore", "!keep.log\n");
	temp.MakeFile("sub/keep.log", "x");
	temp.MakeFile("sub/other.log", "x");

	Result<Repository> opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	const Repository& repository = opened.GetValue();

	CHECK(repository.IsIgnored(temp.GetPath() / "sub" / "other.log"));

	// libgit2 drops a wildcard-free negation that negates no rule of its own file, where git
	// honours it and reports keep.log as tracked. Check the vendored libgit2 before changing this
	// to match git.
	CHECK(repository.IsIgnored(temp.GetPath() / "sub" / "keep.log"));
}

TEST_CASE("A negation with a wildcard does override a parent rule")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile(".gitignore", "*.log\n");
	temp.MakeDirectory("sub");
	temp.MakeFile("sub/.gitignore", "!keep.*\n");
	temp.MakeFile("sub/keep.log", "x");
	temp.MakeFile("sub/other.log", "x");

	Result<Repository> opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	const Repository& repository = opened.GetValue();

	CHECK(repository.IsIgnored(temp.GetPath() / "sub" / "other.log"));
	CHECK_FALSE(repository.IsIgnored(temp.GetPath() / "sub" / "keep.log"));
}

TEST_CASE("A file whose name begins with two dots is one component inside the tree rather than a parent reference")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile(".gitignore", "..config\n");
	temp.MakeFile("..config", "x");

	Result<Repository> opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	CHECK(opened.GetValue().IsIgnored(temp.GetPath() / "..config"));
}

TEST_CASE("A file inside an ignored directory is ignored")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile(".gitignore", "build/\n");
	temp.MakeFile("build/output.o", "x");

	Result<Repository> opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	CHECK(opened.GetValue().IsIgnored(temp.GetPath() / "build" / "output.o"));
}

TEST_CASE("A path spelled with other case and other separators than the working directory is still asked about")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile(".gitignore", "*.log\n");
	temp.MakeFile("sub/error.log", "x");

	Result<Repository> opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	const std::string forwardSlashedRoot = Core::ToGenericUtf8(temp.GetPath());
	std::string otherCaseRoot;

	for (const char character : forwardSlashedRoot)
	{
		otherCaseRoot += static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
	}

	CHECK(opened.GetValue().IsIgnored(Core::FromUtf8(otherCaseRoot + "/sub/error.log")));
	CHECK(opened.GetValue().IsIgnored(Core::FromUtf8(otherCaseRoot + "\\sub\\.\\error.log")));
}

namespace {
	bool NamesFile(const std::vector<std::filesystem::path>& files, const std::filesystem::path& wanted)
	{
		for (const std::filesystem::path& file : files)
		{
			if (Core::SamePath(file, wanted))
			{
				return true;
			}
		}

		return false;
	}
}

TEST_CASE("A folder below the working directory has the rules files of the folders above it among its ignore rules")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	const std::filesystem::path deep = temp.MakeDirectory("a/b");

	Result<Repository> opened = Repository::Open(deep);
	REQUIRE(opened.HasValue());

	const std::vector<std::filesystem::path> files = opened.GetValue().IgnoreRuleFilesOutside(deep);

	CHECK(NamesFile(files, temp.GetPath() / ".gitignore"));
	CHECK(NamesFile(files, temp.GetPath() / "a" / ".gitignore"));
	CHECK(NamesFile(files, temp.GetPath() / ".git" / "info" / "exclude"));
	CHECK(NamesFile(files, temp.GetPath() / ".git" / "config"));
	CHECK_FALSE(NamesFile(files, deep / ".gitignore"));
	CHECK_FALSE(NamesFile(files, temp.GetPath().parent_path() / ".gitignore"));
}

TEST_CASE("A path outside the working directory is never ignored, even one whose folder name starts with it")
{
	TempDirectory temp;
	const std::filesystem::path repositoryRoot = temp.MakeDirectory("repo");
	InitRepository(repositoryRoot);
	temp.MakeFile("repo/.gitignore", "*.log\n");
	temp.MakeFile("repo-other/error.log", "x");
	temp.MakeFile("outside.log", "x");

	Result<Repository> opened = Repository::Open(repositoryRoot);
	REQUIRE(opened.HasValue());

	CHECK_FALSE(opened.GetValue().IsIgnored(temp.GetPath() / "repo-other" / "error.log"));
	CHECK_FALSE(opened.GetValue().IsIgnored(temp.GetPath() / "outside.log"));
	CHECK_FALSE(opened.GetValue().IsIgnored(repositoryRoot / ".." / "outside.log"));
}

TEST_CASE("A directory-only rule ignores the directory itself and not a similarly named file")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile(".gitignore", "build/\n");
	temp.MakeDirectory("build");
	temp.MakeFile("build.txt", "x");

	Result<Repository> opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	const Repository& repository = opened.GetValue();

	CHECK(repository.IsIgnored(temp.GetPath() / "build"));
	CHECK_FALSE(repository.IsIgnored(temp.GetPath() / "build.txt"));
}

TEST_CASE("The repository exposes its working directory")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());

	Result<Repository> opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	CHECK_FALSE(opened.GetValue().GetWorkingDirectory().empty());
}

TEST_CASE("A repository moved out of its open result does not double-free when both are destroyed")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());

	Result<Repository> opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	Repository moved = std::move(opened).GetValue();

	CHECK_FALSE(moved.GetWorkingDirectory().empty());
}

TEST_CASE("An open repository keeps reading the working tree's .gitignore after its .git directory is deleted")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile(".gitignore", "*.log\n");
	temp.MakeFile("build.log", "x");

	Result<Repository> opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	Repository repository = std::move(opened).GetValue();
	REQUIRE(repository.IsIgnored(temp.GetPath() / "build.log"));

	std::error_code removeError;
	std::filesystem::remove_all(temp.GetPath() / ".git", removeError);
	REQUIRE_FALSE(removeError);

	INFO("If this fails, libgit2 changed and now invalidates the handle itself — "
		 "re-evaluate whether Graphite::Workspace::SyncRepository is still needed.");
	CHECK(repository.IsIgnored(temp.GetPath() / "build.log"));
}

namespace {
	void SetExcludesFile(const std::filesystem::path& repositoryRoot,
						 const std::filesystem::path& excludesFile)
	{
		std::string excludesFileText = Core::ToUtf8(excludesFile);

		for (char& character : excludesFileText)
		{
			if (character == '\\')
			{
				character = '/';
			}
		}

		std::ofstream config{repositoryRoot / ".git" / "config", std::ios::app};
		config << "\texcludesfile = " << excludesFileText << "\n";
	}
}

TEST_CASE("An edited excludes file outside the working tree is picked up without reopening the repository because libgit2 revalidates rule files on every query")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile("build.log", "x");

	const std::filesystem::path excludesFile = temp.GetPath() / "my-excludes";
	WriteFileBytes(excludesFile, "\n");
	SetExcludesFile(temp.GetPath(), excludesFile);

	Result<Repository> opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	Repository repository = std::move(opened).GetValue();
	REQUIRE_FALSE(repository.IsIgnored(temp.GetPath() / "build.log"));

	WriteFileBytes(excludesFile, "*.log\n");

	CHECK(repository.IsIgnored(temp.GetPath() / "build.log"));
}

TEST_CASE("Repointing core.excludesFile needs the repository reopened because libgit2 resolves it once at open")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile("build.log", "x");

	const std::filesystem::path emptyExcludesFile = temp.GetPath() / "empty-excludes";
	const std::filesystem::path realExcludesFile = temp.GetPath() / "real-excludes";
	WriteFileBytes(emptyExcludesFile, "\n");
	WriteFileBytes(realExcludesFile, "*.log\n");
	SetExcludesFile(temp.GetPath(), emptyExcludesFile);

	Result<Repository> opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	Repository repository = std::move(opened).GetValue();
	REQUIRE_FALSE(repository.IsIgnored(temp.GetPath() / "build.log"));

	// Only repointing core.excludesFile needs a reopen; it is rare enough that the application never
	// reopens on its own.
	SetExcludesFile(temp.GetPath(), realExcludesFile);

	INFO("If this fails, libgit2 started rereading config — a refresh would then cover "
		 "this case too and any workaround built around it can go.");
	CHECK_FALSE(repository.IsIgnored(temp.GetPath() / "build.log"));
}

TEST_CASE("The excludes file core.excludesFile names, and a worktree config, are among the rules files outside the folder")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	const std::filesystem::path excludesFile = temp.GetPath() / "my-excludes";
	WriteFileBytes(excludesFile, "\n");
	SetExcludesFile(temp.GetPath(), excludesFile);

	Result<Repository> opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	const std::vector<std::filesystem::path> files = opened.GetValue().IgnoreRuleFilesOutside(temp.GetPath());

	CHECK(NamesFile(files, excludesFile));
	CHECK(NamesFile(files, temp.GetPath() / ".git" / "config.worktree"));
}

namespace {
	constexpr int kConfigLevelsReadByEveryRepository[] = {GIT_CONFIG_LEVEL_PROGRAMDATA, GIT_CONFIG_LEVEL_SYSTEM,
														  GIT_CONFIG_LEVEL_XDG, GIT_CONFIG_LEVEL_GLOBAL};

	// WARNING: libgit2's search paths are global to the process, so every one changed here is put back.
	class SearchPathsInFolder
	{
	public:
		explicit SearchPathsInFolder(const std::filesystem::path& folder)
		{
			git_libgit2_init();

			for (const int level : kConfigLevelsReadByEveryRepository)
			{
				git_buf previous = GIT_BUF_INIT;
				REQUIRE(git_libgit2_opts(GIT_OPT_GET_SEARCH_PATH, level, &previous) == 0);
				m_Previous.push_back(previous.ptr != nullptr ? std::string{previous.ptr} : std::string{});
				git_buf_dispose(&previous);

				REQUIRE(git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, level, Core::ToUtf8(folder).c_str()) == 0);
			}
		}

		~SearchPathsInFolder()
		{
			for (std::size_t levelIndex = 0; levelIndex < m_Previous.size(); ++levelIndex)
			{
				git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, kConfigLevelsReadByEveryRepository[levelIndex],
								 m_Previous[levelIndex].c_str());
			}
		}

		SearchPathsInFolder(const SearchPathsInFolder&) = delete;
		SearchPathsInFolder& operator=(const SearchPathsInFolder&) = delete;

	private:
		std::vector<std::string> m_Previous;
	};
}

TEST_CASE("Without core.excludesFile, the ignore file of the XDG configuration folder is among the rules files outside the folder")
{
	TempDirectory temp;
	const std::filesystem::path repositoryRoot = temp.MakeDirectory("repo");
	const std::filesystem::path configurationFolder = temp.MakeDirectory("xdg-git");
	InitRepository(repositoryRoot);

	const SearchPathsInFolder searchPaths{configurationFolder};

	Result<Repository> opened = Repository::Open(repositoryRoot);
	REQUIRE(opened.HasValue());

	CHECK(NamesFile(opened.GetValue().IgnoreRuleFilesOutside(repositoryRoot), configurationFolder / "ignore"));
}

TEST_CASE("A moved-from repository holds no handle and answers every question with an error rather than handing libgit2 a null repository")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());

	Result<Repository> opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	Repository source = std::move(opened).GetValue();
	const Repository moved = std::move(source);
	const Repository& empty = source;
	const std::filesystem::path file = temp.GetPath() / "a.txt";
	const Core::LogRequest anyLogRequest{};
	const Core::DiffRequest anyDiffRequest{};
	const Core::CheckoutTarget anyCheckoutTarget{};
	const Core::CommitRequest anyCommitRequest{};

	CHECK_FALSE(empty.IsIgnored(file));
	CHECK_FALSE(empty.IsInsideSubmodule("a.txt"));
	CHECK_FALSE(empty.QueryLog(anyLogRequest).HasValue());
	CHECK_FALSE(empty.QueryDiff(anyDiffRequest).HasValue());
	CHECK_FALSE(empty.QueryStatus().HasValue());
	CHECK_FALSE(empty.QueryUntrackedIn(temp.GetPath()).HasValue());
	CHECK_FALSE(empty.QueryBranches().HasValue());
	CHECK_FALSE(empty.ReadBlob(file, Core::Oid{}).HasValue());
	CHECK_FALSE(empty.HeadCommit().HasValue());
	CHECK_FALSE(empty.QueryHead().HasValue());
	CHECK_FALSE(empty.ReadIndexBlob(file).HasValue());
	CHECK_FALSE(empty.ReadWorkingTreeFile(file).HasValue());
	CHECK_FALSE(empty.FirstParentOf(Core::Oid{}).HasValue());
	CHECK_FALSE(empty.UntrackedOfStash(Core::Oid{}).HasValue());
	CHECK_FALSE(empty.CountUnmergedCommits("main").HasValue());
	CHECK_FALSE(empty.QueryStashes().HasValue());
	CHECK_FALSE(empty.PlanCheckout(anyCheckoutTarget).HasValue());

	CHECK_FALSE(source.Stage({file}).HasValue());
	CHECK_FALSE(source.Unstage({file}).HasValue());
	CHECK_FALSE(source.DiscardWorkTree({file}).HasValue());
	CHECK_FALSE(source.DeleteUntracked({file}).HasValue());
	CHECK_FALSE(source.Commit(anyCommitRequest).HasValue());
	CHECK_FALSE(source.CreateBranch("b", Core::Oid{}).HasValue());
	CHECK_FALSE(source.RenameBranch("a", "b").HasValue());
	CHECK_FALSE(source.DeleteBranch("b").HasValue());
	CHECK_FALSE(source.StashSave("m", Core::UntrackedFiles::Excluded).HasValue());
	CHECK_FALSE(source.StashApply(Core::Oid{}).HasValue());
	CHECK_FALSE(source.StashPop(Core::Oid{}).HasValue());
	CHECK_FALSE(source.StashDrop(Core::Oid{}).HasValue());
	CHECK(source.Checkout(anyCheckoutTarget).Failure.has_value());
	CHECK_FALSE(source.DiscardAll(Core::UntrackedFiles::Excluded).HasValue());
}
