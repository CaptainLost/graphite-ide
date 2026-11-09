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

		connection->m_Reader = std::thread{[raw = connection.get()] { raw->ReadUntilTheEnd(); }};

		AgentConnection* const self = connection.get();
		connection->Send("hello", {{"protocol", kAgentProtocolVersion}}, {}, [self](const AgentReply& reply) {
			if (reply.Failure)
			{
				self->Settle(State::Failed, reply.Failure);
				return;
			}

			const std::string version = reply.Result.value("version", std::string{});

			if (!self->m_ExpectedVersion.empty() && version != self->m_ExpectedVersion)
			{
				self->Settle(State::Failed,
							 Error{ErrorCode::Unsupported, std::format("the agent in the distro is version {}, Graphite needs {}",
																	   version, self->m_ExpectedVersion)});
				return;
			}

			{
				std::scoped_lock lock{self->m_Mutex};
				self->m_Home = reply.Result.value("home", std::string{});
			}

			self->Settle(State::Ready, std::nullopt);
		});

		return connection;
	}

	AgentConnection::AgentConnection(Scope<AgentPipe> pipe, std::string expectedVersion)
		: m_Pipe(std::move(pipe)), m_ExpectedVersion(std::move(expectedVersion))
	{
	}

	AgentConnection::~AgentConnection()
	{
		if (GetState() == State::Ready)
		{
			std::scoped_lock lock{m_WriteMutex};
			m_Pipe->Write(EncodeRequest(AgentRequest{.Id = 0, .Operation = "bye"}));
		}

		{
			std::scoped_lock lock{m_Mutex};
			m_State = State::Closed;
		}

		m_Pipe->Close();

		if (m_Reader.joinable())
		{
			m_Reader.join();
		}
	}

	void AgentConnection::Send(std::string operation, nlohmann::json arguments, std::string payload, ReplyHandler onReply)
	{
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

		const std::string frame =
			EncodeRequest(AgentRequest{.Id = id, .Operation = std::move(operation), .Arguments = std::move(arguments), .Payload = std::move(payload)});

		bool written = false;
		{
			std::scoped_lock lock{m_WriteMutex};
			written = m_Pipe->Write(frame);
		}

		if (!written)
		{
			// The reader will see the pipe end too; this request is answered either way.
			FailEverything(Stopped());
		}
	}

	Result<AgentReply> AgentConnection::SendAndWait(std::string operation, nlohmann::json arguments, std::string payload,
													std::chrono::milliseconds timeout)
	{
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
					FailEverything(Stopped());
					m_Pipe->Close();
					return;
				}

				Dispatch(std::move(message.GetValue()));
			}

			if (reader.HasFailed())
			{
				CORE_ERROR("The WSL agent sent a frame past the size limits");
				FailEverything(Stopped());
				m_Pipe->Close();
				return;
			}
		}

		FailEverything(Stopped());
	}

	void AgentConnection::Dispatch(AgentMessage message)
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
