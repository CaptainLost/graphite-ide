#include <doctest/doctest.h>

#include "Core/Remote/AgentConnection.h"
#include "Core/Remote/AgentProtocol.h"
#include "Core/Remote/Frame.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

using namespace std::chrono_literals;

namespace {

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

	void Ignore(const Core::AgentRequest&, FakeAgent&) {}
}

TEST_CASE("A connection is ready once the agent answers hello, and knows its home")
{
	FakeAgent agent{Ignore};
	const Core::Scope<Core::AgentConnection> connection = Core::AgentConnection::Open(agent.HostEnd(), "v1");

	REQUIRE(connection->WaitUntilSettled(2s));
	CHECK(connection->GetState() == Core::AgentConnection::State::Ready);
	CHECK(connection->GetHome() == "/home/me");
}

TEST_CASE("An agent of another version fails the connection and says which")
{
	FakeAgent agent{Ignore, "old"};
	const Core::Scope<Core::AgentConnection> connection = Core::AgentConnection::Open(agent.HostEnd(), "new");

	REQUIRE(connection->WaitUntilSettled(2s));
	CHECK(connection->GetState() == Core::AgentConnection::State::Failed);
	REQUIRE(connection->GetFailure().has_value());
	CHECK(connection->GetFailure()->Code == Core::ErrorCode::Unsupported);
	CHECK(connection->GetFailure()->Message.find("old") != std::string::npos);
}

TEST_CASE("A request and its reply round-trip with their payloads")
{
	FakeAgent agent{[](const Core::AgentRequest& request, FakeAgent& self) {
		self.Reply(request.Id, {{"echoed", request.Arguments.at("path")}}, request.Payload + "!");
	}};
	const Core::Scope<Core::AgentConnection> connection = Core::AgentConnection::Open(agent.HostEnd(), "v1");
	REQUIRE(connection->WaitUntilSettled(2s));

	const Core::Result<Core::AgentReply> reply = connection->SendAndWait("writeFile", {{"path", "/tmp/a"}}, "bytes", 2s);

	REQUIRE(reply.HasValue());
	CHECK(reply.GetValue().Result.at("echoed") == "/tmp/a");
	CHECK(reply.GetValue().Payload == "bytes!");
}

TEST_CASE("Replies that come back out of order reach the requests they answer")
{
	std::mutex held;
	std::optional<Core::AgentRequest> first;

	FakeAgent agent{[&](const Core::AgentRequest& request, FakeAgent& self) {
		std::scoped_lock lock{held};

		if (!first)
		{
			first = request; // answered after the second
			return;
		}

		self.Reply(request.Id, {{"which", "second"}});
		self.Reply(first->Id, {{"which", "first"}});
	}};
	const Core::Scope<Core::AgentConnection> connection = Core::AgentConnection::Open(agent.HostEnd(), "v1");
	REQUIRE(connection->WaitUntilSettled(2s));

	std::promise<std::string> firstAnswer;
	std::promise<std::string> secondAnswer;
	connection->Send("a", {}, {}, [&](const Core::AgentReply& reply) { firstAnswer.set_value(reply.Result.at("which")); });
	connection->Send("b", {}, {}, [&](const Core::AgentReply& reply) { secondAnswer.set_value(reply.Result.at("which")); });

	auto firstFuture = firstAnswer.get_future();
	auto secondFuture = secondAnswer.get_future();
	REQUIRE(firstFuture.wait_for(2s) == std::future_status::ready);
	REQUIRE(secondFuture.wait_for(2s) == std::future_status::ready);
	CHECK(firstFuture.get() == "first");
	CHECK(secondFuture.get() == "second");
}

TEST_CASE("An event the agent sends unasked reaches the event handler")
{
	FakeAgent agent{Ignore};
	const Core::Scope<Core::AgentConnection> connection = Core::AgentConnection::Open(agent.HostEnd(), "v1");
	std::promise<std::string> named;
	connection->SetEventHandler([&](const Core::AgentEvent& event) { named.set_value(event.Name); });
	REQUIRE(connection->WaitUntilSettled(2s));

	agent.Send(Core::EncodeEvent(Core::AgentEvent{.Name = "changes", .Body = {{"watch", 1}}}));

	auto future = named.get_future();
	REQUIRE(future.wait_for(2s) == std::future_status::ready);
	CHECK(future.get() == "changes");
}

TEST_CASE("When the agent stops, every request still waiting fails, and later ones fail at once")
{
	FakeAgent agent{Ignore}; // never replies
	const Core::Scope<Core::AgentConnection> connection = Core::AgentConnection::Open(agent.HostEnd(), "v1");
	REQUIRE(connection->WaitUntilSettled(2s));

	std::promise<std::optional<Core::Error>> answered;
	connection->Send("rename", {}, {}, [&](const Core::AgentReply& reply) { answered.set_value(reply.Failure); });

	agent.Hang();

	auto future = answered.get_future();
	REQUIRE(future.wait_for(2s) == std::future_status::ready);
	const std::optional<Core::Error> failure = future.get();
	REQUIRE(failure.has_value());
	CHECK(failure->Code == Core::ErrorCode::IoFailure);
	CHECK(failure->Message == "the WSL agent stopped");

	const Core::Result<Core::AgentReply> later = connection->SendAndWait("rename", {}, {}, 2s);
	REQUIRE_FALSE(later.HasValue());
	CHECK(later.GetError().Code == Core::ErrorCode::IoFailure);
	CHECK(connection->GetState() == Core::AgentConnection::State::Failed);
}

TEST_CASE("A reply that never comes ends the wait as cancelled")
{
	FakeAgent agent{Ignore};
	const Core::Scope<Core::AgentConnection> connection = Core::AgentConnection::Open(agent.HostEnd(), "v1");
	REQUIRE(connection->WaitUntilSettled(2s));

	const Core::Result<Core::AgentReply> reply = connection->SendAndWait("rename", {}, {}, 100ms);

	REQUIRE_FALSE(reply.HasValue());
	CHECK(reply.GetError().Code == Core::ErrorCode::Cancelled);
}

TEST_CASE("Waiting for a reply from inside a handler is refused at once, since only the reader could deliver it")
{
	FakeAgent agent{Ignore};
	const Core::Scope<Core::AgentConnection> connection = Core::AgentConnection::Open(agent.HostEnd(), "v1");

	std::promise<Core::Result<Core::AgentReply>> fromHandler;
	connection->SetEventHandler([&](const Core::AgentEvent&) {
		fromHandler.set_value(connection->SendAndWait("rename", {}, {}, 5s));
	});
	REQUIRE(connection->WaitUntilSettled(2s));

	const auto started = std::chrono::steady_clock::now();
	agent.Send(Core::EncodeEvent(Core::AgentEvent{.Name = "changes"}));

	auto future = fromHandler.get_future();
	REQUIRE(future.wait_for(4s) == std::future_status::ready);
	const Core::Result<Core::AgentReply> result = future.get();
	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Code == Core::ErrorCode::InvalidArgument);
	CHECK(std::chrono::steady_clock::now() - started < 2s);
}

TEST_CASE("A hello answer of the wrong shape fails the connection instead of ending Graphite")
{
	FakeAgent agent{Ignore};
	auto pipe = agent.HostEnd();
	agent.ReplyToHelloWith(nlohmann::json::array({1, 2}));
	const Core::Scope<Core::AgentConnection> connection = Core::AgentConnection::Open(std::move(pipe), "v1");

	REQUIRE(connection->WaitUntilSettled(2s));
	CHECK(connection->GetState() == Core::AgentConnection::State::Failed);
}

TEST_CASE("A payload past the frame limit is refused as too large, and the connection stays up")
{
	FakeAgent agent{[](const Core::AgentRequest& request, FakeAgent& self) { self.Reply(request.Id, {}); }};
	const Core::Scope<Core::AgentConnection> connection = Core::AgentConnection::Open(agent.HostEnd(), "v1");
	REQUIRE(connection->WaitUntilSettled(2s));

	const Core::Result<Core::AgentReply> tooLarge =
		connection->SendAndWait("writeFile", {{"path", "/tmp/big"}}, std::string(Core::kMaxFramePayloadBytes + 1, 'x'), 5s);

	REQUIRE_FALSE(tooLarge.HasValue());
	CHECK(tooLarge.GetError().Code == Core::ErrorCode::TooLarge);
	CHECK(connection->SendAndWait("rename", {}, {}, 2s).HasValue());
}

TEST_CASE("Requests sent just before the connection closes still reach the agent, then bye")
{
	std::mutex seen;
	std::vector<std::string> operations;

	{
		FakeAgent agent{[&](const Core::AgentRequest& request, FakeAgent&) {
			std::scoped_lock lock{seen};
			operations.push_back(request.Operation);
		}};
		{
			const Core::Scope<Core::AgentConnection> connection = Core::AgentConnection::Open(agent.HostEnd(), "v1");
			REQUIRE(connection->WaitUntilSettled(2s));

			for (int index = 0; index < 50; ++index)
			{
				connection->Send("writeFile", {{"path", "/tmp/x"}}, "bytes", [](const Core::AgentReply&) {});
			}
		}
		std::this_thread::sleep_for(100ms);
	}

	std::scoped_lock lock{seen};
	CHECK(operations.size() == 50);
}
