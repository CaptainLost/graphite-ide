#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Api/LanguageService.h"
#include "Language/Server/ServerCatalog.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string_view>

namespace Graphite {

	struct AdapterContext
	{
		std::filesystem::path Root;
		nlohmann::json InitializationOptions;
		nlohmann::json Settings;
	};

	class ServerAdapter
	{
	public:
		virtual ~ServerAdapter() = default;

		virtual nlohmann::json WorkspaceConfiguration(const AdapterContext& context, std::string_view section) const;

		virtual std::optional<ProjectProblem> ProjectProblemAt(const std::filesystem::path& root) const;

		virtual bool WarmUpNeedsOpen() const;

		virtual bool SupportsSwitchSourceHeader() const;
	};

	const ServerAdapter& AdapterOf(AdapterKind kind);

	bool LanguageOffersSwitchSourceHeader(LanguageId language);
}
