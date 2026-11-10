#pragma once

#include "Core/Base/Result.h"
#include "Core/Remote/AgentProtocol.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Agent {

	// What a git request answers: its result, and bytes too large or too raw for the header.
	struct GitAnswer
	{
		nlohmann::json Result = nlohmann::json::object();
		std::string Payload;
	};

	using GitHandler = std::function<Core::Result<GitAnswer>(const Core::AgentRequest& request)>;
	using GitHandlers = std::map<std::string, GitHandler>;

	// Runs the agent's git requests on a thread of their own, one at a time and in order, so a
	// status of a huge repository never holds up a save or a watch event on the loop thread.
	// Finished replies wait in an outbox; the descriptor (an eventfd) is readable while some do.
	class GitWorker
	{
	public:
		GitWorker();
		~GitWorker();

		GitWorker(const GitWorker&) = delete;
		GitWorker& operator=(const GitWorker&) = delete;

		void Submit(Core::AgentRequest request, GitHandler handler);

		std::vector<Core::AgentReply> TakeFinished();

		int GetDescriptor() const { return m_Signal; }

	private:
		struct Job
		{
			Core::AgentRequest Request;
			GitHandler Handler;
		};

		void Run();

		std::mutex m_Mutex;
		std::condition_variable m_Queued;
		std::deque<Job> m_Jobs;
		std::vector<Core::AgentReply> m_Finished;
		bool m_Stopping = false;
		int m_Signal = -1;
		std::thread m_Thread;
	};
}
