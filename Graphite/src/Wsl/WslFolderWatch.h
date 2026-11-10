#pragma once

#include "Core/Base/Base.h"
#include "Core/FileSystem/FolderWatch.h"
#include "Core/FileSystem/WslSharePath.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace Graphite {

	class WslAgents;

	// A folder inside WSL watched by the distro's agent with inotify (ADR 0696): the agent's
	// "changes" events for this watch, their Linux folders turned into the share paths the host
	// names them by. It runs from Start until the agent stops or cannot be reached; the workspace
	// then starts another, which reconnects.
	class WslFolderWatch final : public Core::FolderWatch
	{
	public:
		// Returns at once; the watch is asked for on a thread of its own once the agent is ready.
		static Core::Scope<WslFolderWatch> Start(WslAgents& agents, Core::WslSharePath root);

		~WslFolderWatch() override;

		WslFolderWatch(const WslFolderWatch&) = delete;
		WslFolderWatch& operator=(const WslFolderWatch&) = delete;

		Core::FolderChanges TakeSettled() override;
		bool IsRunning() const override;

	private:
		struct Shared;

		WslFolderWatch(WslAgents& agents, Core::WslSharePath root);

		WslAgents& m_Agents;
		std::shared_ptr<Shared> m_Shared;
		std::thread m_Starter;
	};
}
