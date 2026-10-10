#include <doctest/doctest.h>

#include "Language/Server/Adapters/ServerAdapter.h"
#include "Language/Server/LspProtocol.h"
#include "Language/Server/ServerCatalog.h"

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace {

	const Graphite::ServerAdapter& Roslyn()
	{
		return Graphite::AdapterOf(Graphite::AdapterKind::Roslyn);
	}

	Graphite::AdapterContext ContextWith(std::optional<std::filesystem::path> solution)
	{
		return Graphite::AdapterContext{.Root = "D:/Work",
										.InitializationOptions = nullptr,
										.Settings = nullptr,
										.Solution = std::move(solution)};
	}
}

TEST_CASE("Roslyn is the adapter of the roslyn row and the one that follows the selected solution")
{
	CHECK(Graphite::FindServer("roslyn")->Adapter == Graphite::AdapterKind::Roslyn);
	CHECK(Roslyn().FollowsSelectedSolution());
	CHECK_FALSE(Graphite::AdapterOf(Graphite::AdapterKind::Plain).FollowsSelectedSolution());
	CHECK_FALSE(Graphite::AdapterOf(Graphite::AdapterKind::Clangd).FollowsSelectedSolution());
}

TEST_CASE("Roslyn is told to open the solution once initialized")
{
	const std::filesystem::path solution{L"D:/Work/Demo.slnx"};
	std::vector<Graphite::OutgoingNotification> notifications;

	Roslyn().AfterInitialized(ContextWith(solution), notifications);

	REQUIRE(notifications.size() == 1);
	CHECK(notifications[0].Method == "solution/open");
	CHECK(notifications[0].Params == nlohmann::json{{"solution", Graphite::ToFileUri(solution)}});
	CHECK(Roslyn().LoadsProjectsFrom(ContextWith(solution)) == std::optional<std::string>{"Demo.slnx"});
	CHECK_FALSE(Roslyn().ProjectProblemAt(ContextWith(solution)).has_value());
}

TEST_CASE("Roslyn without a solution reports no solution and sends no solution/open")
{
	std::vector<Graphite::OutgoingNotification> notifications;

	Roslyn().AfterInitialized(ContextWith(std::nullopt), notifications);

	CHECK(notifications.empty());
	CHECK_FALSE(Roslyn().LoadsProjectsFrom(ContextWith(std::nullopt)).has_value());

	const std::optional<Graphite::ProjectProblem> problem = Roslyn().ProjectProblemAt(ContextWith(std::nullopt));
	REQUIRE(problem.has_value());
	CHECK(problem->Summary == "no solution");
}

TEST_CASE("Roslyn's projects are loaded when it says their initialization is complete")
{
	const nlohmann::json noParams;

	CHECK(Roslyn().HandleNotification(ContextWith(std::nullopt), "workspace/projectInitializationComplete", noParams) ==
		  Graphite::NotificationEffect::ProjectsLoaded);
	CHECK(Roslyn().HandleNotification(ContextWith(std::nullopt), "window/logMessage", noParams) ==
		  Graphite::NotificationEffect::None);
}
