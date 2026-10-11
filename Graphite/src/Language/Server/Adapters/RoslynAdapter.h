#pragma once

#include "Language/Server/Adapters/ServerAdapter.h"

namespace Graphite {

	class RoslynAdapter final : public ServerAdapter
	{
	public:
		void AfterInitialized(const AdapterContext& context, std::vector<OutgoingNotification>& notifications) const override;

		std::vector<std::filesystem::path> ProjectsAt(const AdapterContext& context) const override;

		std::optional<ProjectProblem> ProjectProblemAt(const AdapterContext& context) const override;

		NotificationEffect HandleNotification(const AdapterContext& context, std::string_view method,
											  const nlohmann::json& params) const override;

		std::optional<std::string> LoadsProjectsFrom(const AdapterContext& context) const override;

		bool FollowsSelectedSolution() const override;
	};
}
