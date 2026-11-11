#include "Wsl/WslServerInstalls.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/WslSharePath.h"
#include "Core/Platform/ChildProcess.h"

#include <chrono>
#include <format>
#include <optional>
#include <utility>

namespace Graphite {

	namespace {

		// A distro that is running answers at once; one that is not starts first.
		constexpr std::chrono::seconds kLongestChmod{60};
	}

	std::filesystem::path DistroServersDirectory(const std::string& distro, const std::string& linuxHome)
	{
		const std::string home = linuxHome.ends_with('/') ? linuxHome.substr(0, linuxHome.size() - 1) : linuxHome;
		return Core::ToWslSharePath(Core::WslSharePath{distro, home + "/.graphite/servers"});
	}

	ServerInstaller::MakeExecutableStep MakeExecutableInDistro(std::string distro, std::filesystem::path wslExecutable)
	{
		return [distro = std::move(distro), wslExecutable = std::move(wslExecutable)](const std::filesystem::path& program) -> Core::Result<void> {
			const std::optional<Core::WslSharePath> place = Core::ParseWslSharePath(program);

			if (!place || !Core::EqualsIgnoringCase(place->Distro, distro))
			{
				return Core::Error{Core::ErrorCode::InvalidArgument, std::format("{} is not in {}", Core::ToUtf8(program), distro)};
			}

			Core::Result<Core::Scope<Core::ChildProcess>> chmod =
				Core::ChildProcess::Start(wslExecutable, {"-d", distro, "--exec", "chmod", "+x", "--", place->LinuxPath}, wslExecutable.parent_path());

			if (!chmod)
			{
				return chmod.GetError();
			}

			if (!chmod.GetValue()->WaitForExit(kLongestChmod))
			{
				chmod.GetValue()->Terminate();
				return Core::Error{Core::ErrorCode::Cancelled, std::format("{} did not make {} executable in time", distro, place->LinuxPath)};
			}

			if (chmod.GetValue()->GetExitCode() != std::optional<std::uint32_t>{0})
			{
				return Core::Error{Core::ErrorCode::IoFailure, std::format("cannot make {} executable in {}", place->LinuxPath, distro)};
			}

			return Core::Result<void>::Ok();
		};
	}
}
