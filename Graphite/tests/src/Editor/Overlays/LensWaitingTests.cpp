#include <doctest/doctest.h>

#include "Editor/Overlays/LensWaiting.h"

#include <string>

namespace {

	Graphite::ServerStatus StatusOf(Graphite::ServerState state, std::string name)
	{
		return Graphite::ServerStatus{.State = state, .Name = std::move(name)};
	}
}

TEST_CASE("A lens under a server that is starting waits for that server by name")
{
	CHECK(Graphite::LensWaitingText(StatusOf(Graphite::ServerState::Starting, "clangd")) == "waiting for clangd to start");
}

TEST_CASE("A lens under a server with no name yet waits for the language server")
{
	CHECK(Graphite::LensWaitingText(StatusOf(Graphite::ServerState::Starting, "")) == "waiting for the language server to start");
}

TEST_CASE("A lens under an index says how far the index has got when the server tells")
{
	Graphite::ServerStatus indexing = StatusOf(Graphite::ServerState::Indexing, "clangd");

	CHECK(Graphite::LensWaitingText(indexing) == "waiting for clangd to index");

	indexing.Percent = 42;

	CHECK(Graphite::LensWaitingText(indexing) == "waiting for clangd to index (42%)");
}

TEST_CASE("A lens under a solution being loaded names the solution")
{
	Graphite::ServerStatus loading = StatusOf(Graphite::ServerState::Indexing, "Roslyn");
	loading.Loading = "Game.sln";

	CHECK(Graphite::LensWaitingText(loading) == "waiting for Roslyn to load Game.sln");
}

TEST_CASE("A lens under a server that answers is being counted")
{
	CHECK(Graphite::LensWaitingText(StatusOf(Graphite::ServerState::Ready, "clangd")) == "counting references\xE2\x80\xA6");
	CHECK(Graphite::LensWaitingText(StatusOf(Graphite::ServerState::ProjectIncomplete, "Roslyn")) ==
		  "counting references\xE2\x80\xA6");
}

TEST_CASE("A lens under a server that stopped or cannot run says why no count comes")
{
	CHECK(Graphite::LensWaitingText(StatusOf(Graphite::ServerState::Failed, "clangd")) == "clangd stopped");
	CHECK(Graphite::LensWaitingText(StatusOf(Graphite::ServerState::NotInstalled, "clangd")) == "clangd not installed");
	CHECK(Graphite::LensWaitingText(StatusOf(Graphite::ServerState::NotFound, "clangd")) == "clangd not found");

	Graphite::ServerStatus missingRuntime = StatusOf(Graphite::ServerState::MissingRuntime, "Roslyn");
	missingRuntime.Detail = "needs .NET 9";

	CHECK(Graphite::LensWaitingText(missingRuntime) == "Roslyn needs .NET 9");
}

TEST_CASE("A lens with no server to wait for says nothing")
{
	for (const Graphite::ServerState state :
		 {Graphite::ServerState::Off, Graphite::ServerState::NotConfigured, Graphite::ServerState::Restricted})
	{
		CAPTURE(static_cast<int>(state));
		CHECK(Graphite::LensWaitingText(StatusOf(state, "clangd")).empty());
	}
}
