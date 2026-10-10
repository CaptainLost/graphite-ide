#include <doctest/doctest.h>

#include "Core/FileSystem/FileLock.h"
#include "Core/FileSystem/FileSystem.h"

#include "TempDirectory.h"

#include <filesystem>
#include <utility>

namespace {

	std::filesystem::path LockFileIn(TempDirectory& temporaryDirectory)
	{
		const std::filesystem::path folder = temporaryDirectory.GetPath() / Core::FromUtf8("serwery Ω");
		std::filesystem::create_directories(folder);
		return folder / "install.lock";
	}
}

TEST_CASE("A held lock refuses a second holder until it is released")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path lockFile = LockFileIn(temporaryDirectory);

	{
		const Core::Result<Core::FileLock> first = Core::FileLock::TryAcquire(lockFile);
		REQUIRE(first);

		const Core::Result<Core::FileLock> second = Core::FileLock::TryAcquire(lockFile);
		REQUIRE_FALSE(second);
		CHECK(second.GetError().Code == Core::ErrorCode::AccessDenied);
	}

	CHECK(Core::FileLock::TryAcquire(lockFile));
}

TEST_CASE("A released lock leaves no file behind")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path lockFile = LockFileIn(temporaryDirectory);

	{
		const Core::Result<Core::FileLock> held = Core::FileLock::TryAcquire(lockFile);
		REQUIRE(held);
		CHECK(std::filesystem::exists(lockFile));
	}

	CHECK_FALSE(std::filesystem::exists(lockFile));
}

TEST_CASE("A lock moved to a new owner is released once by that owner")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path lockFile = LockFileIn(temporaryDirectory);
	Core::Result<Core::FileLock> acquired = Core::FileLock::TryAcquire(lockFile);
	REQUIRE(acquired);

	{
		const Core::FileLock owner = std::move(acquired).GetValue();
		CHECK_FALSE(Core::FileLock::TryAcquire(lockFile));
	}

	CHECK(Core::FileLock::TryAcquire(lockFile));
}
