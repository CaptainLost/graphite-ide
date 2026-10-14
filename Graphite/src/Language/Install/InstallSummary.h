#pragma once

#include "Language/Install/ServerInstaller.h"
#include "Language/Install/ServerInstalls.h"
#include "Language/Server/ServerCatalog.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	enum class InstallAction : std::uint8_t
	{
		Install,
		Cancel,
		CheckForUpdates,
		Update,
		Uninstall
	};

	struct InstallInputs
	{
		bool AvailableOnPlatform = true;
		bool CanDownload = true;
		bool HasReadInstalls = true;
		std::optional<InstalledVersion> Installed;
		std::optional<InstallJobKind> Running;
		bool Cancelling = false;
		std::optional<InstallProgress> Latest;
	};

	struct InstallSummary
	{
		std::string State;
		std::string Note;
		std::string Failure;
		std::vector<InstallAction> Actions;
	};

	InstallInputs InstallInputsFor(const ServerInstalls& installs, const ServerDefinition& server);

	// WARNING: Takes no server state on purpose: an untrusted folder holds its server at Restricted, where installing is still allowed.
	InstallSummary SummariseInstall(const InstallInputs& inputs);

	std::string InstallProgressText(const InstallProgress& progress);

	std::optional<int> InstallPercent(const InstallProgress& progress);
}
