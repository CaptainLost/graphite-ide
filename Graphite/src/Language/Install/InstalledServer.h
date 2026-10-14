#pragma once

#include "Language/Install/Checksum.h"
#include "Language/Install/ToolSettings.h"

#include "Core/Base/Result.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	struct InstallRecord
	{
		std::string Version;
		std::string SourceUrl;
		std::optional<Checksum> VerifiedChecksum;
		std::string InstalledAt;
		InstalledCommand Command;

		bool operator==(const InstallRecord&) const = default;
	};

	struct InstalledServer
	{
		InstallRecord Record;
		std::filesystem::path Command;
	};

	std::filesystem::path ServerDirectoryOf(const std::filesystem::path& serversDirectory, std::string_view serverId);

	std::filesystem::path RecordFileOf(const std::filesystem::path& serverDirectory);

	std::filesystem::path LockFileOf(const std::filesystem::path& serverDirectory);

	std::filesystem::path DownloadDirectoryOf(const std::filesystem::path& serverDirectory);

	std::filesystem::path StagingDirectoryOf(const std::filesystem::path& serverDirectory, std::string_view version);

	std::string SerialiseInstallRecord(const InstallRecord& record);

	// WARNING: A record whose version or command could lead outside the server's directory reads as nothing, like a broken one,
	// and so does a record a newer Graphite wrote in a later format.
	std::optional<InstallRecord> ParseInstallRecord(std::string_view json);

	Core::Result<void> WriteInstallRecord(const std::filesystem::path& serverDirectory, const InstallRecord& record);

	std::optional<InstallRecord> ReadInstallRecord(const std::filesystem::path& serverDirectory);

	std::optional<InstalledServer> FindInstalledServer(const std::filesystem::path& serverDirectory);

	// WARNING: Renamed to a temporary name before anything inside is deleted, so a directory with a file in use stays whole and
	// answers false; whatever the deletion leaves under the temporary name, the next sweep removes.
	bool RemoveDirectoryWhole(const std::filesystem::path& directory);

	// WARNING: A server whose lock another Graphite window holds is left alone: its download and its half-extracted version are
	// still in use.
	void SweepServersDirectory(const std::filesystem::path& serversDirectory);
}
