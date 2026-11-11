#include "Wsl/WslServerInstalls.h"

#include "Wsl/WslAgents.h"

#include "Core/Remote/AgentDeployment.h"

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

	DistroServerInstalls::DistroServerInstalls(WslAgents& agents, HttpMaker makeHttp, Core::JobSubmitter submit)
		: m_Agents(agents), m_MakeHttp(std::move(makeHttp)), m_Submit(std::move(submit))
	{
	}

	ServerInstalls* DistroServerInstalls::For(const std::string& distro)
	{
		if (const auto known = m_Installs.find(distro); known != m_Installs.end())
		{
			return known->second.get();
		}

		// Never waits: asked again on a later frame while the distro's agent starts.
		const Core::Result<std::shared_ptr<Core::AgentConnection>> agent = m_Agents.Peek(distro);

		if (!agent || agent.GetValue()->GetHome().empty())
		{
			return nullptr;
		}

		constexpr Core::PlatformTarget kDistroPlatform{.System = Core::OperatingSystem::Linux, .Machine = Core::Architecture::X64};
		auto installs = Core::CreateScope<ServerInstalls>(m_MakeHttp(), DistroServersDirectory(distro, agent.GetValue()->GetHome()), m_Submit,
														  kDistroPlatform, MakeExecutableInDistro(distro, Core::WslExecutable()));
		installs->SweepAtStartup();

		return m_Installs.emplace(distro, std::move(installs)).first->second.get();
	}

	void DistroServerInstalls::Poll(const std::function<void(ServerInstalls&, const std::string&)>& apply,
									const std::function<void(ServerInstalls&)>& firstRead)
	{
		for (const auto& [distro, installs] : m_Installs)
		{
			apply(*installs, distro);

			if (installs->HasReadInstalls() && m_Read.insert(distro).second)
			{
				firstRead(*installs);
			}
		}
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
