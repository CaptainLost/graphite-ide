#pragma once

#include "Settings/Panel/SettingsDirectory.h"

#include <filesystem>
#include <functional>

namespace Core {

	class CommandRegistry;
	class EventBus;
	class Keymap;
}

namespace Graphite {

	class ActiveScheme;
	class AnalysisSettings;
	class AppSettings;
	class ClaudeSettings;
	class EditorSettings;
	class LanguageServices;
	class ModalGate;
	class SearchSettings;
	class ServerInstalls;
	class TerminalWindows;
	class WorkspaceTrust;

	struct SettingsSources
	{
		AppSettings& Store;
		EditorSettings& Editor;
		SearchSettings& Search;
		AnalysisSettings& Analysis;
		ClaudeSettings& Claude;
		ActiveScheme& Scheme;
		Core::CommandRegistry& Commands;
		Core::Keymap& Keys;
		ModalGate& Modals;
		Core::EventBus& Events;
		WorkspaceTrust& Trust;
		TerminalWindows& Terminals;
		ServerInstalls& Installs;
		LanguageServices& Services;
		const bool& SolutionLoaded;
		std::filesystem::path ThemesDirectory;
		std::function<void()> OnKeyBindingsChanged;
	};

	SettingsDirectory BuildSettingsDirectory(const SettingsSources& sources);
}
