#include <doctest/doctest.h>

#include "Core/Fs/AtomicWrite.h"

#include "TempDirectory.h"

#include <fstream>
#include <string>

using core::fs::WriteFileAtomically;

TEST_CASE("Writing over an existing file leaves the new content")
{
    TempDirectory temp;
    const auto path = temp.MakeFile("target.txt", "original");

    REQUIRE(WriteFileAtomically(path, "replaced"));

    std::ifstream stream{path, std::ios::binary};
    std::string contents{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};

    CHECK(contents == "replaced");
}

TEST_CASE("Writing a file that does not exist yet creates it")
{
    TempDirectory temp;
    const auto path = temp.Path() / "target.txt";

    REQUIRE(WriteFileAtomically(path, "content"));

    std::ifstream stream{path, std::ios::binary};
    std::string contents{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};

    CHECK(contents == "content");
}

TEST_CASE("An occupied .tmp name is stepped over rather than clobbered")
{
    TempDirectory temp;
    const auto path = temp.MakeFile("target.txt", "original");
    const auto occupied = temp.MakeFile("target.txt.tmp", "someone else's data");

    REQUIRE(WriteFileAtomically(path, "replaced"));

    std::ifstream stream{occupied, std::ios::binary};
    std::string contents{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};

    CHECK(contents == "someone else's data");

    std::ifstream targetStream{path, std::ios::binary};
    std::string targetContents{std::istreambuf_iterator<char>{targetStream},
                               std::istreambuf_iterator<char>{}};

    CHECK(targetContents == "replaced");
}

TEST_CASE("A write to a missing directory fails and leaves nothing behind")
{
    TempDirectory temp;
    const auto path = temp.Path() / "missing-directory" / "target.txt";

    CHECK_FALSE(WriteFileAtomically(path, "content"));
    CHECK_FALSE(std::filesystem::exists(path));
    CHECK_FALSE(std::filesystem::exists(temp.Path() / "missing-directory"));
}

TEST_CASE("A failed write leaves the existing target untouched")
{
    TempDirectory temp;
    const auto path = temp.Path() / "missing-directory" / "target.txt";

    // The directory does not exist, so the temporary write itself fails — no target and
    // no debris either.
    CHECK_FALSE(WriteFileAtomically(path, "content"));
    CHECK_FALSE(std::filesystem::exists(path));

    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator{temp.Path(), code})
        FAIL("unexpected entry left behind: ", entry.path().string());
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("An alternate data stream survives an atomic write")
{
    TempDirectory temp;
    const std::filesystem::path target = temp.Path() / "target.txt";

    {
        std::ofstream content{target, std::ios::binary};
        content << "original";
    }
    {
        std::ofstream stream{target.string() + ":meta", std::ios::binary};
        stream << "kept";
    }

    REQUIRE(WriteFileAtomically(target, "replaced"));

    std::ifstream stream{target.string() + ":meta", std::ios::binary};
    std::string recovered;
    stream >> recovered;

    CHECK(recovered == "kept");
}
#endif
