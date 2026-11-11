#pragma once

#include "Language/Install/ServerInstaller.h"
#include "Language/Install/ServerInstalls.h"

#include "Core/Base/Pointers.h"
#include "Core/Jobs/JobSubmitter.h"
#include "Core/Network/HttpClient.h"

#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace Graphite {

	class WslAgents;

	// Language-server installs into each WSL distro a folder was opened in (stage 6b): made once the
	// distro's agent has said where its home is, never waiting for it.
	class DistroServerInstalls
	{
	public:
		using HttpMaker = std::function<Core::Ref<Core::HttpClient>()>;

		DistroServerInstalls(WslAgents& agents, HttpMaker makeHttp, Core::JobSubmitter submit);

		// None while the distro's agent is starting or has failed.
		ServerInstalls* For(const std::string& distro);

		// Once a frame: apply(installs, distro) for each distro's installs, and firstRead(installs)
		// once, when what a distro has installed has been read.
		void Poll(const std::function<void(ServerInstalls&, const std::string&)>& apply, const std::function<void(ServerInstalls&)>& firstRead);

	private:
		WslAgents& m_Agents;
		HttpMaker m_MakeHttp;
		Core::JobSubmitter m_Submit;
		std::map<std::string, Core::Scope<ServerInstalls>, std::less<>> m_Installs;
		std::set<std::string, std::less<>> m_Read;
	};

	// Where language servers are installed into a distro: <home>/.graphite/servers, beside the
	// agent's own ~/.graphite/agent, named by its share path, which the installer writes through.
	std::filesystem::path DistroServersDirectory(const std::string& distro, const std::string& linuxHome);

	// chmod +x in the distro, through wsl.exe: a file written through the share is not executable
	// (measured on WSL 2). A path that is not in that distro fails.
	ServerInstaller::MakeExecutableStep MakeExecutableInDistro(std::string distro, std::filesystem::path wslExecutable);
}
