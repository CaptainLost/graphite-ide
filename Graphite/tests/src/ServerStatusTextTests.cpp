#include <doctest/doctest.h>

#include "Editor/ServerStatusText.h"

TEST_CASE("A language with no server contributes nothing to the bar")
{
	Graphite::ServerStatus status;
	status.State = Graphite::ServerState::NotConfigured;

	CHECK(Graphite::ServerStatusText(status).empty());
}

TEST_CASE("Each running state names the server and what it is doing")
{
	Graphite::ServerStatus status;
	status.Name = "clangd";

	status.State = Graphite::ServerState::Starting;
	CHECK(Graphite::ServerStatusText(status) == "clangd: starting");

	status.State = Graphite::ServerState::Indexing;
	status.Percent = 42;
	CHECK(Graphite::ServerStatusText(status) == "clangd: indexing 42%");

	status.State = Graphite::ServerState::Ready;
	CHECK(Graphite::ServerStatusText(status) == "clangd: ready");

	status.State = Graphite::ServerState::NotFound;
	CHECK(Graphite::ServerStatusText(status) == "clangd: not found");

	status.State = Graphite::ServerState::NoCompileDatabase;
	CHECK(Graphite::ServerStatusText(status) == "clangd: no compile database");

	status.State = Graphite::ServerState::Failed;
	CHECK(Graphite::ServerStatusText(status) == "clangd: failed");
}

TEST_CASE("Indexing without a reported percentage omits the number")
{
	Graphite::ServerStatus status;
	status.Name = "clangd";
	status.State = Graphite::ServerState::Indexing;
	status.Percent = -1;

	CHECK(Graphite::ServerStatusText(status) == "clangd: indexing");
}

TEST_CASE("The detail explains what is missing rather than repeating the state")
{
	Graphite::ServerStatus status;
	status.Name = "clangd";
	status.State = Graphite::ServerState::NoCompileDatabase;

	const std::string detail = Graphite::ServerStatusDetail(status);

	CHECK(detail.find("compile_commands.json") != std::string::npos);
	CHECK(detail.find("compile_flags.txt") != std::string::npos);
}

TEST_CASE("The detail reports restarts once there have been any")
{
	Graphite::ServerStatus status;
	status.Name = "clangd";
	status.State = Graphite::ServerState::Ready;
	status.Detail = "clangd version 19.1.0";
	status.Restarts = 2;

	CHECK(Graphite::ServerStatusDetail(status).find("restarted 2") != std::string::npos);
}
