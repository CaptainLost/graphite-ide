#pragma once

// An in-memory pipe and a stand-in WSL agent for tests of the host's side of the agent protocol
// (ADR 0695): the agent answers hello itself and hands every other request to the test.

#include "Core/Base/Base.h"
#include "Core/Remote/AgentConnection.h"
#include "Core/Remote/AgentPipe.h"
#include "Core/Remote/AgentProtocol.h"
#include "Core/Remote/Frame.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

namespace AgentFixtures {

	// One direction of a pipe: bytes written on one end, read on the other; closing releases a
	// waiting reader with 0.
	class ByteChannel
	{
	public:
		void Put(std::string_view bytes)
		{
			{
				std::scoped_lock lock{m_Mutex};
				m_Bytes.append(bytes);
			}
			m_Changed.notify_all();
		}

		std::size_t Get(std::span<char> buffer)
		{
			std::unique_lock lock{m_Mutex};
			m_Changed.wait(lock, [&] { return !m_Bytes.empty() || m_Closed; });

			const std::size_t count = std::min(buffer.size(), m_Bytes.size());
			std::copy_n(m_Bytes.begin(), count, buffer.begin());
			m_Bytes.erase(0, count);
			return count;
		}

		void Close()
		{
			{
				std::scoped_lock lock{m_Mutex};
				m_Closed = true;
			}
			m_Changed.notify_all();
		}

	private:
		std::mutex m_Mutex;
		std::condition_variable m_Changed;
		std::string m_Bytes;
		bool m_Closed = false;
	};

	class ChannelPipe final : public Core::AgentPipe
	{
	public:
		ChannelPipe(std::shared_ptr<ByteChannel> in, std::shared_ptr<ByteChannel> out) : m_In(std::move(in)), m_Out(std::move(out)) {}

		std::size_t Read(std::span<char> buffer) override { return m_In->Get(buffer); }
		bool Write(std::string_view bytes) override
		{
			m_Out->Put(bytes);
			return true;
		}
		void Close() override
		{
			m_In->Close();
			m_Out->Close();
		}

	private:
		std::shared_ptr<ByteChannel> m_In;
		std::shared_ptr<ByteChannel> m_Out;
	};

	// A stand-in agent on a thread of its own: it answers hello, then hands every other request
	// to the test's handler, which writes whatever frames it likes.
	class FakeAgent
	{
	public:
		using Handler = std::function<void(const Core::AgentRequest&, FakeAgent&)>;

		explicit FakeAgent(Handler handler, std::string version = "v1")
			: m_ToAgent(std::make_shared<ByteChannel>()), m_FromAgent(std::make_shared<ByteChannel>()),
			  m_Handler(std::move(handler)), m_Version(std::move(version))
		{
			m_Thread = std::thread{[this] { Run(); }};
		}

		~FakeAgent()
		{
			Hang();
			m_Thread.join();
		}

		Core::Scope<Core::AgentPipe> HostEnd() { return Core::CreateScope<ChannelPipe>(m_FromAgent, m_ToAgent); }

		void Send(const std::string& frame) { m_FromAgent->Put(frame); }

		void Reply(std::uint64_t id, nlohmann::json result, std::string payload = {})
		{
			Send(Core::EncodeReply(Core::AgentReply{.Id = id, .Result = std::move(result), .Payload = std::move(payload)}));
		}

		// Answers hello with this result instead of a well-formed one.
		void ReplyToHelloWith(nlohmann::json result)
		{
			std::scoped_lock lock{m_HelloMutex};
			m_HelloOverride = std::move(result);
		}

		// The agent's process ends: both directions close.
		void Hang()
		{
			m_ToAgent->Close();
			m_FromAgent->Close();
		}

	private:
		void Run()
		{
			Core::FrameReader reader;
			std::array<char, 4096> buffer{};

			while (true)
			{
				const std::size_t count = m_ToAgent->Get(buffer);

				if (count == 0)
				{
					return;
				}

				reader.Feed(std::string_view{buffer.data(), count});

				while (const std::optional<Core::Frame> frame = reader.Take())
				{
					Core::Result<Core::AgentMessage> message = Core::DecodeMessage(*frame);
					const auto* request = message ? std::get_if<Core::AgentRequest>(&message.GetValue()) : nullptr;

					if (request == nullptr)
					{
						continue;
					}

					if (request->Operation == "hello")
					{
						std::optional<nlohmann::json> override;
						{
							std::scoped_lock lock{m_HelloMutex};
							override = m_HelloOverride;
						}

						Reply(request->Id, override.value_or(nlohmann::json{{"version", m_Version}, {"protocol", 1}, {"home", "/home/me"}, {"pid", 1}}));
					}
					else if (request->Operation != "bye")
					{
						m_Handler(*request, *this);
					}
				}
			}
		}

		std::shared_ptr<ByteChannel> m_ToAgent;
		std::shared_ptr<ByteChannel> m_FromAgent;
		Handler m_Handler;
		std::string m_Version;
		std::mutex m_HelloMutex;
		std::optional<nlohmann::json> m_HelloOverride;
		std::thread m_Thread;
	};

}
