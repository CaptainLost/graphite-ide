#include "Wsl/WslFolderWatch.h"

#include "Wsl/WslAgents.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Log.h"

#include <chrono>
#include <set>
#include <string>
#include <utility>

namespace Graphite {

	namespace {

		constexpr std::chrono::seconds kStartWait{60};
		constexpr std::chrono::seconds kWatchTimeout{120};
	}

	// What the starter thread and the agent's reader thread fill, and TakeSettled empties.
	struct WslFolderWatch::Shared
	{
		Core::WslSharePath Root;

		std::mutex Mutex;
		std::set<std::filesystem::path> Directories;
		bool Overflowed = false;
		bool IgnoreRulesChanged = false;
		bool RepositoryStateChanged = false;

		std::optional<int> WatchId;
		std::weak_ptr<Core::AgentConnection> Connection;
		bool Failed = false;
		bool Stopping = false;

		std::optional<WslAgents::ListenerId> Listener;
	};

	Core::Scope<WslFolderWatch> WslFolderWatch::Start(WslAgents& agents, Core::WslSharePath root)
	{
		// Not make_unique, which cannot reach the private constructor.
		return Core::Scope<WslFolderWatch>{new WslFolderWatch{agents, std::move(root)}};
	}

	WslFolderWatch::WslFolderWatch(WslAgents& agents, Core::WslSharePath root) : m_Agents(agents), m_Shared(std::make_shared<Shared>())
	{
		m_Shared->Root = std::move(root);

		// The listener holds the shared state, not this object, so an event landing while the
		// watch is destroyed writes into something still alive.
		const std::weak_ptr<Shared> weak = m_Shared;
		m_Shared->Listener = m_Agents.AddListener([weak](const std::string& distro, const Core::AgentEvent& event) {
			const std::shared_ptr<Shared> shared = weak.lock();

			if (!shared || distro != shared->Root.Distro || event.Name != "changes" && event.Name != "watchLimit")
			{
				return;
			}

			std::scoped_lock lock{shared->Mutex};
			const auto watch = event.Body.find("watch");

			if (!shared->WatchId || watch == event.Body.end() || !watch->is_number_integer() || watch->get<int>() != *shared->WatchId)
			{
				return;
			}

			if (event.Name == "watchLimit")
			{
				// Some folders go unwatched past fs.inotify.max_user_watches: listing the whole tree
				// again is all the host can do.
				shared->Overflowed = true;
			}
			else
			{
				if (const auto directories = event.Body.find("directories"); directories != event.Body.end() && directories->is_array())
				{
					for (const nlohmann::json& directory : *directories)
					{
						if (directory.is_string())
						{
							shared->Directories.insert(Core::ToWslSharePath(Core::WslSharePath{shared->Root.Distro, directory.get<std::string>()}));
						}
					}
				}

				shared->Overflowed |= event.Body.value("overflowed", false);
				shared->IgnoreRulesChanged |= event.Body.value("ignoreRules", false);
				shared->RepositoryStateChanged |= event.Body.value("repositoryState", false);
			}

			Core::Redraw::Request();
		});

		m_Starter = std::thread{[this, shared = m_Shared] {
			const Core::Result<std::shared_ptr<Core::AgentConnection>> connection = m_Agents.Get(shared->Root.Distro, kStartWait);

			if (!connection)
			{
				APP_WARN("Cannot watch {} in {}: {}", shared->Root.LinuxPath, shared->Root.Distro, connection.GetError().Message);
				std::scoped_lock lock{shared->Mutex};
				shared->Failed = true;
				return;
			}

			const Core::Result<Core::AgentReply> watched =
				connection.GetValue()->SendAndWait("watch", {{"root", shared->Root.LinuxPath}}, {}, kWatchTimeout);

			std::scoped_lock lock{shared->Mutex};
			shared->Connection = connection.GetValue();

			const auto id = watched ? watched.GetValue().Result.find("watch") : nlohmann::json::const_iterator{};

			if (!watched || id == watched.GetValue().Result.end() || !id->is_number_integer())
			{
				APP_WARN("The agent in {} cannot watch {}: {}", shared->Root.Distro, shared->Root.LinuxPath,
						 watched ? std::string{"no watch in its answer"} : watched.GetError().Message);
				shared->Failed = true;
				return;
			}

			shared->WatchId = id->get<int>();
		}};
	}

	WslFolderWatch::~WslFolderWatch()
	{
		if (m_Starter.joinable())
		{
			m_Starter.join();
		}

		if (m_Shared->Listener)
		{
			m_Agents.RemoveListener(*m_Shared->Listener);
		}

		std::scoped_lock lock{m_Shared->Mutex};
		const std::shared_ptr<Core::AgentConnection> connection = m_Shared->Connection.lock();

		if (connection && m_Shared->WatchId)
		{
			connection->Send("unwatch", {{"watch", *m_Shared->WatchId}}, {}, [](const Core::AgentReply&) {});
		}
	}

	Core::FolderChanges WslFolderWatch::TakeSettled()
	{
		std::scoped_lock lock{m_Shared->Mutex};

		Core::FolderChanges changes{
			.Directories = {m_Shared->Directories.begin(), m_Shared->Directories.end()},
			.Overflowed = std::exchange(m_Shared->Overflowed, false),
			.IgnoreRulesChanged = std::exchange(m_Shared->IgnoreRulesChanged, false),
			.RepositoryStateChanged = std::exchange(m_Shared->RepositoryStateChanged, false),
			.MovedTo = std::nullopt,
		};

		m_Shared->Directories.clear();
		return changes;
	}

	bool WslFolderWatch::IsRunning() const
	{
		std::scoped_lock lock{m_Shared->Mutex};

		if (m_Shared->Failed)
		{
			return false;
		}

		// Before the agent is up the watch counts as running: the folder is being connected to.
		if (!m_Shared->WatchId)
		{
			return true;
		}

		const std::shared_ptr<Core::AgentConnection> connection = m_Shared->Connection.lock();
		return connection != nullptr && connection->GetState() == Core::AgentConnection::State::Ready;
	}
}
