#pragma once

#include "Core/Base/Result.h"
#include "Core/FileSystem/WslFileOperations.h"
#include "Core/Remote/AgentConnection.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace Graphite {

	// One graphite-agent per WSL distro (ADR 0696), started on a worker thread the first time the
	// distro is needed and again after it stopped. It is also how Core writes under a WSL share:
	// registered as Core's WslFileOperations, each change becomes a request to the distro's agent.
	class WslAgents final : public Core::WslFileOperations
	{
	public:
		using Connector = std::function<Core::Result<std::shared_ptr<Core::AgentConnection>>(const std::string& distro)>;
		using EventListener = std::function<void(const std::string& distro, const Core::AgentEvent& event)>;
		using ListenerId = std::uint64_t;

		explicit WslAgents(Connector connect);

		// Waits for every connection being made.
		~WslAgents() override;

		WslAgents(const WslAgents&) = delete;
		WslAgents& operator=(const WslAgents&) = delete;

		// Installs the agent beside the executable into the distro and starts it there.
		static Connector ConnectThroughWsl(std::filesystem::path agentBinary);

		// Starts connecting to the distro in the background unless an agent is there or on its way.
		void Prepare(const std::string& distro);

		// The distro's ready agent, waiting up to `wait` for one being made.
		Core::Result<std::shared_ptr<Core::AgentConnection>> Get(const std::string& distro, std::chrono::milliseconds wait);

		// Every event of every distro's agent, on the agent's reader thread.
		ListenerId AddListener(EventListener listener);
		void RemoveListener(ListenerId id);

		Core::Result<void> WriteFile(const Core::WslSharePath& place, std::string_view bytes) override;
		Core::Result<void> Rename(const Core::WslSharePath& from, const Core::WslSharePath& to) override;
		Core::Result<void> MakeDirectory(const Core::WslSharePath& place) override;
		Core::Result<void> CreateEmptyFile(const Core::WslSharePath& place) override;
		Core::Result<void> MoveToTrash(const Core::WslSharePath& place) override;

	private:
		struct Distro
		{
			std::shared_ptr<Core::AgentConnection> Connection;
			std::optional<Core::Error> Failure;
			bool Connecting = false;
			std::thread Worker;
		};

		bool NeedsConnecting(const Distro& distro) const;
		void Connect(const std::string& name);
		void Dispatch(const std::string& distro, const Core::AgentEvent& event);
		Core::Result<void> Ask(const std::string& distro, std::string operation, nlohmann::json arguments, std::string payload = {});

		Connector m_Connect;

		std::mutex m_Mutex;
		std::condition_variable m_Changed;
		std::map<std::string, Distro> m_Distros;

		std::mutex m_ListenersMutex;
		std::map<ListenerId, std::shared_ptr<EventListener>> m_Listeners;
		ListenerId m_NextListener = 1;
	};
}
