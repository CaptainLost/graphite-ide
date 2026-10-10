#include "Language/Server/ServerCatalog.h"

#include "Language/Registry/LanguageRegistry.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"

#include <array>
#include <system_error>

namespace Graphite {

	namespace {

		// build/compile_commands.json is here because clangd reads it under every ancestor of a file, and the trust check has to judge the directory clangd will read.
		constexpr std::array<std::string_view, 5> kClangdRootMarkers{
			"compile_commands.json", "compile_flags.txt", ".clangd", "build/compile_commands.json", ".git"};

		// --pch-storage=disk keeps preambles out of memory, which is what lets the background index hold several files at once.
		constexpr std::array<std::string_view, 2> kClangdArguments{"--background-index", "--pch-storage=disk"};

		// Only the build this was verified against; a port adds its platform's row once its asset name is checked.
		constexpr std::array<GitHubAsset, 1> kClangdAssets{{
			{.Platform = {.System = Core::OperatingSystem::Windows, .Machine = Core::Architecture::X64},
			 .AssetTemplate = "clangd-{os}-{version}.zip",
			 .Archive = ArchiveKind::Zip,
			 .ExecutableTemplate = "clangd_{version}/bin/clangd{exe}"},
		}};

		constexpr std::array<std::string_view, 3> kCSharpRootMarkers{"*.sln", "*.slnx", ".git"};

		// {logs} is the application's log directory, filled in when the server starts.
		constexpr std::array<std::string_view, 5> kRoslynArguments{"--stdio", "--logLevel", "Information",
																	"--extensionLogDirectory", "{logs}/roslyn"};

		constexpr std::array<std::string_view, 1> kOmniSharpArguments{"-lsp"};

		constexpr std::array<GitHubAsset, 1> kOmniSharpAssets{{
			{.Platform = {.System = Core::OperatingSystem::Windows, .Machine = Core::Architecture::X64},
			 .AssetTemplate = "omnisharp-{rid}.zip",
			 .Archive = ArchiveKind::Zip,
			 .ExecutableTemplate = "OmniSharp{exe}"},
		}};

		constexpr ServerRuntime kDotnet10{.Kind = RuntimeKind::Dotnet, .MinimumMajor = 10};

		constexpr std::array<ServerDefinition, 4> kServers{{
			{.Id = "clangd",
			 .DisplayName = "clangd",
			 .ExecutableName = "clangd",
			 .ProtocolLanguageId = "cpp",
			 .DefaultArguments = kClangdArguments,
			 .RootMarkers = kClangdRootMarkers,
			 .Adapter = AdapterKind::Clangd,
			 .Package = GitHubReleasePackage{.Repository = "clangd/clangd", .Assets = kClangdAssets}},
			{.Id = "roslyn",
			 .DisplayName = "Roslyn",
			 .ExecutableName = "roslyn-language-server",
			 .ProtocolLanguageId = "csharp",
			 .DefaultArguments = kRoslynArguments,
			 .RootMarkers = kCSharpRootMarkers,
			 .Adapter = AdapterKind::Plain,
			 .Package = NuGetToolPackage{.PackageIdTemplate = "roslyn-language-server.{rid}", .AllowPrerelease = true},
			 .Runtime = kDotnet10},
			{.Id = "csharp-ls",
			 .DisplayName = "csharp-ls",
			 .ExecutableName = "csharp-ls",
			 .ProtocolLanguageId = "csharp",
			 .RootMarkers = kCSharpRootMarkers,
			 .Adapter = AdapterKind::Plain,
			 .Package = NuGetToolPackage{.PackageIdTemplate = "csharp-ls", .AllowPrerelease = false},
			 .Runtime = kDotnet10},
			{.Id = "omnisharp",
			 .DisplayName = "OmniSharp",
			 .ExecutableName = "OmniSharp",
			 .ProtocolLanguageId = "csharp",
			 .DefaultArguments = kOmniSharpArguments,
			 .RootMarkers = kCSharpRootMarkers,
			 .Adapter = AdapterKind::Plain,
			 .Package = GitHubReleasePackage{.Repository = "OmniSharp/omnisharp-roslyn", .Assets = kOmniSharpAssets},
			 .Runtime = kDotnet10},
		}};

		// The error_code overload: a directory that denies access reads as "not there" instead of throwing across the boundary ADR 0004 guards.
		bool Exists(const std::filesystem::path& path)
		{
			std::error_code failure;
			return std::filesystem::exists(path, failure) && !failure;
		}

		constexpr std::string_view kExtensionPatternPrefix = "*.";

		bool IsExtensionPattern(std::string_view marker)
		{
			return marker.starts_with(kExtensionPatternPrefix);
		}

		bool MatchesExtensionPattern(std::string_view loweredExtension, std::span<const std::string_view> markers)
		{
			for (const std::string_view marker : markers)
			{
				// "*.sln" names the extension ".sln": the marker without its star.
				if (IsExtensionPattern(marker) && loweredExtension == Core::Lowered(marker.substr(1)))
				{
					return true;
				}
			}

			return false;
		}

		// Listed with the error_code overloads, so a directory that cannot be read holds nothing rather than throwing.
		bool HoldsFileMatchingPattern(const std::filesystem::path& directory, std::span<const std::string_view> markers)
		{
			std::error_code listError;
			std::filesystem::directory_iterator entries{directory, listError};

			for (; !listError && entries != std::filesystem::directory_iterator{}; entries.increment(listError))
			{
				std::error_code kindError;

				if (entries->is_regular_file(kindError) && MatchesExtensionPattern(Core::LoweredExtension(entries->path()), markers))
				{
					return true;
				}
			}

			return false;
		}
	}

	std::span<const ServerDefinition> AllServers()
	{
		return kServers;
	}

	const ServerDefinition* FindServer(std::string_view id)
	{
		for (const ServerDefinition& server : kServers)
		{
			if (server.Id == id)
			{
				return &server;
			}
		}

		return nullptr;
	}

	const ServerDefinition* RecommendedServerFor(LanguageId language)
	{
		const std::span<const std::string_view> servers = DefinitionOf(language).Servers;

		if (servers.empty())
		{
			return nullptr;
		}

		return FindServer(servers.front());
	}

	std::vector<std::string> DefaultArgumentsOf(const ServerDefinition& server)
	{
		std::vector<std::string> arguments;

		for (const std::string_view argument : server.DefaultArguments)
		{
			arguments.emplace_back(argument);
		}

		return arguments;
	}

	std::filesystem::path RootFor(const ServerDefinition& server, const std::filesystem::path& file,
								  const std::filesystem::path& fallbackRoot)
	{
		std::filesystem::path directory = file.parent_path();

		for (;;)
		{
			if (DirectoryHoldsAnyMarker(directory, server.RootMarkers))
			{
				return directory;
			}

			const std::filesystem::path parent = directory.parent_path();
			const bool reachedFilesystemRoot = parent == directory;

			if (reachedFilesystemRoot)
			{
				return fallbackRoot;
			}

			directory = parent;
		}
	}

	bool DirectoryHoldsAnyMarker(const std::filesystem::path& directory, std::span<const std::string_view> markers)
	{
		bool listsAPattern = false;

		for (const std::string_view marker : markers)
		{
			if (IsExtensionPattern(marker))
			{
				listsAPattern = true;
				continue;
			}

			if (Exists(directory / marker))
			{
				return true;
			}
		}

		return listsAPattern && HoldsFileMatchingPattern(directory, markers);
	}

	bool IsProjectRoot(const std::filesystem::path& directory)
	{
		for (const ServerDefinition& server : kServers)
		{
			if (DirectoryHoldsAnyMarker(directory, server.RootMarkers))
			{
				return true;
			}
		}

		return false;
	}
}
