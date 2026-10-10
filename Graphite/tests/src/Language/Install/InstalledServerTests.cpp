#include <doctest/doctest.h>

#include "Language/Install/InstallJson.h"
#include "Language/Install/InstalledServer.h"

#include "Core/Base/ScopeExit.h"
#include "Core/FileSystem/FileLock.h"
#include "Core/FileSystem/FileSystem.h"

#include "TempDirectory.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>
#include <string>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace {

	Graphite::InstallRecord ClangdRecord()
	{
		return Graphite::InstallRecord{
			.Version = "23.1.0",
			.SourceUrl = "https://github.com/clangd/clangd/releases/download/23.1.0/clangd-windows-23.1.0.zip",
			.VerifiedChecksum =
				Graphite::ParseChecksum("sha256:23412a240756a162e7b98a282f36aa2a23a88db5ce16a0cbc4fef7253768c810"),
			.InstalledAt = "2026-10-10T12:00:00Z",
			.Command = {.Runner = Graphite::LaunchRunner::Executable, .RelativePath = "clangd_23.1.0/bin/clangd.exe"}};
	}

	std::string ClangdRecordWith(const char* field, const nlohmann::json& value)
	{
		nlohmann::json root = Graphite::ParseJsonText(Graphite::SerialiseInstallRecord(ClangdRecord()));
		root[field] = value;
		return root.dump();
	}

	void MakeServerFile(const std::filesystem::path& file)
	{
		std::filesystem::create_directories(file.parent_path());
		std::ofstream{file, std::ios::binary} << "the server";
	}
}

TEST_CASE("A record written is read back whole")
{
	const Graphite::InstallRecord record = ClangdRecord();

	CHECK(Graphite::ParseInstallRecord(Graphite::SerialiseInstallRecord(record)) == record);

	Graphite::InstallRecord withoutChecksum = record;
	withoutChecksum.VerifiedChecksum.reset();
	withoutChecksum.Command = {.Runner = Graphite::LaunchRunner::Dotnet,
							   .RelativePath = "tools/net10.0/any/CSharpLanguageServer.dll"};

	CHECK(Graphite::ParseInstallRecord(Graphite::SerialiseInstallRecord(withoutChecksum)) == withoutChecksum);
	CHECK(Graphite::SerialiseInstallRecord(withoutChecksum).find("\"checksum\": null") != std::string::npos);
}

TEST_CASE("A malformed or unsafe record reads as nothing")
{
	CHECK_FALSE(Graphite::ParseInstallRecord("not json").has_value());
	CHECK_FALSE(Graphite::ParseInstallRecord("{}").has_value());
	CHECK_FALSE(Graphite::ParseInstallRecord(ClangdRecordWith("version", "../23.1.0")).has_value());
	CHECK_FALSE(Graphite::ParseInstallRecord(ClangdRecordWith("checksum", "md5:d41d8cd98f00b204e9800998ecf8427e")).has_value());
	CHECK_FALSE(Graphite::ParseInstallRecord(ClangdRecordWith("checksum", 42)).has_value());

	nlohmann::json climbingCommand = nlohmann::json::object();
	climbingCommand["runner"] = "executable";
	climbingCommand["path"] = "../../evil.exe";
	CHECK_FALSE(Graphite::ParseInstallRecord(ClangdRecordWith("command", climbingCommand)).has_value());

	nlohmann::json unknownRunner = nlohmann::json::object();
	unknownRunner["runner"] = "node";
	unknownRunner["path"] = "server.js";
	CHECK_FALSE(Graphite::ParseInstallRecord(ClangdRecordWith("command", unknownRunner)).has_value());
}

TEST_CASE("An installed server is found by its record and its command on disk")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path serverDirectory = temporaryDirectory.GetPath() / Core::FromUtf8("Zażółć Ω") / "clangd";
	const std::filesystem::path executable = serverDirectory / "23.1.0" / "clangd_23.1.0" / "bin" / "clangd.exe";
	MakeServerFile(executable);
	REQUIRE(Graphite::WriteInstallRecord(serverDirectory, ClangdRecord()));

	const std::optional<Graphite::InstalledServer> installed = Graphite::FindInstalledServer(serverDirectory);

	REQUIRE(installed.has_value());
	CHECK(installed->Command == executable);
	CHECK(installed->Record == ClangdRecord());

	std::filesystem::remove(executable);
	CHECK_FALSE(Graphite::FindInstalledServer(serverDirectory).has_value());
}

TEST_CASE("No record or a corrupt one reads as nothing installed")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path serverDirectory = temporaryDirectory.GetPath() / "clangd";
	MakeServerFile(serverDirectory / "23.1.0" / "clangd_23.1.0" / "bin" / "clangd.exe");

	CHECK_FALSE(Graphite::FindInstalledServer(serverDirectory).has_value());

	temporaryDirectory.MakeFile("clangd/installed.json", "{\"version\": \"23.1.0\"");
	CHECK_FALSE(Graphite::FindInstalledServer(serverDirectory).has_value());
}

TEST_CASE("The sweep keeps the recorded version and removes everything else left behind")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path servers = temporaryDirectory.GetPath() / "servers";
	const std::filesystem::path clangd = Graphite::ServerDirectoryOf(servers, "clangd");
	MakeServerFile(clangd / "23.1.0" / "clangd_23.1.0" / "bin" / "clangd.exe");
	MakeServerFile(clangd / "22.1.0" / "clangd_22.1.0" / "bin" / "clangd.exe");
	MakeServerFile(clangd / "24.0.0.tmp" / "half.exe");
	MakeServerFile(clangd / ".download" / "clangd-windows-24.0.0.zip.part");
	MakeServerFile(clangd / "installed.json.tmp");
	MakeServerFile(clangd / "installed.json.tmp1");
	REQUIRE(Graphite::WriteInstallRecord(clangd, ClangdRecord()));

	Graphite::SweepServersDirectory(servers);

	CHECK(std::filesystem::exists(clangd / "23.1.0" / "clangd_23.1.0" / "bin" / "clangd.exe"));
	CHECK(std::filesystem::exists(clangd / "installed.json"));
	CHECK_FALSE(std::filesystem::exists(clangd / "22.1.0"));
	CHECK_FALSE(std::filesystem::exists(clangd / "24.0.0.tmp"));
	CHECK_FALSE(std::filesystem::exists(clangd / ".download"));
	CHECK_FALSE(std::filesystem::exists(clangd / "installed.json.tmp"));
	CHECK_FALSE(std::filesystem::exists(clangd / "installed.json.tmp1"));
	CHECK_FALSE(std::filesystem::exists(clangd / "install.lock"));
}

TEST_CASE("The installer's working folders are ones the sweep clears")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path servers = temporaryDirectory.GetPath() / "servers";
	const std::filesystem::path clangd = Graphite::ServerDirectoryOf(servers, "clangd");

	CHECK(Graphite::DownloadDirectoryOf(clangd) == clangd / ".download");
	CHECK(Graphite::StagingDirectoryOf(clangd, "24.0.0") == clangd / "24.0.0.tmp");

	MakeServerFile(Graphite::DownloadDirectoryOf(clangd) / "clangd-windows-24.0.0.zip.part");
	MakeServerFile(Graphite::StagingDirectoryOf(clangd, "24.0.0") / "half.exe");
	MakeServerFile(clangd / "23.1.0" / "clangd_23.1.0" / "bin" / "clangd.exe");
	REQUIRE(Graphite::WriteInstallRecord(clangd, ClangdRecord()));

	Graphite::SweepServersDirectory(servers);

	CHECK_FALSE(std::filesystem::exists(Graphite::DownloadDirectoryOf(clangd)));
	CHECK_FALSE(std::filesystem::exists(Graphite::StagingDirectoryOf(clangd, "24.0.0")));
	CHECK(std::filesystem::exists(clangd / "23.1.0"));
}

TEST_CASE("The sweep removes every version of a server whose record is corrupt")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path servers = temporaryDirectory.GetPath() / "servers";
	const std::filesystem::path clangd = Graphite::ServerDirectoryOf(servers, "clangd");
	MakeServerFile(clangd / "23.1.0" / "clangd_23.1.0" / "bin" / "clangd.exe");
	temporaryDirectory.MakeFile("servers/clangd/installed.json", "garbage");

	Graphite::SweepServersDirectory(servers);

	CHECK_FALSE(std::filesystem::exists(clangd / "23.1.0"));
}

TEST_CASE("The sweep leaves alone a server another Graphite window is installing")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path servers = temporaryDirectory.GetPath() / "servers";
	const std::filesystem::path clangd = Graphite::ServerDirectoryOf(servers, "clangd");
	MakeServerFile(clangd / "24.0.0.tmp" / "half.exe");
	MakeServerFile(clangd / ".download" / "clangd-windows-24.0.0.zip.part");
	const Core::Result<Core::FileLock> otherWindow = Core::FileLock::TryAcquire(Graphite::LockFileOf(clangd));
	REQUIRE(otherWindow);

	Graphite::SweepServersDirectory(servers);

	CHECK(std::filesystem::exists(clangd / "24.0.0.tmp" / "half.exe"));
	CHECK(std::filesystem::exists(clangd / ".download" / "clangd-windows-24.0.0.zip.part"));
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("The sweep keeps every version while the record cannot be read")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path servers = temporaryDirectory.GetPath() / "servers";
	const std::filesystem::path clangd = Graphite::ServerDirectoryOf(servers, "clangd");
	MakeServerFile(clangd / "23.1.0" / "clangd_23.1.0" / "bin" / "clangd.exe");
	MakeServerFile(clangd / "22.1.0" / "clangd_22.1.0" / "bin" / "clangd.exe");
	MakeServerFile(clangd / "24.0.0.tmp" / "half.exe");
	MakeServerFile(clangd / ".download" / "clangd-windows-24.0.0.zip.part");
	REQUIRE(Graphite::WriteInstallRecord(clangd, ClangdRecord()));

	const HANDLE exclusiveHandle = ::CreateFileW(Graphite::RecordFileOf(clangd).c_str(), GENERIC_READ, 0, nullptr,
												 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	REQUIRE(exclusiveHandle != INVALID_HANDLE_VALUE);
	const Core::ScopeExit closeHandle{[exclusiveHandle] { ::CloseHandle(exclusiveHandle); }};

	Graphite::SweepServersDirectory(servers);

	CHECK(std::filesystem::exists(clangd / "23.1.0"));
	CHECK(std::filesystem::exists(clangd / "22.1.0"));
	CHECK_FALSE(std::filesystem::exists(clangd / "24.0.0.tmp"));
	CHECK_FALSE(std::filesystem::exists(clangd / ".download"));
}
#endif
