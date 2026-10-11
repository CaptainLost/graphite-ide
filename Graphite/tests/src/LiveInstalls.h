#pragma once

#include "Language/Install/InstalledServer.h"
#include "Language/Install/ServerInstaller.h"
#include "Language/Server/ServerCatalog.h"

#include "Core/Network/HttpClient.h"
#include "Core/Platform/CurrentPlatform.h"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

struct LiveInstall
{
	std::vector<Graphite::InstallProgress> Changes;
	std::optional<Graphite::InstalledServer> Installed;
};

// Downloads from the server's release channel: only for cases marked doctest::skip().
inline LiveInstall InstallForReal(const Graphite::ServerDefinition& server, const std::filesystem::path& servers)
{
	Core::Result<Core::Scope<Core::HttpClient>> created = Core::CreateHttpClient("Graphite-tests");
	REQUIRE(created);
	const Core::Ref<Core::HttpClient> http{std::move(created).GetValue()};
	Graphite::ServerInstaller installer{http, servers, Core::CurrentPlatform(), [](std::function<void()> job) { job(); }};
	const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

	REQUIRE(installer.Install(server));

	LiveInstall install;
	installer.Drain(install.Changes);
	const std::chrono::seconds took = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started);
	MESSAGE(std::format("{} installed in {} s", server.Id, took.count()));
	install.Installed = Graphite::FindInstalledServer(Graphite::ServerDirectoryOf(servers, server.Id));
	return install;
}

inline Graphite::InstalledServer RequireInstalled(const Graphite::ServerDefinition& server, const std::filesystem::path& servers)
{
	LiveInstall install = InstallForReal(server, servers);

	REQUIRE_FALSE(install.Changes.empty());
	INFO(install.Changes.back().Failure);
	REQUIRE(install.Changes.back().Step == Graphite::InstallStep::Installed);
	REQUIRE(install.Installed.has_value());

	return std::move(*install.Installed);
}
