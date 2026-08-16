#include "Lang/Server/ServerDiscovery.h"

#include "Core/Base/Ascii.h"
#include "Core/Platform/Process.h"

#include <cstdlib>
#include <memory>

namespace Graphite {

	namespace {

		// Appends ".exe" unless `command` already names one — callers pass a bare command name
		// like "clangd", but nothing stops a settings value from already carrying the extension.
		std::filesystem::path ExecutableNameFor(std::string_view command)
		{
			std::filesystem::path name{command};

			if (Core::Lowered(name.extension().string()) != ".exe")
			{
				name += ".exe";
			}

			return name;
		}
	}

	Core::Result<std::filesystem::path> FindServerBinary(std::string_view command,
														   const std::filesystem::path& configured)
	{
		if (!configured.empty() && std::filesystem::exists(configured))
		{
			return configured;
		}

		const std::filesystem::path executableName = ExecutableNameFor(command);

		if (const auto exeDir = Core::ExecutableDirectory())
		{
			if (const std::filesystem::path beside = exeDir.GetValue() / executableName;
				std::filesystem::exists(beside))
			{
				return beside;
			}
		}

		// _dupenv_s rather than std::getenv: the CRT's "safe" variants are what this repository
		// builds with (fatalwarnings turns std::getenv's C4996 into a build error), and this one
		// hands back an owned buffer instead of a pointer into CRT-managed static storage.
		char* pathEnv = nullptr;
		std::size_t pathEnvLength = 0;
		const std::unique_ptr<char, decltype(&std::free)> pathEnvOwner{
			(_dupenv_s(&pathEnv, &pathEnvLength, "PATH") == 0) ? pathEnv : nullptr, &std::free};

		if (pathEnvOwner)
		{
			const std::string_view path{pathEnvOwner.get()};
			std::size_t start = 0;

			while (start <= path.size())
			{
				std::size_t end = path.find(';', start);
				if (end == std::string_view::npos)
				{
					end = path.size();
				}

				if (const std::string_view entry = path.substr(start, end - start); !entry.empty())
				{
					if (const std::filesystem::path candidate = std::filesystem::path{entry} / executableName;
						std::filesystem::exists(candidate))
					{
						return candidate;
					}
				}

				start = end + 1;
			}
		}

		return Core::Error{Core::ErrorCode::NotFound, "server binary not found: " + std::string{command}};
	}
}
