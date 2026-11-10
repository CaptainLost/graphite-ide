#pragma once

#include "Core/Remote/AgentProtocol.h"
#include "Core/Remote/Frame.h"

#include "Git/GitRequests.h"
#include "Git/GitWorker.h"
#include "Watching/TreeWatch.h"

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Agent {

	// The agent's side of the protocol (ADR 0695), apart from the file descriptors, so tests
	// drive it with strings: bytes in through Receive, frames out through TakeOutput, and Tick
	// for what the agent sends unasked.
	class AgentSession
	{
	public:
		// gitHandlers answer the "git.*" operations on the git worker, quickHandlers theirs on the
		// quick lane; by default the agent's own.
		explicit AgentSession(std::string version, GitHandlers gitHandlers = DefaultGitHandlers(),
							  GitHandlers quickHandlers = DefaultQuickGitHandlers());

		void Receive(std::string_view bytes);

		std::string TakeOutput();

		// Reads the watches and sends a "changes" event for each settled batch.
		void Tick();

		std::vector<int> WatchDescriptors() const;

		// "bye" arrived, or bytes that are not frames: the loop ends.
		bool HasEnded() const { return m_Ended; }

	private:
		void Handle(const Core::AgentRequest& request);
		void HandleOperation(const Core::AgentRequest& request);
		void TickWatch(int id, TreeWatch& watch);
		void Reply(const Core::AgentRequest& request, Core::Result<nlohmann::json> result);

		Core::Result<nlohmann::json> Hello(const nlohmann::json& arguments) const;
		void Watch(const Core::AgentRequest& request);
		Core::Result<nlohmann::json> Unwatch(const nlohmann::json& arguments);

		std::string m_Version;
		GitHandlers m_GitHandlers;
		GitHandlers m_QuickHandlers;

		// Watches armed off the loop wait here until a tick takes them; ids unwatched before that
		// are dropped. Declared before the workers, which are joined first.
		std::mutex m_ArmedMutex;
		std::map<int, std::unique_ptr<TreeWatch>> m_Armed;
		std::set<int> m_UnwatchedEarly;

		GitWorker m_GitWorker;
		GitWorker m_QuickWorker;

		// Arms watches: a walk of a large tree takes seconds, and the loop would answer nothing
		// meanwhile, the quick lane's replies included.
		GitWorker m_WatchStarter;
		Core::FrameReader m_Reader;
		std::string m_Output;
		std::map<int, std::unique_ptr<TreeWatch>> m_Watches;
		std::map<int, bool> m_LimitReported;
		int m_NextWatchId = 1;
		bool m_Ended = false;
	};
}
