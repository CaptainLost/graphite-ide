#pragma once

#include "Language/Install/InstallSummary.h"
#include "Language/Server/DotnetRuntime.h"
#include "Language/Server/ServerCatalog.h"
#include "Language/Server/ServerChoice.h"
#include "Language/Server/ServerCommand.h"

#include <span>
#include <string>

namespace Graphite {

	std::string ServerOptionLabel(const ServerDefinition& server, bool recommended);

	// Empty for a server that needs no runtime.
	std::string RuntimeLine(const ServerRuntime& runtime, std::span<const DotnetInstallation> installations);

	bool RuntimeFound(const ServerRuntime& runtime, std::span<const DotnetInstallation> installations);

	std::string ChoicePreview(LanguageId language, const ServerChoice& choice);

	const char* InstallActionLabel(InstallAction action);

		// Empty when the command runs from Graphite's install.
		std::string RunningElsewhereNote(const ServerCommand& command, bool customPathSet);
}
