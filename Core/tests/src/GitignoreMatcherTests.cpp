#include <doctest/doctest.h>

#include "Core/Fs/GitignoreMatcher.h"

using core::GitignoreMatcher;

namespace
{
    bool IgnoresFile(std::string_view rules, std::string_view path)
    {
        return GitignoreMatcher::Parse(rules).IsIgnored(path, false);
    }

    bool IgnoresDirectory(std::string_view rules, std::string_view path)
    {
        return GitignoreMatcher::Parse(rules).IsIgnored(path, true);
    }
}

TEST_CASE("An empty rules file ignores nothing")
{
    const GitignoreMatcher matcher = GitignoreMatcher::Parse("");

    CHECK(matcher.IsEmpty());
    CHECK_FALSE(matcher.IsIgnored("anything.txt", false));
}

TEST_CASE("Comments and blank lines are not patterns")
{
    const GitignoreMatcher matcher = GitignoreMatcher::Parse("# a comment\n\n   \n");

    CHECK(matcher.IsEmpty());
    CHECK_FALSE(matcher.IsIgnored("# a comment", false));
}

TEST_CASE("A plain name matches at any depth")
{
    CHECK(IgnoresFile("build", "build"));
    CHECK(IgnoresFile("build", "src/build"));
    CHECK(IgnoresDirectory("build", "deep/nested/build"));
    CHECK_FALSE(IgnoresFile("build", "rebuild"));
}

TEST_CASE("A leading slash anchors the pattern to the rules file's directory")
{
    CHECK(IgnoresFile("/build", "build"));
    CHECK_FALSE(IgnoresFile("/build", "src/build"));
}

TEST_CASE("A trailing slash matches directories only")
{
    CHECK(IgnoresDirectory("build/", "build"));
    CHECK_FALSE(IgnoresFile("build/", "build"));
}

TEST_CASE("A star does not cross a directory separator")
{
    CHECK(IgnoresFile("*.log", "error.log"));
    CHECK(IgnoresFile("*.log", "logs/error.log"));
    CHECK_FALSE(IgnoresFile("logs/*.log", "logs/nested/error.log"));
}

TEST_CASE("A double star crosses directory separators")
{
    CHECK(IgnoresFile("logs/**/error.log", "logs/error.log"));
    CHECK(IgnoresFile("logs/**/error.log", "logs/a/b/error.log"));
    CHECK(IgnoresFile("**/vendor", "a/b/vendor"));
}

TEST_CASE("A question mark matches exactly one character")
{
    CHECK(IgnoresFile("file?.txt", "file1.txt"));
    CHECK_FALSE(IgnoresFile("file?.txt", "file10.txt"));
    CHECK_FALSE(IgnoresFile("file?.txt", "file.txt"));
}

TEST_CASE("A character class matches one of its members")
{
    CHECK(IgnoresFile("file[0-9].txt", "file7.txt"));
    CHECK_FALSE(IgnoresFile("file[0-9].txt", "filex.txt"));
    CHECK(IgnoresFile("file[abc].txt", "fileb.txt"));
}

TEST_CASE("A negation un-ignores what an earlier rule ignored")
{
    constexpr const char* rules = "*.log\n!keep.log\n";

    CHECK(IgnoresFile(rules, "error.log"));
    CHECK_FALSE(IgnoresFile(rules, "keep.log"));
}

TEST_CASE("The last matching rule wins, in file order")
{
    // Reversing the two rules reverses the outcome — this is what "last wins" means and
    // is why the patterns cannot be stored unordered.
    CHECK_FALSE(IgnoresFile("*.log\n!keep.log\n", "keep.log"));
    CHECK(IgnoresFile("!keep.log\n*.log\n", "keep.log"));
}

TEST_CASE("A pattern containing a slash is anchored even without a leading one")
{
    CHECK(IgnoresFile("src/main.cpp", "src/main.cpp"));
    CHECK_FALSE(IgnoresFile("src/main.cpp", "lib/src/main.cpp"));
}

TEST_CASE("Trailing whitespace is stripped unless escaped")
{
    CHECK(IgnoresFile("build   \n", "build"));
    CHECK(IgnoresFile("build\\ \n", "build "));
}

TEST_CASE("A whole run of escaped trailing spaces survives")
{
    // Stopping after the first escaped space would leave a stray backslash in the
    // pattern, so it would match a path containing a backslash and nothing else.
    CHECK(IgnoresFile("build\\ \\ \n", "build  "));
    CHECK_FALSE(IgnoresFile("build\\ \\ \n", "build\\  "));
}

TEST_CASE("A leading escape lets a pattern start with a hash or a bang")
{
    CHECK(IgnoresFile("\\#notacomment", "#notacomment"));
    CHECK(IgnoresFile("\\!literal", "!literal"));
}
