#pragma once

#include "Language/Api/HoverInfo.h"
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
		std::vector<std::filesystem::path> Projects;
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

	// WARNING: ProjectProblemAt may read the disk: it runs on the UI thread, before the client starts. Every other hook may run on a client's reader thread with its protocol lock held, so none may block or touch the filesystem.
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

		virtual HoverInfo ParseHover(std::string_view markdown) const;

		virtual std::optional<std::string> LoadsProjectsFrom(const AdapterContext& context) const;

		virtual std::optional<std::string_view> ProjectFileExtension() const;

		virtual std::optional<ProjectProblem> ProjectProblemAt(const AdapterContext& context) const;

		virtual bool WarmUpNeedsOpen() const;

		virtual bool SupportsSwitchSourceHeader() const;

		virtual bool AnswersEmptyConfigurationWithOneEntry() const;

		virtual bool FollowsSelectedSolution() const;
	};

	const ServerAdapter& AdapterOf(AdapterKind kind);
}
