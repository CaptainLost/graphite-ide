#pragma once

#include "Language/Server/Adapters/ServerAdapter.h"

#include <filesystem>

namespace Graphite {

	bool LacksCompileDatabase(const std::filesystem::path& root);

	class ClangdAdapter final : public ServerAdapter
	{
	public:
		nlohmann::json WorkspaceConfiguration(const AdapterContext& context, std::string_view section) const override;

		HoverInfo ParseHover(std::string_view markdown) const override;

		std::optional<ProjectProblem> ProjectProblemAt(const AdapterContext& context) const override;

		bool WarmUpNeedsOpen() const override;

		bool SupportsSwitchSourceHeader() const override;

		bool AnswersEmptyConfigurationWithOneEntry() const override;
	};
}
