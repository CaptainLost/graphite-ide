#pragma once

#include "Language/Api/LanguageService.h"
#include "Language/Install/InstallSummary.h"
#include "Language/Server/DotnetRuntime.h"
#include "Language/Server/ServerCatalog.h"
#include "Language/Server/ServerChoice.h"
#include "Language/Server/ServerCommand.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace Graphite {

	std::string ServerOptionLabel(const ServerDefinition& server, bool recommended);

	const char* CustomChoiceLabel();

	// Empty for a server that needs no runtime.
	std::string RuntimeLine(const ServerRuntime& runtime, std::span<const DotnetInstallation> installations);

	bool RuntimeFound(const ServerRuntime& runtime, std::span<const DotnetInstallation> installations);

	std::string ChoicePreview(LanguageId language, const ServerChoice& choice);

	const char* InstallActionLabel(InstallAction action);

	bool IsFoundOnPath(const InstallInputs& inputs, const std::optional<ServerCommand>& found, const std::filesystem::path& customPath);

	const char* FoundOnPathTitle();

	std::string FoundOnPathNote(const ServerCommand& command);

	const char* FoundOnPathActionLabel(InstallAction action);

	struct PathRowText
	{
		std::string Description;
		std::string Problem;
	};

	PathRowText CustomPathRowText(const std::filesystem::path& storedPath, const ServerStatus& status);
}
