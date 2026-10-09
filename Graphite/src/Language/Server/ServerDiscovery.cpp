#include "Language/Server/ServerDiscovery.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/Process.h"
#include "Core/Platform/ShellProfiles.h"

#include <system_error>

namespace Graphite {

	namespace {

		// The error_code overload, never the throwing one. Every path tested below comes from
		// outside this function — a value the user typed into graphite.json, the executable's own
		// directory, an entry of PATH — and a disconnected drive or an unreachable UNC share makes
		// the throwing overload raise a filesystem_error out through StartServer, across a boundary
		// ADR 0004 says nothing throws across. "Cannot tell" reads as "not there", which is what an
		// unexaminable candidate means to a search looking for one. Same helper, same reasoning, as
		// ServerCatalog.cpp's; not shared, because a one-line wrapper is cheaper to repeat than a
		// header for it.
		bool Exists(const std::filesystem::path& path)
		{
			std::error_code failed;
			return std::filesystem::exists(path, failed) && !failed;
		}

		// Appends ".exe" unless `command` already names one — callers pass a bare command name
		// like "clangd", but nothing stops a settings value from already carrying the extension.
		std::filesystem::path ExecutableNameFor(std::string_view command)
		{
			std::filesystem::path name{command};

			if (Core::LoweredExtension(name) != ".exe")
			{
				name += ".exe";
			}

			return name;
		}
	}

	Core::Result<std::filesystem::path> FindServerBinary(std::string_view command,
														 const std::filesystem::path& configured)
	{
		if (!configured.empty() && Exists(configured))
		{
			return configured;
		}

		const std::filesystem::path executableName = ExecutableNameFor(command);

		if (const auto exeDir = Core::ExecutableDirectory())
		{
			if (const std::filesystem::path beside = exeDir.GetValue() / executableName;
				Exists(beside))
			{
				return beside;
			}
		}

		// Through Core's search, which reads PATH wide: the narrow one turned every character
		// outside the ANSI code page into '?', and a server installed under such a directory was
		// never found.
		if (std::filesystem::path onPath = Core::FindOnPath(Core::ToUtf8(executableName)); !onPath.empty())
		{
			return onPath;
		}

		return Core::Error{Core::ErrorCode::NotFound, "server binary not found: " + std::string{command}};
	}
}
