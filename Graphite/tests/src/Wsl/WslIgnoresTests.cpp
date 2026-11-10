#include <doctest/doctest.h>

#include "Wsl/WslAgents.h"
#include "Wsl/WslIgnores.h"

#include "FakeAgent.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using AgentFixtures::FakeAgent;

namespace {

	const std::filesystem::path kFolder{L"\\\\wsl.localhost\\Ubuntu\\home\\me\\p"};

	// A distro whose agent ignores every name starting with "build", and a folder whose listing the
	// test sets.
	struct IgnoringDistro
	{
		std::mutex Mutex;
		std::vector<std::string> Operations;
		std::unique_ptr<FakeAgent> Agent;
		bool Answers = true;
		bool Silent = false;
		int Listings = 0;
		std::vector<std::string> Listed{"build", "a.txt", "b.txt"};

		Graphite::WslAgents::Connector Connector()
		{
			return [this](const std::string&) -> Core::Result<std::shared_ptr<Core::AgentConnection>> {
				auto agent = std::make_unique<FakeAgent>([this](const Core::AgentRequest& request, FakeAgent& self) {
					bool answers = true;
					bool silent = false;
					{
						std::scoped_lock lock{Mutex};
						Operations.push_back(request.Operation);
						answers = Answers;
						silent = Silent && request.Operation == "git.ignored";
					}

					// Never answered: the host runs out of time.
					if (silent)
					{
						return;
					}

					if (!answers)
					{
						self.Send(Core::EncodeReply(Core::AgentReply{.Id = request.Id, .Failure = Core::Error{Core::ErrorCode::IoFailure, "broken"}}));
						return;
					}

					if (request.Operation == "git.ignoreSetup")
					{
						self.Reply(request.Id, {{"workingDirectory", "/home/me/p"}, {"ruleFiles", nlohmann::json::array()}});
						return;
					}

					nlohmann::json ignored = nlohmann::json::array();

					for (const nlohmann::json& entry : request.Arguments.at("entries"))
					{
						ignored.push_back(entry.at("name").get<std::string>().starts_with("build"));
					}

					self.Reply(request.Id, {{"ignored", std::move(ignored)}});
				});
				std::shared_ptr<Core::AgentConnection> connection{Core::AgentConnection::Open(agent->HostEnd(), "v1")};
				connection->WaitUntilSettled(2s);
				std::scoped_lock lock{Mutex};
				Agent = std::move(agent);
				return connection;
			};
		}

		Graphite::WslIgnores::Lister Lister()
		{
			return [this](const std::filesystem::path& folder) -> Core::Result<std::vector<Core::DirectoryEntry>> {
				std::scoped_lock lock{Mutex};
				++Listings;
				std::vector<Core::DirectoryEntry> entries;

				for (const std::string& name : Listed)
				{
					entries.push_back(Core::DirectoryEntry{.Path = folder / Core::FromUtf8(name), .Name = name, .IsDirectory = name == "build"});
				}

				return entries;
			};
		}

		std::size_t Count(const std::string& operation)
		{
			std::scoped_lock lock{Mutex};
			return static_cast<std::size_t>(std::count(Operations.begin(), Operations.end(), operation));
		}
	};

	Core::Scope<Graphite::RepositoryIgnores> OpenReady(Graphite::WslAgents& agents, IgnoringDistro& distro)
	{
		REQUIRE(agents.Get("Ubuntu", 2s).HasValue());
		Core::Result<Core::Scope<Graphite::RepositoryIgnores>> opened =
			Graphite::WslIgnores::Open(agents, Core::WslSharePath{"Ubuntu", "/home/me/p"}, distro.Lister());
		REQUIRE_MESSAGE(opened.HasValue(), (opened ? "" : opened.GetError().Message));
		return std::move(opened.GetValue());
	}
}

TEST_CASE("A WSL folder's ignores know the repository's working directory in the share spelling")
{
	IgnoringDistro distro;
	Graphite::WslAgents agents{distro.Connector()};

	const Core::Scope<Graphite::RepositoryIgnores> ignores = OpenReady(agents, distro);

	CHECK(ignores->GetWorkingDirectory().native() == kFolder.native());
}

TEST_CASE("A WSL folder's entries are asked about in one request")
{
	IgnoringDistro distro;
	Graphite::WslAgents agents{distro.Connector()};
	const Core::Scope<Graphite::RepositoryIgnores> ignores = OpenReady(agents, distro);

	CHECK(ignores->IsIgnored(kFolder / "build", true));
	CHECK_FALSE(ignores->IsIgnored(kFolder / "a.txt", false));
	CHECK_FALSE(ignores->IsIgnored(kFolder / "b.txt", false));

	CHECK(distro.Count("git.ignored") == 1);
}

TEST_CASE("A name the folder's answer did not list is asked again")
{
	IgnoringDistro distro;
	Graphite::WslAgents agents{distro.Connector()};
	const Core::Scope<Graphite::RepositoryIgnores> ignores = OpenReady(agents, distro);
	REQUIRE_FALSE(ignores->IsIgnored(kFolder / "a.txt", false));

	{
		std::scoped_lock lock{distro.Mutex};
		distro.Listed.push_back("build.log");
	}

	CHECK(ignores->IsIgnored(kFolder / "build.log", false));
	CHECK(distro.Count("git.ignored") == 2);
}

TEST_CASE("Forgotten ignore answers are asked again")
{
	IgnoringDistro distro;
	Graphite::WslAgents agents{distro.Connector()};
	const Core::Scope<Graphite::RepositoryIgnores> ignores = OpenReady(agents, distro);
	REQUIRE(ignores->IsIgnored(kFolder / "build", true));

	ignores->ForgetAnswers();

	CHECK(ignores->IsIgnored(kFolder / "build", true));
	CHECK(distro.Count("git.ignored") == 2);
}

TEST_CASE("An agent that cannot answer leaves a folder's entries not ignored, asking once")
{
	IgnoringDistro distro;
	Graphite::WslAgents agents{distro.Connector()};
	const Core::Scope<Graphite::RepositoryIgnores> ignores = OpenReady(agents, distro);

	{
		std::scoped_lock lock{distro.Mutex};
		distro.Answers = false;
	}

	CHECK_FALSE(ignores->IsIgnored(kFolder / "build", true));
	CHECK_FALSE(ignores->IsIgnored(kFolder / "a.txt", false));
	CHECK(distro.Count("git.ignored") == 1);
}

TEST_CASE("A WSL folder's ignores are not ready while the distro's agent is still starting, and fail once it could not start")
{
	Graphite::WslAgents agents{[](const std::string&) -> Core::Result<std::shared_ptr<Core::AgentConnection>> {
		std::this_thread::sleep_for(300ms);
		return Core::Error{Core::ErrorCode::IoFailure, "no WSL here"};
	}};

	const Core::Result<Core::Scope<Graphite::RepositoryIgnores>> starting = Graphite::WslIgnores::Open(agents, Core::WslSharePath{"Ubuntu", "/home/me/p"});
	REQUIRE_FALSE(agents.Get("Ubuntu", 2s).HasValue());
	const Core::Result<Core::Scope<Graphite::RepositoryIgnores>> failed = Graphite::WslIgnores::Open(agents, Core::WslSharePath{"Ubuntu", "/home/me/p"});

	REQUIRE_FALSE(starting.HasValue());
	CHECK(starting.GetError().Code == Core::ErrorCode::Cancelled);
	REQUIRE_FALSE(failed.HasValue());
	CHECK(failed.GetError().Code == Core::ErrorCode::IoFailure);
}

TEST_CASE("A folder offered with its entries is asked about without listing it again")
{
	IgnoringDistro distro;
	Graphite::WslAgents agents{distro.Connector()};
	const Core::Scope<Graphite::RepositoryIgnores> ignores = OpenReady(agents, distro);
	const std::vector<Core::DirectoryEntry> entries{Core::DirectoryEntry{.Path = kFolder / "build", .Name = "build", .IsDirectory = true},
													Core::DirectoryEntry{.Path = kFolder / "a.txt", .Name = "a.txt"}};

	ignores->PrepareFolder(kFolder, entries);

	CHECK(ignores->IsIgnored(kFolder / "build", true));
	CHECK_FALSE(ignores->IsIgnored(kFolder / "a.txt", false));
	CHECK(distro.Count("git.ignored") == 1);
	std::scoped_lock lock{distro.Mutex};
	CHECK(distro.Listings == 0);
}

TEST_CASE("An entry that became a folder of the same name is asked about again")
{
	IgnoringDistro distro;
	Graphite::WslAgents agents{distro.Connector()};
	const Core::Scope<Graphite::RepositoryIgnores> ignores = OpenReady(agents, distro);
	REQUIRE_FALSE(ignores->IsIgnored(kFolder / "a.txt", false));

	static_cast<void>(ignores->IsIgnored(kFolder / "a.txt", true));

	CHECK(distro.Count("git.ignored") == 2);
}

TEST_CASE("After a question runs out of time no other folder is asked until the answers are forgotten")
{
	IgnoringDistro distro;
	Graphite::WslAgents agents{distro.Connector()};
	const Core::Scope<Graphite::RepositoryIgnores> ignores = OpenReady(agents, distro);
	{
		std::scoped_lock lock{distro.Mutex};
		distro.Silent = true;
	}

	CHECK_FALSE(ignores->IsIgnored(kFolder / "build", true));
	CHECK_FALSE(ignores->IsIgnored(kFolder / "sub" / "build", true));
	CHECK(distro.Count("git.ignored") == 1);

	{
		std::scoped_lock lock{distro.Mutex};
		distro.Silent = false;
	}
	ignores->ForgetAnswers();

	CHECK(ignores->IsIgnored(kFolder / "build", true));
}
