#include <doctest/doctest.h>

#include "Wsl/WslAgents.h"
#include "Wsl/WslSearch.h"

#include "FakeAgent.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Jobs/CancellationToken.h"
#include "Core/Platform/Process.h"
#include "Core/Remote/AgentDeployment.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <process.h>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using AgentFixtures::FakeAgent;

namespace {

	// One fake agent; the test's handler answers each search request.
	struct FakeSearchDistro
	{
		FakeAgent::Handler Answer;
		std::mutex Mutex;
		std::unique_ptr<FakeAgent> Agent;
		std::string LastOperation;
		nlohmann::json LastArguments;

		Graphite::WslAgents::Connector Connector()
		{
			return [this](const std::string&) -> Core::Result<std::shared_ptr<Core::AgentConnection>> {
				auto agent = std::make_unique<FakeAgent>([this](const Core::AgentRequest& request, FakeAgent& self) {
					{
						std::scoped_lock lock{Mutex};
						LastOperation = request.Operation;
						LastArguments = request.Arguments;
					}
					Answer(request, self);
				});
				std::shared_ptr<Core::AgentConnection> connection{Core::AgentConnection::Open(agent->HostEnd(), "v1")};
				connection->WaitUntilSettled(2s);
				std::scoped_lock lock{Mutex};
				Agent = std::move(agent);
				return connection;
			};
		}

		nlohmann::json Arguments()
		{
			std::scoped_lock lock{Mutex};
			return LastArguments;
		}
	};

	const Core::WslSharePath kRoot{"Ubuntu", "/home/me/p"};
}

TEST_CASE("A WSL folder's files are listed by its distro's agent, relative to the folder")
{
	FakeSearchDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		self.Reply(request.Id, {{"count", 2}}, std::string{"a.txt\0src/b.cpp\0", 16});
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslSearch search{agents, kRoot};

	const Core::Result<std::vector<std::string>> files = search.ListFiles(false, Core::CancellationToken{});

	REQUIRE_MESSAGE(files.HasValue(), (files ? "" : files.GetError().Message));
	CHECK(files.GetValue() == std::vector<std::string>{"a.txt", "src/b.cpp"});
	CHECK(distro.LastOperation == "files.list");
	CHECK(distro.Arguments().at("root") == "/home/me/p");
	CHECK(distro.Arguments().at("respectGitIgnore") == false);
}

TEST_CASE("A WSL folder's search hits come back in the share spelling with their lines and columns")
{
	FakeSearchDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		const nlohmann::json hits = nlohmann::json::array({{{"file", "src/b.cpp"}, {"line", 2}, {"text", "int needle = 1;"}, {"from", 4}, {"to", 10}}});
		self.Reply(request.Id, {{"scanned", 7}, {"skipped", 2}, {"unreadable", 1}, {"capped", true}}, hits.dump());
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslSearch search{agents, kRoot};

	const Core::Result<Graphite::FolderSearchFound> found =
		search.SearchText(Graphite::SearchQuery{.Needle = "needle", .Mask = "*.cpp", .MatchCase = true, .WholeWord = true}, true, {}, Core::CancellationToken{});

	REQUIRE_MESSAGE(found.HasValue(), (found ? "" : found.GetError().Message));
	REQUIRE(found.GetValue().Hits.size() == 1);
	const Graphite::SearchHit& hit = found.GetValue().Hits.front();
	CHECK(hit.File.native() == std::filesystem::path{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p\\src\\b.cpp"}.native());
	CHECK(hit.Line == 2);
	CHECK(hit.Text == "int needle = 1;");
	CHECK(hit.From == 4);
	CHECK(hit.To == 10);
	CHECK(found.GetValue().Scanned == 7);
	CHECK(found.GetValue().Skipped == 2);
	CHECK(found.GetValue().Unreadable == 1);
	CHECK(found.GetValue().Capped);

	const nlohmann::json arguments = distro.Arguments();
	CHECK(distro.LastOperation == "search.text");
	CHECK(arguments.at("root") == "/home/me/p");
	CHECK(arguments.at("respectGitIgnore") == true);
	CHECK(arguments.at("query").at("needle") == "needle");
	CHECK(arguments.at("query").at("mask") == "*.cpp");
	CHECK(arguments.at("query").at("matchCase") == true);
	CHECK(arguments.at("query").at("wholeWord") == true);
}

TEST_CASE("A WSL folder's file named with a character Windows forbids is listed and found as the share spells it")
{
	FakeSearchDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		if (request.Operation == "files.list")
		{
			self.Reply(request.Id, {{"count", 1}}, std::string{"logs/run:1.log\0", 15});
			return;
		}

		self.Reply(request.Id, {{"scanned", 1}}, nlohmann::json::array({{{"file", "logs/run:1.log"}, {"line", 0}, {"text", "x"}, {"from", 0}, {"to", 1}}}).dump());
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslSearch search{agents, kRoot};

	const Core::Result<std::vector<std::string>> files = search.ListFiles(true, Core::CancellationToken{});
	REQUIRE(files.HasValue());
	CHECK(files.GetValue() == std::vector<std::string>{"logs/run\xEF\x80\xBA" "1.log"});

	const Core::Result<Graphite::FolderSearchFound> found = search.SearchText(Graphite::SearchQuery{.Needle = "x"}, true, {}, Core::CancellationToken{});
	REQUIRE(found.HasValue());
	REQUIRE(found.GetValue().Hits.size() == 1);
	CHECK(found.GetValue().Hits[0].File.native() == std::filesystem::path{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p\\logs\\run1.log"}.native());
}

TEST_CASE("A WSL folder's search leaves out the open documents it is told to, named relative to the folder")
{
	FakeSearchDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) { self.Reply(request.Id, {{"scanned", 0}}, "[]"); };
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslSearch search{agents, kRoot};

	// One open document in the folder, one in another distro, one beside the folder and one on Windows.
	const std::vector<std::filesystem::path> skip{std::filesystem::path{L"\\\\wsl$\\Ubuntu\\home\\me\\p\\src\\b.cpp"},
												  std::filesystem::path{L"\\\\wsl.localhost\\Debian\\home\\me\\p\\c.txt"},
												  std::filesystem::path{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\pq\\d.txt"},
												  std::filesystem::path{L"C:\\work\\e.txt"}};

	const Core::Result<Graphite::FolderSearchFound> found = search.SearchText(Graphite::SearchQuery{.Needle = "x"}, true, skip, Core::CancellationToken{});

	REQUIRE(found.HasValue());
	CHECK(distro.Arguments().at("skip") == nlohmann::json::array({"src/b.cpp"}));
}

TEST_CASE("A WSL folder's search says why its agent could not search")
{
	FakeSearchDistro distro;
	distro.Answer = [](const Core::AgentRequest& request, FakeAgent& self) {
		self.Send(Core::EncodeReply(Core::AgentReply{.Id = request.Id, .Failure = Core::Error{Core::ErrorCode::NotFound, "no such folder"}}));
	};
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslSearch search{agents, kRoot};

	const Core::Result<Graphite::FolderSearchFound> found = search.SearchText(Graphite::SearchQuery{.Needle = "x"}, true, {}, Core::CancellationToken{});

	REQUIRE_FALSE(found.HasValue());
	CHECK(found.GetError().Code == Core::ErrorCode::NotFound);
	CHECK(found.GetError().Message == "no such folder");
}

TEST_CASE("A WSL folder's search stops waiting for its agent once cancelled")
{
	FakeSearchDistro distro;
	distro.Answer = [](const Core::AgentRequest&, FakeAgent&) {}; // never answers
	Graphite::WslAgents agents{distro.Connector()};
	Graphite::WslSearch search{agents, kRoot};
	REQUIRE(agents.Get("Ubuntu", 2s).HasValue());

	Core::CancellationToken token;
	std::thread canceller{[token]() mutable {
		std::this_thread::sleep_for(100ms);
		token.Cancel();
	}};

	const auto started = std::chrono::steady_clock::now();
	const Core::Result<Graphite::FolderSearchFound> found = search.SearchText(Graphite::SearchQuery{.Needle = "x"}, true, {}, token);
	const auto waited = std::chrono::steady_clock::now() - started;
	canceller.join();

	REQUIRE_FALSE(found.HasValue());
	CHECK(found.GetError().Code == Core::ErrorCode::Cancelled);
	CHECK(waited < 5s);
}

// The real agent in Ubuntu: a folder of files written through the share, listed and searched in the
// distro. Skipped where there is no WSL Ubuntu or no Linux build of the agent.
TEST_CASE("A folder inside WSL is listed and searched through the real agent")
{
	const Core::Result<std::filesystem::path> here = Core::ExecutableDirectory();
	REQUIRE(here.HasValue());
#ifdef CORE_DEBUG
	const std::filesystem::path agentBinary = here.GetValue().parent_path().parent_path() / "Debug-linux-x86_64" / "GraphiteAgent" / "graphite-agent";
#else
	const std::filesystem::path agentBinary = here.GetValue().parent_path().parent_path() / "Release-linux-x86_64" / "GraphiteAgent" / "graphite-agent";
#endif
	std::error_code error;

	if (!Core::IsWslAvailable() || !std::filesystem::exists(agentBinary, error) || !std::filesystem::exists(L"\\\\wsl.localhost\\Ubuntu\\tmp", error))
	{
		MESSAGE("no WSL Ubuntu or no Linux build of graphite-agent here: skipped");
		return;
	}

	// Per process: another test run at the same time would share the folder.
	const std::string name = "graphite-wslsearch-test-" + std::to_string(_getpid());
	const std::filesystem::path folder = std::filesystem::path{L"\\\\wsl.localhost\\Ubuntu\\tmp\\"} / Core::FromUtf8(name);
	std::filesystem::remove_all(folder, error);
	std::filesystem::create_directories(folder / "src", error);
	std::ofstream{folder / "src" / "a.cpp", std::ios::binary} << "int first;\nint needle = 2;\n";
	std::ofstream{folder / "b.txt", std::ios::binary} << "no match here\n";
	// src/run:1.log in the distro: the share spells its ':' as U+F03A.
	const std::filesystem::path colonFile = folder / "src" / L"run1.log";
	std::ofstream{colonFile, std::ios::binary} << "a needle\n";

	Graphite::WslAgents agents{Graphite::WslAgents::ConnectThroughWsl(agentBinary)};
	Graphite::WslSearch search{agents, Core::WslSharePath{"Ubuntu", "/tmp/" + name}};

	Core::Result<std::vector<std::string>> files = search.ListFiles(true, Core::CancellationToken{});
	REQUIRE_MESSAGE(files.HasValue(), (files ? "" : files.GetError().Message));
	std::sort(files.GetValue().begin(), files.GetValue().end());
	CHECK(files.GetValue() == std::vector<std::string>{"b.txt", "src/a.cpp", "src/run\xEF\x80\xBA" "1.log"});

	const Core::Result<Graphite::FolderSearchFound> found = search.SearchText(Graphite::SearchQuery{.Needle = "needle"}, true, {}, Core::CancellationToken{});
	REQUIRE_MESSAGE(found.HasValue(), (found ? "" : found.GetError().Message));
	std::vector<Graphite::SearchHit> hits = found.GetValue().Hits;
	std::sort(hits.begin(), hits.end(), [](const Graphite::SearchHit& left, const Graphite::SearchHit& right) { return left.File < right.File; });
	REQUIRE(hits.size() == 2);
	CHECK(hits[0].File.native() == (folder / "src" / "a.cpp").native());
	CHECK(hits[0].Line == 1);
	CHECK(hits[0].From == 4);
	CHECK(found.GetValue().Scanned == 3);

	// The hit's path opens the file it was found in.
	CHECK(hits[1].File.native() == colonFile.native());
	std::string opened;
	std::getline(std::ifstream{hits[1].File, std::ios::binary}, opened);
	CHECK(opened == "a needle");

	std::filesystem::remove_all(folder, error);
}
