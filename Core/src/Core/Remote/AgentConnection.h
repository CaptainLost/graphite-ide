#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Platform/ChildProcess.h"
#include "Core/Remote/AgentPipe.h"
#include "Core/Remote/AgentProtocol.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace Core {

	// The stdin and stdout of a started agent process as an AgentPipe.
	Scope<AgentPipe> PipeOver(Scope<ChildProcess> process);

	// Graphite's end of the conversation with one WSL agent (ADR 0695). A reader thread takes
	// frames off the pipe and hands each reply to the handler of the request it answers and each
	// event to the event handler. When the pipe ends every request still waiting fails with
	// "the WSL agent stopped", and so does every later one.
	class AgentConnection : NonCopyable
	{
	public:
		enum class State
		{
			Connecting,
			Ready,
			Failed,
			Closed
		};

		using ReplyHandler = std::function<void(const AgentReply&)>;
		using EventHandler = std::function<void(const AgentEvent&)>;

		// Starts the reader and says hello; Ready once the agent answers with expectedVersion
		// (any version when it is empty).
		static Scope<AgentConnection> Open(Scope<AgentPipe> pipe, std::string expectedVersion);

		// Says bye, ends the pipe and waits for the reader.
		~AgentConnection();

		// onReply runs on the reader thread, or at once on this one when the connection has
		// already failed.
		void Send(std::string operation, nlohmann::json arguments, std::string payload, ReplyHandler onReply);

		// A failed reply comes back as its error; no reply within the timeout is Cancelled. Not from
		// a reply or event handler: only the reader thread they run on could deliver the reply, so
		// such a call is refused at once with InvalidArgument.
		Result<AgentReply> SendAndWait(std::string operation, nlohmann::json arguments, std::string payload,
									   std::chrono::milliseconds timeout);

		// Runs on the reader thread. Set it before the agent has reason to send anything.
		void SetEventHandler(EventHandler onEvent);

		// True once the state is no longer Connecting, false when the timeout passes first.
		bool WaitUntilSettled(std::chrono::milliseconds timeout) const;

		State GetState() const;
		std::optional<Error> GetFailure() const;
		std::string GetHome() const;

	private:
		AgentConnection(Scope<AgentPipe> pipe, std::string expectedVersion);

		void ReadUntilTheEnd();
		void WriteUntilTheEnd();
		void Dispatch(AgentMessage message);
		void FailEverything(const Error& failure);
		void Settle(State state, std::optional<Error> failure);
		void Enqueue(std::string frame);
		void TakeHello(const AgentReply& reply);

		Scope<AgentPipe> m_Pipe;
		std::string m_ExpectedVersion;

		mutable std::mutex m_Mutex;
		mutable std::condition_variable m_Settled;
		State m_State = State::Connecting;
		std::optional<Error> m_Failure;
		std::string m_Home;
		std::uint64_t m_NextId = 1;
		std::map<std::uint64_t, ReplyHandler> m_Waiting;
		EventHandler m_OnEvent;
		bool m_ReaderEnded = false;

		// Frames go out on a thread of their own, so the reader never waits on the pipe: a handler
		// that sends while the agent is blocked writing to us would otherwise stop the reader that
		// the agent waits on.
		std::mutex m_OutboxMutex;
		std::condition_variable m_OutboxChanged;
		std::deque<std::string> m_Outbox;
		bool m_WriterStopping = false;
		bool m_WriterIdle = true;

		std::thread m_Reader;
		std::thread m_Writer;
		std::thread::id m_ReaderId;
	};
}
