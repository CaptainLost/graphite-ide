#include <doctest/doctest.h>

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"

#include "TempDirectory.h"

#include <git2.h>

#include <fstream>
#include <string>
#include <string_view>

using Core::Repository;

namespace {
	void InitRepository(const std::filesystem::path& path)
	{
		git_libgit2_init();

		git_repository* repository = nullptr;
		const int result = git_repository_init(&repository, Core::ToUtf8(path).c_str(), 0);

		REQUIRE(result == 0);
		git_repository_free(repository);
	}

	// Records a submodule the way `git submodule add` leaves it in the index: a gitlink entry,
	// mode 160000, naming a commit of the submodule's own repository. The commit does not have
	// to exist here - it never does in the superproject's object store.
	void AddGitlink(const std::filesystem::path& root, const std::string& path)
	{
		git_repository* repository = nullptr;
		REQUIRE(git_repository_open(&repository, Core::ToUtf8(root).c_str()) == 0);

		git_index* index = nullptr;
		REQUIRE(git_repository_index(&index, repository) == 0);

		git_index_entry entry{};
		entry.path = path.c_str();
		entry.mode = GIT_FILEMODE_COMMIT;
		REQUIRE(git_oid_fromstr(&entry.id, "0123456789abcdef0123456789abcdef01234567") == 0);

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
	AddGitlink(temp.GetPath(), "vendor/lib");

	auto opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());
	const Repository& repository = opened.GetValue();

	CHECK(repository.IsInsideSubmodule(std::filesystem::path{"vendor"} / "lib" / "src" / "lib.cpp"));
	CHECK(repository.IsInsideSubmodule(std::filesystem::path{"vendor"} / "lib" / "lib.h"));

	// The gitlink itself, a sibling of it and a file elsewhere are not inside it.
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

	auto opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	CHECK_FALSE(opened.GetValue().IsInsideSubmodule(std::filesystem::path{"a"} / "b" / "c.txt"));
	CHECK_FALSE(opened.GetValue().IsInsideSubmodule(std::filesystem::path{".."} / "a.txt"));
}

TEST_CASE("Opening a directory that is not a repository fails")
{
	TempDirectory temp;

	auto opened = Repository::Open(temp.GetPath());

	REQUIRE_FALSE(opened.HasValue());
	CHECK(opened.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("Opening succeeds from the repository root")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());

	auto opened = Repository::Open(temp.GetPath());

	REQUIRE(opened.HasValue());
}

TEST_CASE("Opening succeeds from a directory inside the repository")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeDirectory("deep/nested");

	auto opened = Repository::Open(temp.GetPath() / "deep" / "nested");

	REQUIRE(opened.HasValue());
}

TEST_CASE("A file matching a rule is reported as ignored")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile(".gitignore", "*.log\n");
	temp.MakeFile("error.log", "x");
	temp.MakeFile("main.cpp", "x");

	auto opened = Repository::Open(temp.GetPath());
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

	auto opened = Repository::Open(temp.GetPath());
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

	auto opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	const Repository& repository = opened.GetValue();

	CHECK(repository.IsIgnored(temp.GetPath() / "sub" / "other.log"));

	// This asserts libgit2's behaviour, which diverges from the git command line
	// here: git honours the nested negation and reports keep.log as tracked.
	// libgit2's ignore.c discards a negation that carries no wildcard when it does
	// not negate a rule from the SAME .gitignore file — does_negate_rule only scans
	// the rules of the file being parsed, so the parent's "*.log" is invisible to
	// it. A negation WITH a wildcard, such as "!keep.*", is kept and does work.
	// Do not "fix" this assertion to match git without first checking whether the
	// vendored libgit2 version still behaves this way.
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

	auto opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	const Repository& repository = opened.GetValue();

	CHECK(repository.IsIgnored(temp.GetPath() / "sub" / "other.log"));
	CHECK_FALSE(repository.IsIgnored(temp.GetPath() / "sub" / "keep.log"));
}

TEST_CASE("A file whose name begins with two dots is inside the tree")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile(".gitignore", "..config\n");
	temp.MakeFile("..config", "x");

	auto opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	// "..config" is a single path component, not a parent reference. A string-prefix
	// test for ".." would treat it as outside the working tree and answer "not
	// ignored" for every such file, whatever the rules say.
	CHECK(opened.GetValue().IsIgnored(temp.GetPath() / "..config"));
}

TEST_CASE("A file inside an ignored directory is ignored")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile(".gitignore", "build/\n");
	temp.MakeFile("build/output.o", "x");

	auto opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	// The commonest shape in a real tree, and the one the file tree will ask about
	// most: the rule names the directory, the query names a file underneath it.
	CHECK(opened.GetValue().IsIgnored(temp.GetPath() / "build" / "output.o"));
}

TEST_CASE("A directory-only rule ignores the directory itself")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile(".gitignore", "build/\n");
	temp.MakeDirectory("build");
	temp.MakeFile("build.txt", "x");

	auto opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	const Repository& repository = opened.GetValue();

	CHECK(repository.IsIgnored(temp.GetPath() / "build"));

	// The trailing slash means directories only, so the similarly named file must
	// not match. The hand-rolled matcher pinned this before it was deleted; nothing
	// pinned it afterwards until now.
	CHECK_FALSE(repository.IsIgnored(temp.GetPath() / "build.txt"));
}

TEST_CASE("The repository exposes its working directory")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());

	auto opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	CHECK_FALSE(opened.GetValue().GetWorkingDirectory().empty());
}

TEST_CASE("A moved-from repository does not double-free")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());

	auto opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	Repository moved = std::move(opened).GetValue();

	// The source must have been left with nothing to release; the destructor of both
	// objects runs at the end of this scope and must not free the same handle twice.
	CHECK_FALSE(moved.GetWorkingDirectory().empty());
}

TEST_CASE("An open repository keeps answering after its .git directory is deleted")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile(".gitignore", "*.log\n");
	temp.MakeFile("build.log", "x");

	auto opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	Repository repository = std::move(opened).GetValue();
	REQUIRE(repository.IsIgnored(temp.GetPath() / "build.log"));

	std::error_code code;
	std::filesystem::remove_all(temp.GetPath() / ".git", code);
	REQUIRE_FALSE(code);

	// Not the intuitive answer, which is why it is pinned here. The folder is no longer
	// a repository, but .gitignore lives in the working tree rather than in .git, and
	// libgit2 goes on reading it through the handle it already holds. So the verdict
	// survives the repository that justified it.
	//
	// Callers therefore cannot wait to be told. Graphite::Workspace::SyncRepository watches
	// for .git disappearing and drops the repository itself; without that, a folder that
	// stopped being a repository would keep dimming entries for the rest of the session.
	INFO("If this fails, libgit2 changed and now invalidates the handle itself — "
		 "re-evaluate whether Graphite::Workspace::SyncRepository is still needed.");
	CHECK(repository.IsIgnored(temp.GetPath() / "build.log"));
}

namespace {
	void SetExcludesFile(const std::filesystem::path& repository,
						 const std::filesystem::path& excludes)
	{
		std::string text = Core::ToUtf8(excludes);

		for (char& character : text)
		{
			if (character == '\\')
			{
				character = '/';
			}
		}

		std::ofstream config{repository / ".git" / "config", std::ios::app};
		config << "\texcludesfile = " << text << "\n";
	}

	void WriteFile(const std::filesystem::path& path, std::string_view contents)
	{
		std::ofstream stream{path, std::ios::binary | std::ios::trunc};
		stream << contents;
	}
}

TEST_CASE("An edited excludes file is picked up without reopening the repository")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile("build.log", "x");

	const auto excludes = temp.GetPath() / "my-excludes";
	WriteFile(excludes, "\n");
	SetExcludesFile(temp.GetPath(), excludes);

	auto opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	Repository repository = std::move(opened).GetValue();
	REQUIRE_FALSE(repository.IsIgnored(temp.GetPath() / "build.log"));

	WriteFile(excludes, "*.log\n");

	// libgit2 revalidates rule files by timestamp and size on every query, so editing
	// one takes effect through nothing more than a refresh of the tree. That holds for
	// the file core.excludesFile points at just as it does for .gitignore, which is why
	// this file lives outside the working tree and is still covered.
	CHECK(repository.IsIgnored(temp.GetPath() / "build.log"));
}

TEST_CASE("Repointing core.excludesFile needs the repository reopened")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());
	temp.MakeFile("build.log", "x");

	const auto empty = temp.GetPath() / "empty-excludes";
	const auto real = temp.GetPath() / "real-excludes";
	WriteFile(empty, "\n");
	WriteFile(real, "*.log\n");
	SetExcludesFile(temp.GetPath(), empty);

	auto opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	Repository repository = std::move(opened).GetValue();
	REQUIRE_FALSE(repository.IsIgnored(temp.GetPath() / "build.log"));

	SetExcludesFile(temp.GetPath(), real);

	// The other half of the answer, and the surprising one: the *contents* of a rule
	// file are revalidated, but the configuration saying which file to read is not. It
	// is resolved once when the repository is opened and cached in the handle.
	//
	// So a refresh covers editing the ignore rules, and only changing which file holds
	// them requires reopening the folder. That is a once-in-a-long-while action, which
	// is why nothing in the application reopens on its own.
	INFO("If this fails, libgit2 started rereading config — a refresh would then cover "
		 "this case too and any workaround built around it can go.");
	CHECK_FALSE(repository.IsIgnored(temp.GetPath() / "build.log"));
}

// A moved-from Repository holds no handle. Every question asked of it fails as one asked of a
// repository that could not be read, rather than handing libgit2 a null repository.
TEST_CASE("A moved-from repository answers every question with an error")
{
	TempDirectory temp;
	InitRepository(temp.GetPath());

	auto opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	Repository source = std::move(opened).GetValue();
	const Repository moved = std::move(source);
	const Repository& empty = source;
	const std::filesystem::path file = temp.GetPath() / "a.txt";

	CHECK_FALSE(empty.IsIgnored(file));
	CHECK_FALSE(empty.IsInsideSubmodule("a.txt"));
	CHECK_FALSE(empty.QueryLog({}).HasValue());
	CHECK_FALSE(empty.QueryDiff({}).HasValue());
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
	CHECK_FALSE(empty.PlanCheckout({}).HasValue());

	CHECK_FALSE(source.Stage({file}).HasValue());
	CHECK_FALSE(source.Unstage({file}).HasValue());
	CHECK_FALSE(source.DiscardWorkTree({file}).HasValue());
	CHECK_FALSE(source.DeleteUntracked({file}).HasValue());
	CHECK_FALSE(source.Commit({}).HasValue());
	CHECK_FALSE(source.CreateBranch("b", Core::Oid{}).HasValue());
	CHECK_FALSE(source.RenameBranch("a", "b").HasValue());
	CHECK_FALSE(source.DeleteBranch("b").HasValue());
	CHECK_FALSE(source.StashSave("m", false).HasValue());
	CHECK_FALSE(source.StashApply(Core::Oid{}).HasValue());
	CHECK_FALSE(source.StashPop(Core::Oid{}).HasValue());
	CHECK_FALSE(source.StashDrop(Core::Oid{}).HasValue());
	CHECK(source.Checkout({}).Failure.has_value());
	CHECK_FALSE(source.DiscardAll(false).HasValue());
}
