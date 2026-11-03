#include <doctest/doctest.h>

#include "Workspace/WorkspaceTrust.h"

#include "TempDirectory.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
	std::string ReadText(const std::filesystem::path& file)
	{
		std::ifstream input(file, std::ios::binary);
		return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
	}
}

TEST_CASE("A new trust list trusts nothing")
{
	const Graphite::WorkspaceTrust trust;

	CHECK_FALSE(trust.IsTrusted("D:\\Repos"));
	CHECK(trust.GetEntries().empty());
}

TEST_CASE("A trusted folder trusts itself and everything inside it")
{
	Graphite::WorkspaceTrust trust;
	trust.Trust("D:\\Repos");

	CHECK(trust.IsTrusted("D:\\Repos"));
	CHECK(trust.IsTrusted("D:\\Repos\\IDE"));
	CHECK(trust.IsTrusted("D:\\Repos\\IDE\\Core\\src"));
	CHECK_FALSE(trust.IsTrusted("D:\\Other"));
}

TEST_CASE("Trust ignores case, slash direction and a trailing separator")
{
	Graphite::WorkspaceTrust trust;
	trust.Trust("D:/Repos/");

	CHECK(trust.IsTrusted("d:\\repos"));
	CHECK(trust.IsTrusted("D:\\REPOS\\ide\\"));
	CHECK(trust.GetEntries().front() == std::filesystem::path{"D:\\Repos"});
}

TEST_CASE("A sibling whose name starts with a trusted folder's is not trusted")
{
	Graphite::WorkspaceTrust trust;
	trust.Trust("D:\\Repos");

	CHECK_FALSE(trust.IsTrusted("D:\\Repos2"));
	CHECK_FALSE(trust.IsTrusted("D:\\Repos2\\IDE"));
}

TEST_CASE("A trusted drive root trusts every folder on the drive")
{
	Graphite::WorkspaceTrust trust;
	trust.Trust("D:\\");

	CHECK(trust.IsTrusted("D:\\Anything\\At\\All"));
	CHECK_FALSE(trust.IsTrusted("E:\\Anything"));
}

TEST_CASE("A relative or empty path is never trusted and never becomes an entry")
{
	Graphite::WorkspaceTrust trust;
	trust.Trust("Repos");
	trust.Trust("");

	CHECK(trust.GetEntries().empty());
	CHECK_FALSE(trust.IsTrusted(""));
	CHECK_FALSE(trust.IsTrusted("Repos"));
}

TEST_CASE("TrustedThrough names the entry that grants the trust")
{
	Graphite::WorkspaceTrust trust;
	trust.Trust("D:\\Repos");

	CHECK(trust.TrustedThrough("D:\\Repos\\IDE") == std::filesystem::path{"D:\\Repos"});
	CHECK_FALSE(trust.TrustedThrough("D:\\Other").has_value());
}

TEST_CASE("IsEntry tells a listed folder from one trusted through a parent")
{
	Graphite::WorkspaceTrust trust;
	trust.Trust("D:\\Repos");

	CHECK(trust.IsEntry("d:/repos/"));
	CHECK_FALSE(trust.IsEntry("D:\\Repos\\IDE"));
}

TEST_CASE("Trusting a listed folder again changes nothing")
{
	Graphite::WorkspaceTrust trust;
	trust.Trust("D:\\Repos");
	const std::uint64_t revision = trust.GetRevision();

	trust.Trust("d:\\repos\\");

	CHECK(trust.GetEntries().size() == 1);
	CHECK(trust.GetRevision() == revision);
}

TEST_CASE("Revoking an entry, however it is spelled, removes it and moves the revision")
{
	Graphite::WorkspaceTrust trust;
	trust.Trust("D:\\Repos");
	const std::uint64_t revision = trust.GetRevision();

	trust.Revoke("d:/repos");

	CHECK(trust.GetEntries().empty());
	CHECK_FALSE(trust.IsTrusted("D:\\Repos\\IDE"));
	CHECK(trust.GetRevision() > revision);
}

TEST_CASE("A trusted folder that is renamed stays trusted under its new name and not under the old one")
{
	Graphite::WorkspaceTrust trust;
	trust.Trust("D:\\Repos\\IDE");
	const std::uint64_t revision = trust.GetRevision();

	trust.FollowFolderMove("D:\\Repos\\IDE", "D:\\Repos\\Graphite");

	CHECK(trust.IsTrusted("D:\\Repos\\Graphite"));
	CHECK_FALSE(trust.IsTrusted("D:\\Repos\\IDE"));
	CHECK(trust.GetEntries().size() == 1);
	CHECK(trust.GetRevision() > revision);
}

TEST_CASE("A folder trusted inside a moved folder moves with it")
{
	Graphite::WorkspaceTrust trust;
	trust.Trust("D:\\Work\\Project\\Tools");

	trust.FollowFolderMove("d:/work/project", "D:\\Work\\Renamed");

	CHECK(trust.IsTrusted("D:\\Work\\Renamed\\Tools"));
	CHECK_FALSE(trust.IsTrusted("D:\\Work\\Project\\Tools"));
	CHECK_FALSE(trust.IsTrusted("D:\\Work\\Renamed"));
}

TEST_CASE("A folder trusted through a parent stays trusted when it moves out from under that parent")
{
	Graphite::WorkspaceTrust trust;
	trust.Trust("D:\\Repos");

	trust.FollowFolderMove("D:\\Repos\\IDE", "D:\\Elsewhere\\IDE");

	CHECK(trust.IsTrusted("D:\\Elsewhere\\IDE"));
	CHECK_FALSE(trust.IsTrusted("D:\\Elsewhere"));
	CHECK(trust.IsEntry("D:\\Repos"));
}

TEST_CASE("A folder that stays under the parent it is trusted through adds no entry when it moves")
{
	Graphite::WorkspaceTrust trust;
	trust.Trust("D:\\Repos");
	const std::uint64_t revision = trust.GetRevision();

	trust.FollowFolderMove("D:\\Repos\\IDE", "D:\\Repos\\Graphite");

	CHECK(trust.IsTrusted("D:\\Repos\\Graphite"));
	CHECK(trust.GetEntries().size() == 1);
	CHECK(trust.GetRevision() == revision);
}

TEST_CASE("An untrusted folder that moves stays untrusted")
{
	Graphite::WorkspaceTrust trust;
	trust.Trust("D:\\Other");
	const std::uint64_t revision = trust.GetRevision();

	trust.FollowFolderMove("D:\\Repos\\IDE", "D:\\Repos\\Graphite");

	CHECK_FALSE(trust.IsTrusted("D:\\Repos\\Graphite"));
	CHECK(trust.GetEntries().size() == 1);
	CHECK(trust.GetRevision() == revision);
}

TEST_CASE("A folder's trust carried by a move is written to the trust file")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "trusted-folders.json";

	{
		Graphite::WorkspaceTrust trust{file};
		trust.Trust("D:\\Repos\\IDE");
		trust.FollowFolderMove("D:\\Repos\\IDE", "D:\\Repos\\Graphite");
	}

	const Graphite::WorkspaceTrust reread{file};

	CHECK(reread.IsTrusted("D:\\Repos\\Graphite"));
	CHECK_FALSE(reread.IsTrusted("D:\\Repos\\IDE"));
}

TEST_CASE("The trust list survives being written and read back")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "trusted-folders.json";

	{
		Graphite::WorkspaceTrust trust{file};
		trust.Trust("D:\\Repos");
		trust.Trust("C:\\Work\\Zażółć");
	}

	const Graphite::WorkspaceTrust reread{file};

	CHECK(reread.IsTrusted("D:\\Repos\\IDE"));
	CHECK(reread.IsTrusted("C:\\Work\\Zażółć\\src"));
	CHECK(reread.GetEntries().size() == 2);
}

TEST_CASE("An unreadable trust file trusts nothing")
{
	TempDirectory temp;
	temp.MakeFile("trusted-folders.json", "{ this is not json");

	const Graphite::WorkspaceTrust trust{temp.GetPath() / "trusted-folders.json"};

	CHECK(trust.GetEntries().empty());
}

TEST_CASE("An unreadable trust file is kept as .bak before the first change writes over it")
{
	TempDirectory temp;
	const std::string broken = "{ this is not json";
	temp.MakeFile("trusted-folders.json", broken);
	const std::filesystem::path file = temp.GetPath() / "trusted-folders.json";

	Graphite::WorkspaceTrust trust{file};
	trust.Trust("E:\\Else");

	CHECK(ReadText(temp.GetPath() / "trusted-folders.json.bak") == broken);

	const Graphite::WorkspaceTrust reread{file};
	CHECK(reread.IsTrusted("E:\\Else"));
}

TEST_CASE("A trust file from a newer version is read but never written")
{
	TempDirectory temp;
	const std::string newer = R"({"version": 99, "folders": ["D:\\Repos"]})";
	temp.MakeFile("trusted-folders.json", newer);
	const std::filesystem::path file = temp.GetPath() / "trusted-folders.json";

	Graphite::WorkspaceTrust trust{file};
	trust.Trust("E:\\Else");

	CHECK(trust.IsTrusted("D:\\Repos\\IDE"));
	CHECK(trust.IsTrusted("E:\\Else"));
	CHECK(ReadText(file) == newer);
}

TEST_CASE("A trust file that exists but cannot be opened is never written")
{
	TempDirectory temp;
	const std::string listed = R"({"version": 1, "folders": ["D:\\Repos"]})";
	temp.MakeFile("trusted-folders.json", listed);
	const std::filesystem::path file = temp.GetPath() / "trusted-folders.json";

	const HANDLE held = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
									FILE_ATTRIBUTE_NORMAL, nullptr);
	REQUIRE(held != INVALID_HANDLE_VALUE);

	Graphite::WorkspaceTrust trust{file};
	CloseHandle(held);

	trust.Trust("E:\\Else");

	CHECK(trust.IsTrusted("E:\\Else"));
	CHECK(ReadText(file) == listed);
}

TEST_CASE("The parent offered for trust is the folder's own parent")
{
	CHECK(Graphite::ParentWorthTrusting("D:\\Downloads\\evil-repo") ==
		  std::filesystem::path{"D:\\Downloads"});
	CHECK(Graphite::ParentWorthTrusting("D:/Downloads/evil-repo/") ==
		  std::filesystem::path{"D:\\Downloads"});
}

TEST_CASE("No parent is offered when it would be a drive root")
{
	CHECK_FALSE(Graphite::ParentWorthTrusting("D:\\evil-repo").has_value());
	CHECK_FALSE(Graphite::ParentWorthTrusting("D:\\").has_value());
	CHECK_FALSE(Graphite::ParentWorthTrusting("relative\\folder").has_value());
}
