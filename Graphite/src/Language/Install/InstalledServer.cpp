#include "Language/Install/InstalledServer.h"

#include "Language/Install/InstallJson.h"
#include "Language/Install/PackageNames.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileLock.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/RelativePath.h"

#include <cstdint>
#include <system_error>
#include <vector>

namespace Graphite {

	namespace {

		constexpr std::uintmax_t kLargestRecordBytes = 64 * 1024;
		constexpr int kRecordIndent = 2;
		constexpr std::int64_t kRecordFormat = 1;
		constexpr std::string_view kDownloadDirectoryName = ".download";

		void RemoveQuietly(const std::filesystem::path& path)
		{
			std::error_code ignoredError;
			std::filesystem::remove_all(path, ignoredError);
		}

		// Absent or null means the source published none; anything else must read as a checksum.
		Core::Result<std::optional<Checksum>> ChecksumIn(const nlohmann::json& root)
		{
			const nlohmann::json::const_iterator field = root.find("checksum");

			if (field == root.end() || field->is_null())
			{
				return std::optional<Checksum>{};
			}

			std::optional<Checksum> checksum = field->is_string() ? ParseChecksum(field->get<std::string>()) : std::nullopt;

			if (!checksum)
			{
				return Core::Error{.Code = Core::ErrorCode::InvalidArgument,
								   .Message = "installed.json holds a checksum that cannot be read"};
			}

			return checksum;
		}

		enum class RecordFormat
		{
			Current,
			Newer,
			Unknown
		};

		// A record without a format predates the field and is read as the first format.
		RecordFormat FormatOf(const nlohmann::json& root)
		{
			const nlohmann::json::const_iterator field = root.find("format");

			if (field == root.end())
			{
				return RecordFormat::Current;
			}

			if (field->is_number_integer() && field->get<std::int64_t>() == kRecordFormat)
			{
				return RecordFormat::Current;
			}

			if (field->is_number() && field->get<double>() > static_cast<double>(kRecordFormat))
			{
				return RecordFormat::Newer;
			}

			return RecordFormat::Unknown;
		}

		std::optional<InstallRecord> RecordFrom(const nlohmann::json& root)
		{
			const std::optional<std::string> version = TextAt(root, "version");
			const std::optional<std::string> source = TextAt(root, "source");
			const std::optional<std::string> installedAt = TextAt(root, "installedAt");
			const nlohmann::json* command = ObjectAt(root, "command");

			if (FormatOf(root) != RecordFormat::Current || !version || !source || !installedAt || command == nullptr ||
				!IsSafeDirectoryName(*version))
			{
				return std::nullopt;
			}

			const std::optional<std::string> runnerName = TextAt(*command, "runner");
			const std::optional<std::string> commandPath = TextAt(*command, "path");
			const std::optional<LaunchRunner> runner = runnerName ? RunnerNamed(*runnerName) : std::nullopt;

			if (!runner || !commandPath || !Core::IsSafeRelativePath(*commandPath))
			{
				return std::nullopt;
			}

			Core::Result<std::optional<Checksum>> checksum = ChecksumIn(root);

			if (!checksum)
			{
				return std::nullopt;
			}

			return InstallRecord{.Version = *version,
								 .SourceUrl = *source,
								 .VerifiedChecksum = std::move(checksum).GetValue(),
								 .InstalledAt = *installedAt,
								 .Command = {.Runner = *runner, .RelativePath = *commandPath}};
		}

		enum class RecordState
		{
			AbsentOrInvalid,
			Valid,
			Unreadable
		};

		struct RecordOutcome
		{
			RecordState State = RecordState::AbsentOrInvalid;
			std::optional<InstallRecord> Record;
		};

		RecordOutcome ReadRecordOutcome(const std::filesystem::path& serverDirectory)
		{
			const Core::Result<std::string> text = Core::ReadFileBytes(RecordFileOf(serverDirectory), kLargestRecordBytes);

			if (!text)
			{
				const bool isAbsent = text.GetError().Code == Core::ErrorCode::NotFound;
				return {.State = isAbsent ? RecordState::AbsentOrInvalid : RecordState::Unreadable, .Record = std::nullopt};
			}

			const nlohmann::json root = ParseJsonText(text.GetValue());

			// A newer Graphite's record names versions this build cannot tell apart from leftovers.
			if (FormatOf(root) == RecordFormat::Newer)
			{
				return {.State = RecordState::Unreadable, .Record = std::nullopt};
			}

			std::optional<InstallRecord> record = RecordFrom(root);
			const RecordState state = record ? RecordState::Valid : RecordState::AbsentOrInvalid;

			return {.State = state, .Record = std::move(record)};
		}

		bool IsWorkingLeftover(const Core::DirectoryEntry& entry, const std::filesystem::path& serverDirectory)
		{
			return IsTemporaryName(entry.Name) || (entry.IsDirectory && entry.Path == DownloadDirectoryOf(serverDirectory));
		}

		bool IsLeftover(const Core::DirectoryEntry& entry, const std::filesystem::path& serverDirectory,
						const RecordOutcome& outcome)
		{
			if (outcome.State == RecordState::Unreadable)
			{
				return IsWorkingLeftover(entry, serverDirectory);
			}

			if (!entry.IsDirectory)
			{
				return IsTemporaryName(entry.Name);
			}

			const bool isRecordedVersion =
				outcome.Record.has_value() && Core::EqualsIgnoringCase(entry.Name, outcome.Record->Version);

			return !isRecordedVersion;
		}

		void SweepServerDirectory(const std::filesystem::path& serverDirectory)
		{
			const Core::Result<Core::FileLock> lock = Core::FileLock::TryAcquire(LockFileOf(serverDirectory));

			if (!lock)
			{
				return;
			}

			const RecordOutcome outcome = ReadRecordOutcome(serverDirectory);
			const Core::Result<std::vector<Core::DirectoryEntry>> entries = Core::ReadDirectory(serverDirectory);

			if (!entries)
			{
				return;
			}

			for (const Core::DirectoryEntry& entry : entries.GetValue())
			{
				if (IsLeftover(entry, serverDirectory, outcome))
				{
					RemoveQuietly(entry.Path);
				}
			}
		}
	}

	std::filesystem::path ServerDirectoryOf(const std::filesystem::path& serversDirectory, std::string_view serverId)
	{
		return serversDirectory / Core::FromUtf8(serverId);
	}

	std::filesystem::path RecordFileOf(const std::filesystem::path& serverDirectory)
	{
		return serverDirectory / Core::FromUtf8(kRecordFileName);
	}

	std::filesystem::path LockFileOf(const std::filesystem::path& serverDirectory)
	{
		return serverDirectory / Core::FromUtf8(kLockFileName);
	}

	std::filesystem::path DownloadDirectoryOf(const std::filesystem::path& serverDirectory)
	{
		return serverDirectory / Core::FromUtf8(kDownloadDirectoryName);
	}

	std::filesystem::path StagingDirectoryOf(const std::filesystem::path& serverDirectory, std::string_view version)
	{
		return serverDirectory / Core::FromUtf8(TemporaryNameOf(version));
	}

	std::string SerialiseInstallRecord(const InstallRecord& record)
	{
		nlohmann::json command = nlohmann::json::object();
		command["runner"] = std::string{RunnerName(record.Command.Runner)};
		command["path"] = record.Command.RelativePath;

		nlohmann::json root = nlohmann::json::object();
		root["format"] = kRecordFormat;
		root["version"] = record.Version;
		root["source"] = record.SourceUrl;
		root["checksum"] = record.VerifiedChecksum ? nlohmann::json(DescribeChecksum(*record.VerifiedChecksum))
												   : nlohmann::json(nullptr);
		root["installedAt"] = record.InstalledAt;
		root["command"] = command;

		return root.dump(kRecordIndent, ' ', false, nlohmann::json::error_handler_t::replace);
	}

	std::optional<InstallRecord> ParseInstallRecord(std::string_view json)
	{
		return RecordFrom(ParseJsonText(json));
	}

	Core::Result<void> WriteInstallRecord(const std::filesystem::path& serverDirectory, const InstallRecord& record)
	{
		return Core::WriteFileAtomically(RecordFileOf(serverDirectory), SerialiseInstallRecord(record));
	}

	std::optional<InstallRecord> ReadInstallRecord(const std::filesystem::path& serverDirectory)
	{
		const Core::Result<std::string> text = Core::ReadFileBytes(RecordFileOf(serverDirectory), kLargestRecordBytes);

		if (!text)
		{
			return std::nullopt;
		}

		return ParseInstallRecord(text.GetValue());
	}

	std::optional<InstalledServer> FindInstalledServer(const std::filesystem::path& serverDirectory)
	{
		std::optional<InstallRecord> record = ReadInstallRecord(serverDirectory);

		if (!record)
		{
			return std::nullopt;
		}

		const std::filesystem::path command =
			serverDirectory / Core::FromUtf8(record->Version) / Core::FromUtf8(record->Command.RelativePath);
		std::error_code statusError;

		if (!std::filesystem::is_regular_file(command, statusError))
		{
			return std::nullopt;
		}

		return InstalledServer{.Record = std::move(*record), .Command = command};
	}

	void SweepServersDirectory(const std::filesystem::path& serversDirectory)
	{
		const Core::Result<std::vector<Core::DirectoryEntry>> servers = Core::ReadDirectory(serversDirectory);

		if (!servers)
		{
			return;
		}

		for (const Core::DirectoryEntry& server : servers.GetValue())
		{
			if (server.IsDirectory)
			{
				SweepServerDirectory(server.Path);
			}
		}
	}
}
