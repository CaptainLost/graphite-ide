#include "Editor/Overlays/LensWaiting.h"

#include "UI/TypographicMarks.h"

namespace Graphite {

	namespace {
		constexpr std::string_view kUnnamedServer = "the language server";

		std::string ServerName(const ServerStatus& status)
		{
			return status.Name.empty() ? std::string{kUnnamedServer} : status.Name;
		}

		std::string WaitingFor(const ServerStatus& status, std::string_view what)
		{
			return "waiting for " + ServerName(status) + " to " + std::string{what};
		}
	}

	std::string LensWaitingText(const ServerStatus& status)
	{
		switch (status.State)
		{
		case ServerState::Starting:
			return WaitingFor(status, "start");

		case ServerState::Indexing:
			if (status.Percent > kNoPercentReported)
			{
				return WaitingFor(status, "index (" + std::to_string(status.Percent) + "%)");
			}

			if (!status.Loading.empty())
			{
				return WaitingFor(status, "load " + status.Loading);
			}

			return WaitingFor(status, "index");

		case ServerState::Ready:
		case ServerState::ProjectIncomplete:
			return "counting references" + std::string{kEllipsis};

		case ServerState::Failed:
			return ServerName(status) + " stopped";

		case ServerState::NotInstalled:
			return ServerName(status) + " not installed";

		case ServerState::NotFound:
			return ServerName(status) + " not found";

		case ServerState::MissingRuntime:
			return ServerName(status) + " " + status.Detail;

		case ServerState::NotConfigured:
		case ServerState::Off:
		case ServerState::Restricted:
			break;
		}

		return {};
	}
}
