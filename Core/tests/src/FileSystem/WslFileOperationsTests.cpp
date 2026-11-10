#include <doctest/doctest.h>

#include "Core/Base/PlatformDetection.h"
#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileOperations.h"
#include "Core/FileSystem/WslFileOperations.h"

#include "TempDirectory.h"

#include <string>
#include <vector>

// Writes under a WSL share go to whatever WslFileOperations is registered (ADR 0696). Windows
// only: on Linux a path never names a WSL share.
#ifdef CORE_PLATFORM_WINDOWS

namespace {

	class RecordingOperations final : public Core::WslFileOperations
	{
	public:
		std::vector<std::string> Calls;

		Core::Result<void> WriteFile(const Core::WslSharePath& place, std::string_view bytes) override
		{
			Calls.push_back("write " + place.Distro + " " + place.LinuxPath + " " + std::string{bytes});
			return Core::Result<void>::Ok();
		}

		Core::Result<void> Rename(const Core::WslSharePath& from, const Core::WslSharePath& to) override
		{
			Calls.push_back("rename " + from.LinuxPath + " " + to.LinuxPath);
			return Core::Result<void>::Ok();
		}

		Core::Result<void> MakeDirectory(const Core::WslSharePath& place) override
		{
			Calls.push_back("mkdir " + place.LinuxPath);
			return Core::Result<void>::Ok();
		}

		Core::Result<void> CreateEmptyFile(const Core::WslSharePath& place) override
		{
			Calls.push_back("create " + place.LinuxPath);
			return Core::Result<void>::Ok();
		}

		Core::Result<void> MoveToTrash(const Core::WslSharePath& place) override
		{
			Calls.push_back("trash " + place.LinuxPath);
			return Core::Result<void>::Ok();
		}
	};

	// Registers the operations for one test and takes them away after it.
	struct Registered
	{
		explicit Registered(Core::WslFileOperations* operations) { Core::SetWslFileOperations(operations); }
		~Registered() { Core::SetWslFileOperations(nullptr); }
	};
}

TEST_CASE("A write under a WSL share goes to the registered operations as a place in the distro")
{
	RecordingOperations recorded;
	Registered registered{&recorded};

	REQUIRE(Core::WriteFileAtomically(L"\\\\wsl.localhost\\Ubuntu\\tmp\\a.txt", "x").HasValue());

	REQUIRE(recorded.Calls.size() == 1);
	CHECK(recorded.Calls.front() == "write Ubuntu /tmp/a.txt x");
}

TEST_CASE("Renames, folders, new files and deletions under a WSL share go to the registered operations")
{
	RecordingOperations recorded;
	Registered registered{&recorded};

	CHECK(Core::Rename(L"\\\\wsl.localhost\\Ubuntu\\a", L"\\\\wsl$\\Ubuntu\\b").HasValue());
	CHECK(Core::MakeDirectory(L"\\\\wsl.localhost\\Ubuntu\\dir").HasValue());
	CHECK(Core::CreateEmptyFile(L"\\\\wsl.localhost\\Ubuntu\\new.txt").HasValue());
	CHECK(Core::MoveToRecycleBin(L"\\\\wsl.localhost\\Ubuntu\\old.txt").HasValue());

	CHECK(recorded.Calls == std::vector<std::string>{"rename /a /b", "mkdir /dir", "create /new.txt", "trash /old.txt"});
}

TEST_CASE("Without registered operations a write under a WSL share is refused, naming the distro")
{
	const Core::Result<void> written = Core::WriteFileAtomically(L"\\\\wsl.localhost\\Ubuntu\\tmp\\a.txt", "x");

	REQUIRE_FALSE(written.HasValue());
	CHECK(written.GetError().Code == Core::ErrorCode::Unsupported);
	CHECK(written.GetError().Message.find("Ubuntu") != std::string::npos);
}

TEST_CASE("A local write never reaches the WSL operations")
{
	RecordingOperations recorded;
	Registered registered{&recorded};
	TempDirectory directory;

	REQUIRE(Core::WriteFileAtomically(directory.GetPath() / "local.txt", "x").HasValue());

	CHECK(recorded.Calls.empty());
}

TEST_CASE("A rename between WSL and a local folder, or between two distros, is refused")
{
	RecordingOperations recorded;
	Registered registered{&recorded};

	CHECK(Core::Rename(L"\\\\wsl.localhost\\Ubuntu\\a", L"D:\\a").GetError().Code == Core::ErrorCode::Unsupported);
	CHECK(Core::Rename(L"\\\\wsl.localhost\\Ubuntu\\a", L"\\\\wsl.localhost\\Debian\\a").GetError().Code ==
		  Core::ErrorCode::Unsupported);
	CHECK(recorded.Calls.empty());
}

#endif
