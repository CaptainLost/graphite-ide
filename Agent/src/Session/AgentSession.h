#pragma once

#include "Core/Remote/AgentProtocol.h"
#include "Core/Remote/Frame.h"

#include "Watching/TreeWatch.h"

#include <map>
#include <memory>
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
		explicit AgentSession(std::string version);

		void Receive(std::string_view bytes);

		std::string TakeOutput();

		// Reads the watches and sends a "changes" event for each settled batch.
		void Tick();

		std::vector<int> WatchDescriptors() const;

		// "bye" arrived, or bytes that are not frames: the loop ends.
		bool HasEnded() const { return m_Ended; }

	private:
		void Handle(const Core::AgentRequest& request);
		void Reply(const Core::AgentRequest& request, Core::Result<nlohmann::json> result);

		Core::Result<nlohmann::json> Hello(const nlohmann::json& arguments) const;
		Core::Result<nlohmann::json> Watch(const nlohmann::json& arguments);
		Core::Result<nlohmann::json> Unwatch(const nlohmann::json& arguments);

		std::string m_Version;
		Core::FrameReader m_Reader;
		std::string m_Output;
		std::map<int, std::unique_ptr<TreeWatch>> m_Watches;
		std::map<int, bool> m_LimitReported;
		int m_NextWatchId = 1;
		bool m_Ended = false;
	};
}
