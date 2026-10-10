#include "Language/Install/ServerInstaller.h"

#include "Language/Install/Checksum.h"
#include "Language/Install/GitHubReleaseSource.h"
#include "Language/Install/InstalledServer.h"
#include "Language/Install/NuGetSource.h"
#include "Language/Install/NuGetVersion.h"
#include "Language/Install/ResolvedPackage.h"
#include "Language/Install/ToolSettings.h"

#include "Core/Base/Assert.h"
#include "Core/Base/ScopeExit.h"
#include "Core/FileSystem/FileLock.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/ZipArchive.h"
#include "Core/Jobs/CancellationToken.h"

#include <chrono>
#include <format>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <system_error>
#include <utility>
#include <variant>

namespace Graphite {

	struct InstallerState
	{
		std::mutex Mutex;
		std::vector<InstallProgress> Changes;
		std::map<std::string, Core::CancellationToken, std::less<>> Running;
	};

	namespace {

		constexpr std::string_view kPartialSuffix = ".part";

		struct InstallJob
		{
			InstallJobKind Kind = InstallJobKind::Install;
			const ServerDefinition* Server = nullptr;
			Core::Ref<Core::HttpClient> Http;
			std::filesystem::path ServerDirectory;
			Core::PlatformTarget Platform;
			Core::CancellationToken Token;
		};

		InstallProgress ChangeOf(const InstallJob& job, InstallStep step, std::string_view version = {})
		{
			return InstallProgress{.ServerId = std::string{job.Server->Id}, .Step = step, .Version = std::string{version}};
		}

		// Consecutive download progress for one server collapses into the latest, so a long download does not queue
		// a change per piece between two frames.
		void Report(InstallerState& state, InstallProgress change)
		{
			const std::scoped_lock lock{state.Mutex};
			const bool continuesADownload = change.Step == InstallStep::Downloading && !state.Changes.empty() &&
											state.Changes.back().ServerId == change.ServerId &&
											state.Changes.back().Step == InstallStep::Downloading;

			if (continuesADownload)
			{
				state.Changes.back() = std::move(change);
				return;
			}

			state.Changes.push_back(std::move(change));
		}

		// One step under the lock, so IsBusy turns false exactly when the outcome can be drained.
		void Finish(InstallerState& state, InstallProgress outcome)
		{
			const std::scoped_lock lock{state.Mutex};
			const std::string serverId = outcome.ServerId;
			state.Changes.push_back(std::move(outcome));
			state.Running.erase(serverId);
		}

		InstallProgress OutcomeOf(const InstallJob& job, const Core::Error& error)
		{
			const bool wasCancelled = error.Code == Core::ErrorCode::Cancelled;
			InstallProgress outcome = ChangeOf(job, wasCancelled ? InstallStep::Cancelled : InstallStep::Failed);
			outcome.Failure = wasCancelled ? std::string{} : error.Message;
			return outcome;
		}

		void RemoveQuietly(const std::filesystem::path& path)
		{
			std::error_code ignoredError;
			std::filesystem::remove_all(path, ignoredError);
		}

		Core::Error CancelledError(const InstallJob& job)
		{
			return Core::Error{.Code = Core::ErrorCode::Cancelled,
							   .Message = std::format("installing {} was cancelled", job.Server->DisplayName)};
		}

		std::string NowInUtc()
		{
			const std::chrono::sys_seconds now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
			return std::format("{:%FT%TZ}", now);
		}

		Core::Result<ResolvedPackage> ResolveLatest(const InstallJob& job)
		{
			if (const GitHubReleasePackage* release = std::get_if<GitHubReleasePackage>(&job.Server->Package))
			{
				return ResolveGitHubRelease(*job.Http, *release, job.Platform);
			}

			const NuGetToolPackage* tool = std::get_if<NuGetToolPackage>(&job.Server->Package);
			CORE_ASSERT(tool != nullptr, "a server's package is a GitHub release or a NuGet tool");
			return ResolveNuGetTool(*job.Http, *tool, job.Platform);
		}

		Core::Result<Core::FileLock> LockServer(const InstallJob& job)
		{
			std::error_code creationError;
			std::filesystem::create_directories(job.ServerDirectory, creationError);

			if (creationError)
			{
				return Core::Error{.Code = Core::ErrorCode::IoFailure,
								   .Message = std::format("cannot create '{}': {}", Core::ToUtf8(job.ServerDirectory),
														  creationError.message())};
			}

			Core::Result<Core::FileLock> lock = Core::FileLock::TryAcquire(LockFileOf(job.ServerDirectory));

			if (!lock && lock.GetError().Code == Core::ErrorCode::AccessDenied)
			{
				return Core::Error{.Code = Core::ErrorCode::AccessDenied,
								   .Message = std::format("another Graphite window is installing {}", job.Server->DisplayName)};
			}

			return lock;
		}

		Core::Result<InstalledCommand> CommandOf(const ResolvedPackage& package, const std::filesystem::path& stagingDirectory,
												 Core::PlatformTarget platform)
		{
			if (package.ExecutableInArchive.empty())
			{
				return FindToolCommand(stagingDirectory, platform);
			}

			std::error_code statusError;
			const std::filesystem::path executable = stagingDirectory / Core::FromUtf8(package.ExecutableInArchive);

			if (!std::filesystem::is_regular_file(executable, statusError))
			{
				return Core::Error{.Code = Core::ErrorCode::NotFound,
								   .Message = std::format("{} has no {}", package.FileName, package.ExecutableInArchive)};
			}

			return InstalledCommand{.Runner = LaunchRunner::Executable, .RelativePath = package.ExecutableInArchive};
		}

		// A directory of this version left by an uninstall whose files were in use is replaced, never merged into.
		Core::Result<void> PlaceVersion(const std::filesystem::path& stagingDirectory, const std::filesystem::path& versionDirectory)
		{
			std::error_code placementError;
			std::filesystem::remove_all(versionDirectory, placementError);

			if (!placementError)
			{
				std::filesystem::rename(stagingDirectory, versionDirectory, placementError);
			}

			if (placementError)
			{
				return Core::Error{.Code = Core::ErrorCode::IoFailure,
								   .Message = std::format("cannot put the new version at '{}': {}; a server running from "
														  "there may still hold its files",
														  Core::ToUtf8(versionDirectory), placementError.message())};
			}

			return Core::Result<void>::Ok();
		}

		InstallProgress InstalledOutcome(const InstallJob& job, std::string_view version, bool hasChecksum)
		{
			InstallProgress outcome = ChangeOf(job, InstallStep::Installed, version);
			outcome.HasChecksum = hasChecksum;
			return outcome;
		}

		Core::Result<InstallProgress> InstallLatest(const InstallJob& job, InstallerState& state)
		{
			const Core::Result<Core::FileLock> lock = LockServer(job);

			if (!lock)
			{
				return lock.GetError();
			}

			Report(state, ChangeOf(job, InstallStep::Resolving));
			const Core::Result<ResolvedPackage> resolved = ResolveLatest(job);

			if (!resolved)
			{
				return resolved.GetError();
			}

			const ResolvedPackage& package = resolved.GetValue();

			if (package.Archive != ArchiveKind::Zip)
			{
				return Core::Error{.Code = Core::ErrorCode::Unsupported,
								   .Message = std::format("{} comes as a .tar.gz archive, which this build cannot extract yet",
														  job.Server->DisplayName)};
			}

			if (const std::optional<InstalledServer> installed = FindInstalledServer(job.ServerDirectory);
				installed && installed->Record.Version == package.Version)
			{
				return InstalledOutcome(job, package.Version, installed->Record.VerifiedChecksum.has_value());
			}

			const std::filesystem::path downloadDirectory = DownloadDirectoryOf(job.ServerDirectory);
			const std::filesystem::path stagingDirectory = StagingDirectoryOf(job.ServerDirectory, package.Version);

			// Runs on success too: a finished install leaves only its version directory and its record.
			const Core::ScopeExit removeWorkingFolders{[&downloadDirectory, &stagingDirectory] {
				RemoveQuietly(downloadDirectory);
				RemoveQuietly(stagingDirectory);
			}};

			std::error_code creationError;
			std::filesystem::create_directories(downloadDirectory, creationError);

			if (creationError)
			{
				return Core::Error{.Code = Core::ErrorCode::IoFailure,
								   .Message = std::format("cannot create '{}': {}", Core::ToUtf8(downloadDirectory),
														  creationError.message())};
			}

			const std::filesystem::path archive = downloadDirectory / Core::FromUtf8(package.FileName + std::string{kPartialSuffix});
			const Core::DownloadObserver reportDownload = [&state, &job, &package](const Core::DownloadProgress& progress) {
				InstallProgress change = ChangeOf(job, InstallStep::Downloading, package.Version);
				change.ReceivedBytes = progress.ReceivedBytes;
				change.TotalBytes = progress.TotalBytes;
				Report(state, std::move(change));
			};

			const Core::Result<void> downloaded = job.Http->Download({.Url = package.DownloadUrl}, archive, reportDownload, job.Token);

			if (!downloaded)
			{
				return downloaded.GetError();
			}

			if (package.ExpectedChecksum)
			{
				Report(state, ChangeOf(job, InstallStep::Verifying, package.Version));
				const Core::Result<void> verified = VerifyChecksum(archive, *package.ExpectedChecksum);

				if (!verified)
				{
					return verified.GetError();
				}
			}

			if (job.Token.IsCancelled())
			{
				return CancelledError(job);
			}

			Report(state, ChangeOf(job, InstallStep::Extracting, package.Version));
			RemoveQuietly(stagingDirectory);
			const Core::Result<void> extracted = Core::ExtractZip(archive, stagingDirectory, job.Token);

			if (!extracted)
			{
				return extracted.GetError();
			}

			const Core::Result<InstalledCommand> command = CommandOf(package, stagingDirectory, job.Platform);

			if (!command)
			{
				return command.GetError();
			}

			const Core::Result<void> placed = PlaceVersion(stagingDirectory, job.ServerDirectory / Core::FromUtf8(package.Version));

			if (!placed)
			{
				return placed.GetError();
			}

			const InstallRecord record{.Version = package.Version,
									   .SourceUrl = package.DownloadUrl,
									   .VerifiedChecksum = package.ExpectedChecksum,
									   .InstalledAt = NowInUtc(),
									   .Command = command.GetValue()};
			const Core::Result<void> recorded = WriteInstallRecord(job.ServerDirectory, record);

			if (!recorded)
			{
				return recorded.GetError();
			}

			return InstalledOutcome(job, package.Version, package.ExpectedChecksum.has_value());
		}

		Core::Result<InstallProgress> CheckLatest(const InstallJob& job, InstallerState& state)
		{
			Report(state, ChangeOf(job, InstallStep::Resolving));
			const Core::Result<ResolvedPackage> resolved = ResolveLatest(job);

			if (!resolved)
			{
				return resolved.GetError();
			}

			const std::string& latest = resolved.GetValue().Version;
			const std::optional<InstallRecord> record = ReadInstallRecord(job.ServerDirectory);

			if (record && !IsNewerRelease(job.Server->Package, latest, record->Version))
			{
				return ChangeOf(job, InstallStep::UpToDate, record->Version);
			}

			return ChangeOf(job, InstallStep::UpdateAvailable, latest);
		}

		Core::Result<InstallProgress> UninstallServer(const InstallJob& job)
		{
			const Core::Result<Core::FileLock> lock = LockServer(job);

			if (!lock)
			{
				return lock.GetError();
			}

			const std::optional<InstallRecord> record = ReadInstallRecord(job.ServerDirectory);
			std::error_code removalError;
			std::filesystem::remove(RecordFileOf(job.ServerDirectory), removalError);

			if (removalError)
			{
				return Core::Error{.Code = Core::ErrorCode::IoFailure,
								   .Message = std::format("cannot remove '{}': {}", Core::ToUtf8(RecordFileOf(job.ServerDirectory)),
														  removalError.message())};
			}

			if (!record)
			{
				return ChangeOf(job, InstallStep::Uninstalled);
			}

			// Files a running server still holds stay behind for the start-up sweep.
			RemoveQuietly(job.ServerDirectory / Core::FromUtf8(record->Version));
			return ChangeOf(job, InstallStep::Uninstalled, record->Version);
		}

		Core::Result<InstallProgress> RunJob(const InstallJob& job, InstallerState& state)
		{
			switch (job.Kind)
			{
			case InstallJobKind::Install:
				return InstallLatest(job, state);
			case InstallJobKind::CheckForUpdates:
				return CheckLatest(job, state);
			case InstallJobKind::Uninstall:
				break;
			}

			return UninstallServer(job);
		}

		void RunAndFinish(const InstallJob& job, const Core::Ref<InstallerState>& state)
		{
			const Core::Result<InstallProgress> outcome = RunJob(job, *state);
			Finish(*state, outcome ? outcome.GetValue() : OutcomeOf(job, outcome.GetError()));
		}
	}

	bool IsFinished(InstallStep step)
	{
		switch (step)
		{
		case InstallStep::Resolving:
		case InstallStep::Downloading:
		case InstallStep::Verifying:
		case InstallStep::Extracting:
			return false;
		case InstallStep::Installed:
		case InstallStep::UpToDate:
		case InstallStep::UpdateAvailable:
		case InstallStep::Uninstalled:
		case InstallStep::Cancelled:
		case InstallStep::Failed:
			break;
		}

		return true;
	}

	bool IsNewerRelease(const ServerPackage& package, std::string_view latest, std::string_view installed)
	{
		if (std::holds_alternative<NuGetToolPackage>(package))
		{
			const std::optional<NuGetVersion> latestVersion = NuGetVersion::Parse(latest);
			const std::optional<NuGetVersion> installedVersion = NuGetVersion::Parse(installed);

			if (latestVersion && installedVersion)
			{
				return *installedVersion < *latestVersion;
			}
		}

		return latest != installed;
	}

	ServerInstaller::ServerInstaller(Core::Ref<Core::HttpClient> http, std::filesystem::path serversDirectory,
									 Core::PlatformTarget platform, Core::JobSubmitter submit)
		: m_State(Core::CreateRef<InstallerState>()),
		  m_Http(std::move(http)),
		  m_ServersDirectory(std::move(serversDirectory)),
		  m_Platform(platform),
		  m_Submit(std::move(submit))
	{
	}

	ServerInstaller::~ServerInstaller()
	{
		const std::scoped_lock lock{m_State->Mutex};

		for (const std::pair<const std::string, Core::CancellationToken>& running : m_State->Running)
		{
			running.second.Cancel();
		}
	}

	bool ServerInstaller::Install(const ServerDefinition& server)
	{
		return Start(InstallJobKind::Install, server);
	}

	bool ServerInstaller::CheckForUpdates(const ServerDefinition& server)
	{
		return Start(InstallJobKind::CheckForUpdates, server);
	}

	bool ServerInstaller::Uninstall(const ServerDefinition& server)
	{
		return Start(InstallJobKind::Uninstall, server);
	}

	void ServerInstaller::Cancel(std::string_view serverId)
	{
		const std::scoped_lock lock{m_State->Mutex};
		const std::map<std::string, Core::CancellationToken, std::less<>>::const_iterator running =
			m_State->Running.find(serverId);

		if (running != m_State->Running.end())
		{
			running->second.Cancel();
		}
	}

	bool ServerInstaller::IsBusy(std::string_view serverId) const
	{
		const std::scoped_lock lock{m_State->Mutex};
		return m_State->Running.contains(serverId);
	}

	void ServerInstaller::Drain(std::vector<InstallProgress>& changes)
	{
		const std::scoped_lock lock{m_State->Mutex};
		changes.insert(changes.end(), std::make_move_iterator(m_State->Changes.begin()),
					   std::make_move_iterator(m_State->Changes.end()));
		m_State->Changes.clear();
	}

	bool ServerInstaller::Start(InstallJobKind kind, const ServerDefinition& server)
	{
		const Core::CancellationToken token;

		{
			const std::scoped_lock lock{m_State->Mutex};

			if (m_State->Running.contains(server.Id))
			{
				return false;
			}

			m_State->Running.emplace(std::string{server.Id}, token);
		}

		const InstallJob job{.Kind = kind,
							 .Server = &server,
							 .Http = m_Http,
							 .ServerDirectory = ServerDirectoryOf(m_ServersDirectory, server.Id),
							 .Platform = m_Platform,
							 .Token = token};
		const Core::Ref<InstallerState> state = m_State;

		// Submitted outside the lock: a submitter that runs the job where it is submitted reports through the same mutex.
		m_Submit([job, state] { RunAndFinish(job, state); });
		return true;
	}
}
