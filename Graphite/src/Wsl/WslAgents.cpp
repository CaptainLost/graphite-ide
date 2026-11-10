#include "Wsl/WslAgents.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Log.h"
#include "Core/Remote/AgentDeployment.h"

#include <format>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		// A request the agent does not answer in this long fails, rather than freezing a save.
		constexpr std::chrono::seconds kRequestTimeout{30};

		// How long the first change into a distro waits for its agent to start.
		constexpr std::chrono::seconds kStartTimeout{30};

		bool IsReady(const std::shared_ptr<Core::AgentConnection>& connection)
		{
			return connection != nullptr && connection->GetState() == Core::AgentConnection::State::Ready;
		}

		// WSL takes a distro's name in any case: \\wsl$\ubuntu and wsl -d ubuntu reach Ubuntu.
		std::string DistroKey(std::string_view name)
		{
			std::string key{name};

			for (char& character : key)
			{
				character = Core::LowerAscii(character);
			}

			return key;
		}
	}

	WslAgents::WslAgents(Connector connect, ConnectedHook onConnected)
		: m_Connect(std::move(connect)), m_OnConnected(std::move(onConnected))
	{
	}

	WslAgents::~WslAgents()
	{
		std::vector<std::thread> workers;
		{
			std::scoped_lock lock{m_Mutex};

			for (auto& [name, distro] : m_Distros)
			{
				if (distro.Worker.joinable())
				{
					workers.push_back(std::move(distro.Worker));
				}
			}
		}

		for (std::thread& worker : workers)
		{
			worker.join();
		}

		// Closed here, while the listeners are alive: a connection's reader keeps dispatching the
		// events already in the pipe for as long as closing waits for the agent to end.
		std::vector<std::shared_ptr<Core::AgentConnection>> connections;
		{
			std::scoped_lock lock{m_Mutex};

			for (auto& [name, distro] : m_Distros)
			{
				connections.push_back(std::move(distro.Connection));
			}
		}

		connections.clear();
	}

	WslAgents::Connector WslAgents::ConnectThroughWsl(std::filesystem::path agentBinary)
	{
		return [agentBinary = std::move(agentBinary)](const std::string& distro) -> Core::Result<std::shared_ptr<Core::AgentConnection>> {
			const Core::Result<Core::AgentInstall> installed = Core::InstallAgent(distro, agentBinary);

			if (!installed)
			{
				return installed.GetError();
			}

			Core::Result<Core::Scope<Core::AgentPipe>> pipe = Core::StartAgent(installed.GetValue());

			if (!pipe)
			{
				return pipe.GetError();
			}

			std::shared_ptr<Core::AgentConnection> connection{
				Core::AgentConnection::Open(std::move(pipe.GetValue()), installed.GetValue().Version)};

			if (!connection->WaitUntilSettled(kStartTimeout))
			{
				return Core::Error{Core::ErrorCode::Cancelled, std::format("the agent in {} did not answer in time", distro)};
			}

			if (connection->GetState() != Core::AgentConnection::State::Ready)
			{
				return connection->GetFailure().value_or(Core::Error{Core::ErrorCode::IoFailure, "the WSL agent stopped"});
			}

			return connection;
		};
	}

	bool WslAgents::NeedsConnecting(const Distro& distro) const
	{
		return !distro.Connecting && !IsReady(distro.Connection);
	}

	void WslAgents::Prepare(const std::string& asked)
	{
		const std::string name = DistroKey(asked);
		std::thread previous;
		{
			std::scoped_lock lock{m_Mutex};
			Distro& distro = m_Distros[name];

			if (!NeedsConnecting(distro))
			{
				return;
			}

			distro.Connecting = true;
			distro.Failure.reset();
			previous = std::move(distro.Worker);
			distro.Worker = std::thread{[this, name] { Connect(name); }};
		}

		// The worker of an earlier attempt has finished (it cleared Connecting before ending).
		if (previous.joinable())
		{
			previous.join();
		}
	}

	void WslAgents::Connect(const std::string& name)
	{
		Core::Result<std::shared_ptr<Core::AgentConnection>> connected = m_Connect(name);

		if (connected)
		{
			connected.GetValue()->SetEventHandler([this, name](const Core::AgentEvent& event) { Dispatch(name, event); });
			APP_INFO("Connected to the Graphite agent in {}", name);

			if (m_OnConnected)
			{
				m_OnConnected(name, *connected.GetValue());
			}
		}
		else
		{
			APP_WARN("Cannot reach the Graphite agent in {}: {}", name, connected.GetError().Message);
		}

		{
			std::scoped_lock lock{m_Mutex};
			Distro& distro = m_Distros[name];
			distro.Connecting = false;

			if (connected)
			{
				distro.Connection = std::move(connected.GetValue());
			}
			else
			{
				distro.Connection.reset();
				distro.Failure = connected.GetError();
			}
		}

		m_Changed.notify_all();
	}

	Core::Result<std::shared_ptr<Core::AgentConnection>> WslAgents::Get(const std::string& asked, std::chrono::milliseconds wait)
	{
		const std::string name = DistroKey(asked);
		Prepare(name);

		std::unique_lock lock{m_Mutex};
		const bool settled = m_Changed.wait_for(lock, wait, [&] { return !m_Distros[name].Connecting; });
		const Distro& distro = m_Distros[name];

		if (!settled)
		{
			return Core::Error{Core::ErrorCode::Cancelled, std::format("still connecting to {}", name)};
		}

		if (IsReady(distro.Connection))
		{
			return distro.Connection;
		}

		return distro.Failure.value_or(Core::Error{Core::ErrorCode::IoFailure, std::format("cannot reach {}", name)});
	}

	WslAgents::ListenerId WslAgents::AddListener(EventListener listener)
	{
		std::scoped_lock lock{m_ListenersMutex};
		const ListenerId id = m_NextListener++;
		m_Listeners.emplace(id, std::make_shared<EventListener>(std::move(listener)));
		return id;
	}

	void WslAgents::RemoveListener(ListenerId id)
	{
		std::scoped_lock lock{m_ListenersMutex};
		m_Listeners.erase(id);
	}

	void WslAgents::Dispatch(const std::string& distro, const Core::AgentEvent& event)
	{
		std::vector<std::shared_ptr<EventListener>> listeners;
		{
			std::scoped_lock lock{m_ListenersMutex};

			for (const auto& [id, listener] : m_Listeners)
			{
				listeners.push_back(listener);
			}
		}

		for (const std::shared_ptr<EventListener>& listener : listeners)
		{
			(*listener)(distro, event);
		}
	}

	Core::Result<void> WslAgents::Ask(const std::string& distro, std::string operation, nlohmann::json arguments, std::string payload)
	{
		const Core::Result<std::shared_ptr<Core::AgentConnection>> connection = Get(distro, kStartTimeout);

		if (!connection)
		{
			return connection.GetError();
		}

		const Core::Result<Core::AgentReply> reply =
			connection.GetValue()->SendAndWait(std::move(operation), std::move(arguments), std::move(payload), kRequestTimeout);

		if (!reply)
		{
			return reply.GetError();
		}

		return Core::Result<void>::Ok();
	}

	Core::Result<void> WslAgents::WriteFile(const Core::WslSharePath& place, std::string_view bytes)
	{
		return Ask(place.Distro, "writeFile", {{"path", place.LinuxPath}}, std::string{bytes});
	}

	Core::Result<void> WslAgents::Rename(const Core::WslSharePath& from, const Core::WslSharePath& to)
	{
		return Ask(from.Distro, "rename", {{"from", from.LinuxPath}, {"to", to.LinuxPath}});
	}

	Core::Result<void> WslAgents::MakeDirectory(const Core::WslSharePath& place)
	{
		return Ask(place.Distro, "makeDirectory", {{"path", place.LinuxPath}});
	}

	Core::Result<void> WslAgents::CreateEmptyFile(const Core::WslSharePath& place)
	{
		return Ask(place.Distro, "createFile", {{"path", place.LinuxPath}});
	}

	Core::Result<void> WslAgents::MoveToTrash(const Core::WslSharePath& place)
	{
		return Ask(place.Distro, "moveToTrash", {{"path", place.LinuxPath}});
	}
}
