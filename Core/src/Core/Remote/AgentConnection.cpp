#include "corepch.h"

#include "Core/Remote/AgentConnection.h"

#include "Core/Base/Log.h"

#include <array>
#include <format>
#include <future>
#include <utility>
#include <variant>
#include <vector>

namespace Core {

	namespace {

		constexpr std::size_t kReadBufferBytes = 64 * 1024;

		// How long closing waits for the queue to drain and for the agent to end after bye before
		// the process is killed: long enough for a save sent just before, short enough not to
		// hold Graphite's exit.
		constexpr std::chrono::seconds kGoodbyeWait{2};

		class ProcessPipe final : public AgentPipe
		{
		public:
			explicit ProcessPipe(Scope<ChildProcess> process) : m_Process(std::move(process)) {}

			std::size_t Read(std::span<char> buffer) override { return m_Process->Read(buffer); }

			bool Write(std::string_view bytes) override { return m_Process->Write(bytes); }

			// Terminate releases a Read blocked on the reader thread, which the destructor would not.
			void Close() override { m_Process->Terminate(); }

		private:
			Scope<ChildProcess> m_Process;
		};

		Error Stopped()
		{
			return Error{ErrorCode::IoFailure, "the WSL agent stopped"};
		}
	}

	Scope<AgentPipe> PipeOver(Scope<ChildProcess> process)
	{
		return CreateScope<ProcessPipe>(std::move(process));
	}

	Scope<AgentConnection> AgentConnection::Open(Scope<AgentPipe> pipe, std::string expectedVersion)
	{
		// Not make_unique, which cannot reach the private constructor.
		Scope<AgentConnection> connection{new AgentConnection{std::move(pipe), std::move(expectedVersion)}};
		AgentConnection* const self = connection.get();

		connection->m_Reader = std::thread{[self] { self->ReadUntilTheEnd(); }};
		connection->m_ReaderId = connection->m_Reader.get_id();
		connection->m_Writer = std::thread{[self] { self->WriteUntilTheEnd(); }};

		connection->Send("hello", {{"protocol", kAgentProtocolVersion}}, {}, [self](const AgentReply& reply) { self->TakeHello(reply); });

		return connection;
	}

	AgentConnection::AgentConnection(Scope<AgentPipe> pipe, std::string expectedVersion)
		: m_Pipe(std::move(pipe)), m_ExpectedVersion(std::move(expectedVersion))
	{
	}

	AgentConnection::~AgentConnection()
	{
		const bool wasReady = GetState() == State::Ready;

		if (wasReady)
		{
			Enqueue(EncodeRequest(AgentRequest{.Id = 0, .Operation = "bye"}));
		}

		// What was sent last goes out before the pipe is cut, then the agent gets a moment to end
		// on its own after bye, so nothing written into the pipe is lost to a kill.
		{
			std::unique_lock lock{m_OutboxMutex};
			m_OutboxChanged.wait_for(lock, kGoodbyeWait, [&] { return m_Outbox.empty() && m_WriterIdle; });
			m_WriterStopping = true;
		}
		m_OutboxChanged.notify_all();

		if (wasReady)
		{
			std::unique_lock lock{m_Mutex};
			m_Settled.wait_for(lock, kGoodbyeWait, [&] { return m_ReaderEnded; });
		}

		{
			std::scoped_lock lock{m_Mutex};
			m_State = State::Closed;
		}

		m_Pipe->Close();

		if (m_Writer.joinable())
		{
			m_Writer.join();
		}

		if (m_Reader.joinable())
		{
			m_Reader.join();
		}
	}

	void AgentConnection::TakeHello(const AgentReply& reply)
	{
		if (reply.Failure)
		{
			Settle(State::Failed, reply.Failure);
			return;
		}

		// An agent of another protocol may answer anything: its shape is checked, never assumed.
		const nlohmann::json& result = reply.Result;
		const auto version = result.is_object() ? result.find("version") : result.end();

		if (!result.is_object() || version == result.end() || !version->is_string())
		{
			Settle(State::Failed, Error{ErrorCode::Unsupported, "the agent in the distro answered hello in a form Graphite does not know"});
			return;
		}

		const std::string agentVersion = version->get<std::string>();

		if (!m_ExpectedVersion.empty() && agentVersion != m_ExpectedVersion)
		{
			Settle(State::Failed, Error{ErrorCode::Unsupported, std::format("the agent in the distro is version {}, Graphite needs {}",
																			agentVersion, m_ExpectedVersion)});
			return;
		}

		const auto home = result.find("home");

		{
			std::scoped_lock lock{m_Mutex};
			m_Home = home != result.end() && home->is_string() ? home->get<std::string>() : std::string{};
		}

		Settle(State::Ready, std::nullopt);
	}

	void AgentConnection::Enqueue(std::string frame)
	{
		{
			std::scoped_lock lock{m_OutboxMutex};
			m_Outbox.push_back(std::move(frame));
		}
		m_OutboxChanged.notify_all();
	}

	void AgentConnection::Send(std::string operation, nlohmann::json arguments, std::string payload, ReplyHandler onReply)
	{
		if (payload.size() > kMaxFramePayloadBytes)
		{
			onReply(AgentReply{.Id = 0, .Failure = Error{ErrorCode::TooLarge,
														 std::format("{} bytes is more than the agent takes in one request ({})",
																	 payload.size(), kMaxFramePayloadBytes)}});
			return;
		}

		// The arguments travel in the header, which the agent refuses past its limit by ending the
		// session: refused here instead, leaving room for the id and the operation's name.
		constexpr std::size_t kHeaderRoom = 512;
		const std::size_t argumentsSize = arguments.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace).size();

		if (argumentsSize + operation.size() + kHeaderRoom > kMaxFrameHeaderBytes)
		{
			onReply(AgentReply{.Id = 0, .Failure = Error{ErrorCode::TooLarge,
														 std::format("{} bytes of arguments is more than the agent takes in one request ({})",
																	 argumentsSize, kMaxFrameHeaderBytes)}});
			return;
		}

		std::uint64_t id = 0;
		std::optional<Error> refused;

		{
			std::scoped_lock lock{m_Mutex};

			if (m_State == State::Failed || m_State == State::Closed)
			{
				refused = m_Failure.value_or(Stopped());
			}
			else
			{
				id = m_NextId++;
				m_Waiting.emplace(id, std::move(onReply));
			}
		}

		if (refused)
		{
			onReply(AgentReply{.Id = 0, .Failure = refused});
			return;
		}

		Enqueue(EncodeRequest(
			AgentRequest{.Id = id, .Operation = std::move(operation), .Arguments = std::move(arguments), .Payload = std::move(payload)}));
	}

	Result<AgentReply> AgentConnection::SendAndWait(std::string operation, nlohmann::json arguments, std::string payload,
													std::chrono::milliseconds timeout)
	{
		if (std::this_thread::get_id() == m_ReaderId)
		{
			return Error{ErrorCode::InvalidArgument, "a reply or event handler cannot wait for a reply: send, and act in the handler"};
		}

		// Shared, so a reply landing after the wait gave up writes into something still alive.
		auto answer = std::make_shared<std::promise<AgentReply>>();
		std::future<AgentReply> answered = answer->get_future();

		Send(std::move(operation), std::move(arguments), std::move(payload),
			 [answer](const AgentReply& reply) { answer->set_value(reply); });

		if (answered.wait_for(timeout) != std::future_status::ready)
		{
			return Error{ErrorCode::Cancelled, "the WSL agent did not answer in time"};
		}

		AgentReply reply = answered.get();

		if (reply.Failure)
		{
			return *reply.Failure;
		}

		return reply;
	}

	void AgentConnection::SetEventHandler(EventHandler onEvent)
	{
		std::scoped_lock lock{m_Mutex};
		m_OnEvent = std::move(onEvent);
	}

	bool AgentConnection::WaitUntilSettled(std::chrono::milliseconds timeout) const
	{
		std::unique_lock lock{m_Mutex};
		return m_Settled.wait_for(lock, timeout, [&] { return m_State != State::Connecting; });
	}

	AgentConnection::State AgentConnection::GetState() const
	{
		std::scoped_lock lock{m_Mutex};
		return m_State;
	}

	std::optional<Error> AgentConnection::GetFailure() const
	{
		std::scoped_lock lock{m_Mutex};
		return m_Failure;
	}

	std::string AgentConnection::GetHome() const
	{
		std::scoped_lock lock{m_Mutex};
		return m_Home;
	}

	void AgentConnection::Settle(State state, std::optional<Error> failure)
	{
		{
			std::scoped_lock lock{m_Mutex};

			if (m_State != State::Connecting)
			{
				return;
			}

			m_State = state;
			m_Failure = std::move(failure);
		}

		m_Settled.notify_all();
	}

	void AgentConnection::WriteUntilTheEnd()
	{
		while (true)
		{
			std::string frame;
			{
				std::unique_lock lock{m_OutboxMutex};
				m_WriterIdle = true;
				m_OutboxChanged.notify_all();
				m_OutboxChanged.wait(lock, [&] { return !m_Outbox.empty() || m_WriterStopping; });

				if (m_Outbox.empty())
				{
					return;
				}

				frame = std::move(m_Outbox.front());
				m_Outbox.pop_front();
				m_WriterIdle = false;
			}

			if (!m_Pipe->Write(frame))
			{
				FailEverything(Stopped());

				std::scoped_lock lock{m_OutboxMutex};
				m_Outbox.clear();
				m_WriterIdle = true;
				m_OutboxChanged.notify_all();
				return;
			}
		}
	}

	void AgentConnection::ReadUntilTheEnd()
	{
		FrameReader reader;
		std::array<char, kReadBufferBytes> buffer{};

		while (true)
		{
			const std::size_t count = m_Pipe->Read(buffer);

			if (count == 0)
			{
				break;
			}

			reader.Feed(std::string_view{buffer.data(), count});

			while (std::optional<Frame> frame = reader.Take())
			{
				Result<AgentMessage> message = DecodeMessage(*frame);

				if (!message)
				{
					CORE_ERROR("The WSL agent sent something that is not a message: {}", message.GetError().Message);
					reader = FrameReader{};
					FailEverything(Stopped());
					m_Pipe->Close();
					break;
				}

				Dispatch(std::move(message.GetValue()));
			}

			if (reader.HasFailed())
			{
				CORE_ERROR("The WSL agent sent a frame past the size limits");
				FailEverything(Stopped());
				m_Pipe->Close();
				break;
			}
		}

		FailEverything(Stopped());

		{
			std::scoped_lock lock{m_Mutex};
			m_ReaderEnded = true;
		}
		m_Settled.notify_all();
	}

	void AgentConnection::Dispatch(AgentMessage message)
	{
		// A handler that throws loses its own message, not the reader and every request behind it.
		try
		{
			if (auto* reply = std::get_if<AgentReply>(&message))
			{
				ReplyHandler handler;
				{
					std::scoped_lock lock{m_Mutex};
					const auto waiting = m_Waiting.find(reply->Id);

					if (waiting == m_Waiting.end())
					{
						return; // a reply to a request already failed, or a wait that gave up
					}

					handler = std::move(waiting->second);
					m_Waiting.erase(waiting);
				}

				handler(*reply);
				return;
			}

			if (auto* event = std::get_if<AgentEvent>(&message))
			{
				EventHandler handler;
				{
					std::scoped_lock lock{m_Mutex};
					handler = m_OnEvent;
				}

				if (handler)
				{
					handler(*event);
				}
			}
		}
		catch (const std::exception& thrown)
		{
			CORE_ERROR("Handling a message from the WSL agent failed: {}", thrown.what());
		}
	}

	void AgentConnection::FailEverything(const Error& failure)
	{
		std::map<std::uint64_t, ReplyHandler> waiting;
		bool settled = false;

		{
			std::scoped_lock lock{m_Mutex};
			waiting = std::exchange(m_Waiting, {});

			if (m_State == State::Connecting || m_State == State::Ready)
			{
				m_State = State::Failed;
				m_Failure = failure;
				settled = true;
			}
		}

		if (settled)
		{
			m_Settled.notify_all();
		}

		for (auto& [id, handler] : waiting)
		{
			handler(AgentReply{.Id = id, .Failure = failure});
		}
	}
}
