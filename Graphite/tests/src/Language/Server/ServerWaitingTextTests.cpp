#include <doctest/doctest.h>

#include "Language/Server/ServerWaitingText.h"

#include <string>

namespace {

	Graphite::ServerStatus StatusOf(Graphite::ServerState state, std::string name,
									Graphite::LanguageId language = Graphite::LanguageId::Cpp)
	{
		return Graphite::ServerStatus{.State = state, .Name = std::move(name), .Language = language};
	}

	Graphite::ServerReply FailedReply(Graphite::ReplyFailure reason, Graphite::ServerStatus server)
	{
		Graphite::ServerReply reply;
		reply.Failed = true;
		reply.FailedBecause = reason;
		reply.ServerAtFailure = std::move(server);

		return reply;
	}
}

TEST_CASE("A server that is starting or loading its projects is coming up, and one indexing answers")
{
	CHECK(Graphite::AvailabilityOf(StatusOf(Graphite::ServerState::Starting, "clangd")) == Graphite::ServerAvailability::ComingUp);

	Graphite::ServerStatus loading = StatusOf(Graphite::ServerState::Indexing, "Roslyn");
	loading.Loading = "Graphite.csproj";

	CHECK(Graphite::AvailabilityOf(loading) == Graphite::ServerAvailability::ComingUp);
	CHECK_FALSE(Graphite::IsStillIndexing(loading));

	Graphite::ServerStatus indexing = StatusOf(Graphite::ServerState::Indexing, "clangd");
	indexing.Percent = 40;

	CHECK(Graphite::AvailabilityOf(indexing) == Graphite::ServerAvailability::Answers);
	CHECK(Graphite::IsStillIndexing(indexing));

	CHECK(Graphite::AvailabilityOf(StatusOf(Graphite::ServerState::Ready, "clangd")) == Graphite::ServerAvailability::Answers);
	CHECK(Graphite::AvailabilityOf(StatusOf(Graphite::ServerState::ProjectIncomplete, "clangd")) ==
		  Graphite::ServerAvailability::Answers);
}

TEST_CASE("A server that cannot run, stopped or is not chosen is unavailable")
{
	for (const Graphite::ServerState state :
		 {Graphite::ServerState::NotConfigured, Graphite::ServerState::Off, Graphite::ServerState::NotFound,
		  Graphite::ServerState::NotInstalled, Graphite::ServerState::MissingRuntime, Graphite::ServerState::Failed,
		  Graphite::ServerState::Restricted})
	{
		CAPTURE(static_cast<int>(state));
		CHECK(Graphite::AvailabilityOf(StatusOf(state, "clangd")) == Graphite::ServerAvailability::Unavailable);
	}
}

TEST_CASE("A wait on a server names what the server is doing")
{
	CHECK(Graphite::ServerWaitingText(StatusOf(Graphite::ServerState::Starting, "clangd")) == "waiting for clangd to start");

	Graphite::ServerStatus loading = StatusOf(Graphite::ServerState::Indexing, "Roslyn");
	loading.Loading = "Graphite.csproj";

	CHECK(Graphite::ServerWaitingText(loading) == "waiting for Roslyn to load Graphite.csproj");

	Graphite::ServerStatus indexing = StatusOf(Graphite::ServerState::Indexing, "clangd");
	indexing.Percent = 40;

	CHECK(Graphite::ServerWaitingText(indexing) == "waiting for clangd to index (40%)");
	CHECK(Graphite::StillIndexingText(indexing) == "clangd is still indexing (40%)");
}

TEST_CASE("A server with no name yet is waited for as the language server")
{
	CHECK(Graphite::ServerWaitingText(StatusOf(Graphite::ServerState::Starting, "")) ==
		  "waiting for the language server to start");
}

TEST_CASE("An unavailable server says why in the same words everywhere")
{
	CHECK(Graphite::ServerUnavailableText(StatusOf(Graphite::ServerState::Failed, "clangd")) == "clangd stopped");
	CHECK(Graphite::ServerUnavailableText(StatusOf(Graphite::ServerState::NotInstalled, "clangd")) == "clangd not installed");
	CHECK(Graphite::ServerUnavailableText(StatusOf(Graphite::ServerState::NotFound, "clangd")) == "clangd not found");
	CHECK(Graphite::ServerUnavailableText(StatusOf(Graphite::ServerState::Restricted, "clangd")) == "this folder is not trusted");
	CHECK(Graphite::ServerUnavailableText(StatusOf(Graphite::ServerState::Off, "C++")) == "no language server is chosen for C++");
	CHECK(Graphite::ServerUnavailableText(StatusOf(Graphite::ServerState::NotConfigured, "", Graphite::LanguageId::CSharp)) ==
		  "no language server is chosen for C#");

	Graphite::ServerStatus missingRuntime = StatusOf(Graphite::ServerState::MissingRuntime, "Roslyn");
	missingRuntime.Detail = "needs .NET 9";

	CHECK(Graphite::ServerUnavailableText(missingRuntime) == "Roslyn needs .NET 9");
}

TEST_CASE("A failed reply says whether the server erred, fell silent or could not be asked")
{
	const Graphite::ServerStatus ready = StatusOf(Graphite::ServerState::Ready, "clangd");

	CHECK(Graphite::ReplyFailureText(FailedReply(Graphite::ReplyFailure::ServerError, ready)) == "clangd returned an error");
	CHECK(Graphite::ReplyFailureText(FailedReply(Graphite::ReplyFailure::TimedOut, ready)) == "clangd did not answer");
	CHECK(Graphite::ReplyFailureText(FailedReply(Graphite::ReplyFailure::ServerUnavailable, ready)) == "clangd stopped");
	CHECK(Graphite::ReplyFailureText(FailedReply(Graphite::ReplyFailure::ServerUnavailable,
												 StatusOf(Graphite::ServerState::NotInstalled, "clangd"))) ==
		  "clangd not installed");
	CHECK(Graphite::ReplyFailureText(FailedReply(Graphite::ReplyFailure::ServerUnavailable,
												 StatusOf(Graphite::ServerState::Starting, "Roslyn"))) ==
		  "waiting for Roslyn to start");
}

TEST_CASE("A phrase becomes a sentence unless it opens on the server's own name")
{
	const Graphite::ServerStatus clangd = StatusOf(Graphite::ServerState::Failed, "clangd");

	CHECK(Graphite::AsSentence("waiting for clangd to start", clangd) == "Waiting for clangd to start");
	CHECK(Graphite::AsSentence("clangd stopped", clangd) == "clangd stopped");
	CHECK(Graphite::AsSentence("", clangd).empty());
}

TEST_CASE("Only a server turned off, not chosen or kept from an untrusted folder leaves nothing to wait for")
{
	CHECK(Graphite::HasNoServerToWaitFor(StatusOf(Graphite::ServerState::Off, "clangd")));
	CHECK(Graphite::HasNoServerToWaitFor(StatusOf(Graphite::ServerState::NotConfigured, "clangd")));
	CHECK(Graphite::HasNoServerToWaitFor(StatusOf(Graphite::ServerState::Restricted, "clangd")));
	CHECK_FALSE(Graphite::HasNoServerToWaitFor(StatusOf(Graphite::ServerState::NotInstalled, "clangd")));
	CHECK_FALSE(Graphite::HasNoServerToWaitFor(StatusOf(Graphite::ServerState::Failed, "clangd")));
}
