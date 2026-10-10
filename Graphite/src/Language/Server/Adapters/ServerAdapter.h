#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Api/LanguageService.h"
#include "Language/Server/ServerCatalog.h"

#include <nlohmann/json.hpp>

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

		virtual std::optional<ProjectProblem> ProjectProblemAt(const AdapterContext& context) const;

		virtual bool WarmUpNeedsOpen() const;

		virtual bool SupportsSwitchSourceHeader() const;

		virtual bool AnswersEmptyConfigurationWithOneEntry() const;
	};

	const ServerAdapter& AdapterOf(AdapterKind kind);
}
