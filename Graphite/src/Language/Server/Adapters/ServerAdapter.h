#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Api/LanguageService.h"
#include "Language/Server/ServerCatalog.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	struct AdapterContext
	{
		std::filesystem::path Root;
		nlohmann::json InitializationOptions;
		nlohmann::json Settings;
		std::optional<std::filesystem::path> Solution;
	};

	struct OutgoingNotification
	{
		std::string Method;
		nlohmann::json Params;
	};

	enum class NotificationEffect : std::uint8_t
	{
		None,
		ProjectsLoaded
	};

	// ProjectProblemAt is the one hook that runs on the UI thread, before the client starts, and may read the disk. The others - WorkspaceConfiguration, AnswerRequest and AfterInitialized - run on the client's reader thread with its protocol lock held, so they must not block or touch the filesystem.
	class ServerAdapter
	{
	public:
		virtual ~ServerAdapter() = default;

		virtual nlohmann::json WorkspaceConfiguration(const AdapterContext& context, std::string_view section) const;

		virtual nlohmann::json InitializationOptions(const AdapterContext& context) const;

		virtual void AfterInitialized(const AdapterContext& context, std::vector<OutgoingNotification>& notifications) const;

		virtual std::optional<nlohmann::json> AnswerRequest(const AdapterContext& context, std::string_view method,
															 const nlohmann::json& params) const;

		virtual NotificationEffect HandleNotification(const AdapterContext& context, std::string_view method,
													  const nlohmann::json& params) const;

		// Nothing when the server can answer as soon as its handshake ends.
		virtual std::optional<std::string> LoadsProjectsFrom(const AdapterContext& context) const;

		virtual std::optional<ProjectProblem> ProjectProblemAt(const AdapterContext& context) const;

		virtual bool WarmUpNeedsOpen() const;

		virtual bool SupportsSwitchSourceHeader() const;

		virtual bool AnswersEmptyConfigurationWithOneEntry() const;

		// A server that loads the solution it is handed must restart when the user selects another.
		virtual bool FollowsSelectedSolution() const;
	};

	const ServerAdapter& AdapterOf(AdapterKind kind);
}
