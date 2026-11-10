#include <doctest/doctest.h>

#include "Session/AgentSession.h"

#include "Core/Remote/AgentProtocol.h"
#include "Core/Remote/Frame.h"

#include "TempDirectory.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <variant>
#include <vector>

using nlohmann::json;

namespace {

	std::vector<Core::AgentMessage> Decode(const std::string& output)
	{
		Core::FrameReader reader;
		reader.Feed(output);

		std::vector<Core::AgentMessage> messages;

		while (const std::optional<Core::Frame> frame = reader.Take())
		{
			Core::Result<Core::AgentMessage> message = Core::DecodeMessage(*frame);
			REQUIRE(message.HasValue());
			messages.push_back(std::move(message.GetValue()));
		}

		return messages;
	}

	Core::AgentReply Ask(Agent::AgentSession& session, std::uint64_t id, std::string operation, json arguments,
						 std::string payload = {})
	{
		session.Receive(Core::EncodeRequest(Core::AgentRequest{
			.Id = id, .Operation = std::move(operation), .Arguments = std::move(arguments), .Payload = std::move(payload)}));

		const std::vector<Core::AgentMessage> messages = Decode(session.TakeOutput());
		REQUIRE(messages.size() == 1);

		const auto* reply = std::get_if<Core::AgentReply>(&messages.front());
		REQUIRE(reply != nullptr);
		CHECK(reply->Id == id);
		return *reply;
	}
}

TEST_CASE("hello answers with the agent's version, the protocol and the home folder")
{
	Agent::AgentSession session{"abc123"};

	const Core::AgentReply reply = Ask(session, 1, "hello", json{{"protocol", Core::kAgentProtocolVersion}});

	REQUIRE_FALSE(reply.Failure.has_value());
	CHECK(reply.Result.at("version") == "abc123");
	CHECK(reply.Result.at("protocol") == Core::kAgentProtocolVersion);
	CHECK(reply.Result.at("home") == std::string{std::getenv("HOME")});
}

TEST_CASE("hello with another protocol version is refused")
{
	Agent::AgentSession session{"abc123"};

	const Core::AgentReply reply = Ask(session, 1, "hello", json{{"protocol", 99}});

	REQUIRE(reply.Failure.has_value());
	CHECK(reply.Failure->Code == Core::ErrorCode::Unsupported);
}

TEST_CASE("writeFile through frames writes the payload")
{
	TempDirectory directory;
	Agent::AgentSession session{"v"};
	const std::filesystem::path file = directory.GetPath() / "written.txt";

	const Core::AgentReply reply = Ask(session, 5, "writeFile", json{{"path", file.string()}}, "payload"); // not a path conversion

	REQUIRE_FALSE(reply.Failure.has_value());
	std::ifstream stream{file};
	std::ostringstream contents;
	contents << stream.rdbuf();
	CHECK(contents.str() == "payload");
}

TEST_CASE("An operation the agent does not know is unsupported")
{
	Agent::AgentSession session{"v"};

	const Core::AgentReply reply = Ask(session, 2, "launchRockets", json::object());

	REQUIRE(reply.Failure.has_value());
	CHECK(reply.Failure->Code == Core::ErrorCode::Unsupported);
}

TEST_CASE("A watched folder's change arrives as one changes event naming the folder")
{
	TempDirectory directory;
	Agent::AgentSession session{"v"};

	const Core::AgentReply watched = Ask(session, 3, "watch", json{{"root", directory.GetPath().string()}}); // not a path conversion
	REQUIRE_FALSE(watched.Failure.has_value());
	const int watchId = watched.Result.at("watch").get<int>();

	directory.MakeFile("new.txt", "x");

	std::vector<Core::AgentMessage> events;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};

	while (events.empty() && std::chrono::steady_clock::now() < deadline)
	{
		session.Tick();
		events = Decode(session.TakeOutput());
		std::this_thread::sleep_for(std::chrono::milliseconds{20});
	}

	REQUIRE(events.size() == 1);
	const auto* event = std::get_if<Core::AgentEvent>(&events.front());
	REQUIRE(event != nullptr);
	CHECK(event->Name == "changes");
	CHECK(event->Body.at("watch") == watchId);
	CHECK(event->Body.at("directories") == json::array({directory.GetPath().string()})); // not a path conversion
}

TEST_CASE("bye ends the session")
{
	Agent::AgentSession session{"v"};

	session.Receive(Core::EncodeRequest(Core::AgentRequest{.Id = 9, .Operation = "bye"}));

	CHECK(session.HasEnded());
}

TEST_CASE("Bytes that are not frames end the session without a reply")
{
	Agent::AgentSession session{"v"};

	session.Receive(std::string("\xFF\xFF\xFF\xFF garbage", 12));

	CHECK(session.HasEnded());
	CHECK(session.TakeOutput().empty());
}

TEST_CASE("A change under a folder whose name is not UTF-8 is reported as its nearest UTF-8 folder, and the agent goes on")
{
	TempDirectory directory;
	Agent::AgentSession session{"v"};

	const Core::AgentReply watched = Ask(session, 3, "watch", json{{"root", directory.GetPath().string()}}); // not a path conversion
	REQUIRE_FALSE(watched.Failure.has_value());

	const std::filesystem::path oddFolder = directory.GetPath() / std::string{"caf\xE9"};
	std::filesystem::create_directory(oddFolder);
	std::this_thread::sleep_for(std::chrono::milliseconds{100});
	session.Tick();
	std::ofstream{oddFolder / "inside.txt"} << "x";

	std::vector<Core::AgentMessage> events;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};

	while (events.empty() && std::chrono::steady_clock::now() < deadline)
	{
		session.Tick();
		events = Decode(session.TakeOutput());
		std::this_thread::sleep_for(std::chrono::milliseconds{20});
	}

	REQUIRE_FALSE(events.empty());
	const auto* event = std::get_if<Core::AgentEvent>(&events.back());
	REQUIRE(event != nullptr);
	CHECK(event->Body.at("directories") == json::array({directory.GetPath().string()})); // not a path conversion

	// Still answering.
	const Core::AgentReply hello = Ask(session, 4, "hello", json{{"protocol", Core::kAgentProtocolVersion}});
	CHECK_FALSE(hello.Failure.has_value());
}

TEST_CASE("Arguments of the wrong type are an invalid argument, not the end of the agent")
{
	Agent::AgentSession session{"v"};

	const Core::AgentReply unwatched = Ask(session, 1, "unwatch", json{{"watch", "not a number"}});
	REQUIRE(unwatched.Failure.has_value());
	CHECK(unwatched.Failure->Code == Core::ErrorCode::InvalidArgument);

	const Core::AgentReply hello = Ask(session, 2, "hello", json{{"protocol", "one"}});
	REQUIRE(hello.Failure.has_value());
	CHECK_FALSE(session.HasEnded());
}
