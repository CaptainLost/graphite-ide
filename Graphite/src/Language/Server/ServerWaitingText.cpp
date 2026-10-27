#include "Language/Server/ServerWaitingText.h"

#include "Language/Registry/LanguageRegistry.h"

#include <string_view>

namespace Graphite {

	namespace {
		constexpr std::string_view kUnnamedServer = "the language server";

		std::string WaitingFor(const ServerStatus& status, std::string_view what)
		{
			return "waiting for " + ServerNameOf(status) + " to " + std::string{what};
		}

		std::string PercentNote(const ServerStatus& status)
		{
			if (status.Percent <= kNoPercentReported)
			{
				return {};
			}

			return " (" + std::to_string(status.Percent) + "%)";
		}

		std::string NoServerChosenText(LanguageId language)
		{
			const std::string_view name = LanguageDisplayName(language);

			if (name.empty())
			{
				return "no language server is chosen for this file";
			}

			return "no language server is chosen for " + std::string{name};
		}

		bool IsLoadingProjects(const ServerStatus& status)
		{
			return status.State == ServerState::Indexing && !status.Loading.empty();
		}

		char UpperAscii(char character)
		{
			const bool isAsciiLowercase = character >= 'a' && character <= 'z';

			return isAsciiLowercase ? static_cast<char>(character - 'a' + 'A') : character;
		}
	}

	ServerAvailability AvailabilityOf(const ServerStatus& status)
	{
		switch (status.State)
		{
		case ServerState::Ready:
		case ServerState::ProjectIncomplete:
			return ServerAvailability::Answers;

		case ServerState::Indexing:
			return IsLoadingProjects(status) ? ServerAvailability::ComingUp : ServerAvailability::Answers;

		case ServerState::Starting:
			return ServerAvailability::ComingUp;

		case ServerState::NotConfigured:
		case ServerState::Off:
		case ServerState::NotFound:
		case ServerState::NotInstalled:
		case ServerState::MissingRuntime:
		case ServerState::Failed:
		case ServerState::Restricted:
			break;
		}

		return ServerAvailability::Unavailable;
	}

	bool IsStillIndexing(const ServerStatus& status)
	{
		return status.State == ServerState::Indexing && !IsLoadingProjects(status);
	}

	bool HasNoServerToWaitFor(const ServerStatus& status)
	{
		return status.State == ServerState::Off || status.State == ServerState::NotConfigured ||
			   status.State == ServerState::Restricted;
	}

	std::string ServerNameOf(const ServerStatus& status)
	{
		return status.Name.empty() ? std::string{kUnnamedServer} : status.Name;
	}

	std::string ServerWaitingText(const ServerStatus& status)
	{
		if (IsLoadingProjects(status))
		{
			return WaitingFor(status, "load " + status.Loading);
		}

		if (status.State == ServerState::Indexing)
		{
			return WaitingFor(status, "index" + PercentNote(status));
		}

		return WaitingFor(status, "start");
	}

	std::string ServerUnavailableText(const ServerStatus& status)
	{
		switch (status.State)
		{
		case ServerState::Failed:
			return ServerNameOf(status) + " stopped";

		case ServerState::NotInstalled:
			return ServerNameOf(status) + " not installed";

		case ServerState::NotFound:
			return ServerNameOf(status) + " not found";

		case ServerState::MissingRuntime:
			return ServerNameOf(status) + " " + status.Detail;

		case ServerState::Restricted:
			return "this folder is not trusted";

		case ServerState::Off:
		case ServerState::NotConfigured:
			return NoServerChosenText(status.Language);

		case ServerState::Starting:
		case ServerState::Indexing:
		case ServerState::Ready:
		case ServerState::ProjectIncomplete:
			break;
		}

		return ServerNameOf(status) + " stopped";
	}

	std::string StillIndexingText(const ServerStatus& status)
	{
		return ServerNameOf(status) + " is still indexing" + PercentNote(status);
	}

	std::string ReplyFailureText(const ServerReply& reply)
	{
		const ServerStatus& server = reply.ServerAtFailure;

		switch (reply.FailedBecause)
		{
		case ReplyFailure::ServerError:
			return ServerNameOf(server) + " returned an error";

		case ReplyFailure::TimedOut:
			return ServerNameOf(server) + " did not answer";

		case ReplyFailure::ServerUnavailable:
			break;
		}

		if (AvailabilityOf(server) == ServerAvailability::ComingUp)
		{
			return ServerWaitingText(server);
		}

		return ServerUnavailableText(server);
	}

	std::string AsSentence(const std::string& phrase, const ServerStatus& status)
	{
		if (phrase.empty() || (!status.Name.empty() && phrase.starts_with(status.Name)))
		{
			return phrase;
		}

		std::string sentence = phrase;
		sentence[0] = UpperAscii(sentence[0]);

		return sentence;
	}
}
