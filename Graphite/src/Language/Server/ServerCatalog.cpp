#include "Language/Server/ServerCatalog.h"

#include "Language/Registry/LanguageRegistry.h"

#include <array>
#include <system_error>

namespace Graphite {

	namespace {

		// build/compile_commands.json is here because clangd reads it under every ancestor of a file, and the trust check has to judge the directory clangd will read.
		constexpr std::array<std::string_view, 5> kClangdRootMarkers{
			"compile_commands.json", "compile_flags.txt", ".clangd", "build/compile_commands.json", ".git"};

		// --pch-storage=disk keeps preambles out of memory, which is what lets the background index hold several files at once.
		constexpr std::array<std::string_view, 2> kClangdArguments{"--background-index", "--pch-storage=disk"};

		constexpr std::array<ServerDefinition, 1> kServers{{
			{.Id = "clangd",
			 .DisplayName = "clangd",
			 .ExecutableName = "clangd",
			 .ProtocolLanguageId = "cpp",
			 .DefaultArguments = kClangdArguments,
			 .RootMarkers = kClangdRootMarkers,
			 .Adapter = AdapterKind::Clangd},
		}};

		// The error_code overload: a directory that denies access reads as "not there" instead of throwing across the boundary ADR 0004 guards.
		bool Exists(const std::filesystem::path& path)
		{
			std::error_code failure;
			return std::filesystem::exists(path, failure) && !failure;
		}

		bool HoldsAnyMarker(const std::filesystem::path& directory, std::span<const std::string_view> markers)
		{
			for (const std::string_view marker : markers)
			{
				if (Exists(directory / marker))
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
			if (HoldsAnyMarker(directory, server.RootMarkers))
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

	bool IsProjectRoot(const std::filesystem::path& directory)
	{
		for (const ServerDefinition& server : kServers)
		{
			if (HoldsAnyMarker(directory, server.RootMarkers))
			{
				return true;
			}
		}

		return false;
	}
}
