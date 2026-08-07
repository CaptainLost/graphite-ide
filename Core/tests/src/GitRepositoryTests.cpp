#include <doctest/doctest.h>

#include "Core/Fs/FileSystem.h"
#include "Core/Git/Repository.h"

#include "TempDirectory.h"

#include <git2.h>

using core::git::Repository;

namespace
{
    /// Creates a real repository so the tests exercise libgit2 rather than a stub.
    /// libgit2 can initialise one itself, so this needs no git executable on PATH.
    void InitRepository(const std::filesystem::path& path)
    {
        git_libgit2_init();

        git_repository* repository = nullptr;
        const int result = git_repository_init(&repository, core::fs::ToUtf8(path).c_str(), 0);

        REQUIRE(result == 0);
        git_repository_free(repository);
    }
}

TEST_CASE("Opening a directory that is not a repository fails")
{
    TempDirectory temp;

    auto opened = Repository::Open(temp.Path());

    REQUIRE_FALSE(opened.HasValue());
    CHECK(opened.GetError().code == core::ErrorCode::NotFound);
}

TEST_CASE("Opening succeeds from the repository root")
{
    TempDirectory temp;
    InitRepository(temp.Path());

    auto opened = Repository::Open(temp.Path());

    REQUIRE(opened.HasValue());
}

TEST_CASE("Opening succeeds from a directory inside the repository")
{
    TempDirectory temp;
    InitRepository(temp.Path());
    temp.MakeDir("deep/nested");

    auto opened = Repository::Open(temp.Path() / "deep" / "nested");

    REQUIRE(opened.HasValue());
}

TEST_CASE("A file matching a rule is reported as ignored")
{
    TempDirectory temp;
    InitRepository(temp.Path());
    temp.MakeFile(".gitignore", "*.log\n");
    temp.MakeFile("error.log", "x");
    temp.MakeFile("main.cpp", "x");

    auto opened = Repository::Open(temp.Path());
    REQUIRE(opened.HasValue());

    const Repository& repository = opened.Value();

    CHECK(repository.IsIgnored(temp.Path() / "error.log"));
    CHECK_FALSE(repository.IsIgnored(temp.Path() / "main.cpp"));
}

TEST_CASE("A nested rules file applies inside its own directory only")
{
    TempDirectory temp;
    InitRepository(temp.Path());
    temp.MakeDir("sub");
    temp.MakeFile("sub/.gitignore", "secret.txt\n");
    temp.MakeFile("sub/secret.txt", "x");
    temp.MakeFile("secret.txt", "x");

    auto opened = Repository::Open(temp.Path());
    REQUIRE(opened.HasValue());

    const Repository& repository = opened.Value();

    CHECK(repository.IsIgnored(temp.Path() / "sub" / "secret.txt"));
    CHECK_FALSE(repository.IsIgnored(temp.Path() / "secret.txt"));
}

TEST_CASE("A wildcard-free negation in a nested file does not override a parent rule")
{
    TempDirectory temp;
    InitRepository(temp.Path());
    temp.MakeFile(".gitignore", "*.log\n");
    temp.MakeDir("sub");
    temp.MakeFile("sub/.gitignore", "!keep.log\n");
    temp.MakeFile("sub/keep.log", "x");
    temp.MakeFile("sub/other.log", "x");

    auto opened = Repository::Open(temp.Path());
    REQUIRE(opened.HasValue());

    const Repository& repository = opened.Value();

    CHECK(repository.IsIgnored(temp.Path() / "sub" / "other.log"));

    // This asserts libgit2's behaviour, which diverges from the git command line
    // here: git honours the nested negation and reports keep.log as tracked.
    // libgit2's ignore.c discards a negation that carries no wildcard when it does
    // not negate a rule from the SAME .gitignore file — does_negate_rule only scans
    // the rules of the file being parsed, so the parent's "*.log" is invisible to
    // it. A negation WITH a wildcard, such as "!keep.*", is kept and does work.
    // Do not "fix" this assertion to match git without first checking whether the
    // vendored libgit2 version still behaves this way.
    CHECK(repository.IsIgnored(temp.Path() / "sub" / "keep.log"));
}

TEST_CASE("A negation with a wildcard does override a parent rule")
{
    TempDirectory temp;
    InitRepository(temp.Path());
    temp.MakeFile(".gitignore", "*.log\n");
    temp.MakeDir("sub");
    temp.MakeFile("sub/.gitignore", "!keep.*\n");
    temp.MakeFile("sub/keep.log", "x");
    temp.MakeFile("sub/other.log", "x");

    auto opened = Repository::Open(temp.Path());
    REQUIRE(opened.HasValue());

    const Repository& repository = opened.Value();

    CHECK(repository.IsIgnored(temp.Path() / "sub" / "other.log"));
    CHECK_FALSE(repository.IsIgnored(temp.Path() / "sub" / "keep.log"));
}

TEST_CASE("A file whose name begins with two dots is inside the tree")
{
    TempDirectory temp;
    InitRepository(temp.Path());
    temp.MakeFile(".gitignore", "..config\n");
    temp.MakeFile("..config", "x");

    auto opened = Repository::Open(temp.Path());
    REQUIRE(opened.HasValue());

    // "..config" is a single path component, not a parent reference. A string-prefix
    // test for ".." would treat it as outside the working tree and answer "not
    // ignored" for every such file, whatever the rules say.
    CHECK(opened.Value().IsIgnored(temp.Path() / "..config"));
}

TEST_CASE("A file inside an ignored directory is ignored")
{
    TempDirectory temp;
    InitRepository(temp.Path());
    temp.MakeFile(".gitignore", "build/\n");
    temp.MakeFile("build/output.o", "x");

    auto opened = Repository::Open(temp.Path());
    REQUIRE(opened.HasValue());

    // The commonest shape in a real tree, and the one the file tree will ask about
    // most: the rule names the directory, the query names a file underneath it.
    CHECK(opened.Value().IsIgnored(temp.Path() / "build" / "output.o"));
}

TEST_CASE("A directory-only rule ignores the directory itself")
{
    TempDirectory temp;
    InitRepository(temp.Path());
    temp.MakeFile(".gitignore", "build/\n");
    temp.MakeDir("build");
    temp.MakeFile("build.txt", "x");

    auto opened = Repository::Open(temp.Path());
    REQUIRE(opened.HasValue());

    const Repository& repository = opened.Value();

    CHECK(repository.IsIgnored(temp.Path() / "build"));

    // The trailing slash means directories only, so the similarly named file must
    // not match. The hand-rolled matcher pinned this before it was deleted; nothing
    // pinned it afterwards until now.
    CHECK_FALSE(repository.IsIgnored(temp.Path() / "build.txt"));
}

TEST_CASE("The repository exposes its working directory")
{
    TempDirectory temp;
    InitRepository(temp.Path());

    auto opened = Repository::Open(temp.Path());
    REQUIRE(opened.HasValue());

    CHECK_FALSE(opened.Value().WorkingDirectory().empty());
}

TEST_CASE("A moved-from repository does not double-free")
{
    TempDirectory temp;
    InitRepository(temp.Path());

    auto opened = Repository::Open(temp.Path());
    REQUIRE(opened.HasValue());

    Repository moved = std::move(opened).Value();

    // The source must have been left with nothing to release; the destructor of both
    // objects runs at the end of this scope and must not free the same handle twice.
    CHECK_FALSE(moved.WorkingDirectory().empty());
}

TEST_CASE("An open repository keeps answering after its .git directory is deleted")
{
    TempDirectory temp;
    InitRepository(temp.Path());
    temp.MakeFile(".gitignore", "*.log\n");
    temp.MakeFile("build.log", "x");

    auto opened = Repository::Open(temp.Path());
    REQUIRE(opened.HasValue());

    Repository repository = std::move(opened).Value();
    REQUIRE(repository.IsIgnored(temp.Path() / "build.log"));

    std::error_code code;
    std::filesystem::remove_all(temp.Path() / ".git", code);
    REQUIRE_FALSE(code);

    // Not the intuitive answer, which is why it is pinned here. The folder is no longer
    // a repository, but .gitignore lives in the working tree rather than in .git, and
    // libgit2 goes on reading it through the handle it already holds. So the verdict
    // survives the repository that justified it.
    //
    // Callers therefore cannot wait to be told. ide::Workspace::SyncRepository watches
    // for .git disappearing and drops the repository itself; without that, a folder that
    // stopped being a repository would keep dimming entries for the rest of the session.
    INFO("If this fails, libgit2 changed and now invalidates the handle itself — "
         "re-evaluate whether ide::Workspace::SyncRepository is still needed.");
    CHECK(repository.IsIgnored(temp.Path() / "build.log"));
}
