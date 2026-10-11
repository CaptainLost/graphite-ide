#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Api/LanguageService.h"
#include "Language/Install/InstallSummary.h"
#include "Language/Server/ServerChoice.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	enum class LanguageBarChoice : std::uint8_t
	{
		None,
		Configure,
		Install,
		Cancel,
		GetRuntime,
		OpenSettings,
		NotNow
	};

	struct LanguagePrompt
	{
		LanguageId Language = LanguageId::PlainText;
		std::string ServerId;
		int RuntimeMajor = 0;
		std::string Message;
		std::vector<LanguageBarChoice> Buttons;
	};

	struct LanguagePromptInputs
	{
		LanguageId Language = LanguageId::PlainText;
		ServerChoice Choice;
		ServerState State = ServerState::NotConfigured;
		InstallInputs Install;
		bool Dismissed = false;
		bool RestrictedModeShown = false;
	};

	// Nothing while the restricted-mode bar shows: no server would start there anyway, and one bar at a time is read.
	std::optional<LanguagePrompt> LanguagePromptFor(const LanguagePromptInputs& inputs);

	const char* LanguageBarChoiceLabel(LanguageBarChoice choice);

	// Returns what was clicked this frame; the caller acts on it.
	LanguageBarChoice DrawLanguageSupportBar(const LanguagePrompt& prompt);
}
