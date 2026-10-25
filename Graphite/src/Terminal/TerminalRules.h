#pragma once

#include "Core/Base/KeyCodes.h"
#include "Core/Platform/ShellProfiles.h"
#include "Core/Terminal/TerminalScreen.h"

#include "Persistence/PersistedState.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	std::string TerminalTitleFor(const std::string& title, const std::string& profileName);

	std::string NewTerminalCommandId(std::string_view profileName);

	std::size_t DefaultShellIndex(const std::vector<Core::ShellProfile>& shells, std::string_view chosen);

	struct OpenTerminal
	{
		std::string Id;
		std::string ProfileName;
		std::filesystem::path WorkingDirectory;
		bool FromCommand = false;
		bool CarriedOver = false;
		std::string Title;
		bool Unowned = false;
		bool AutoOpened = false;
	};

	void CarryOverTerminals(std::vector<OpenTerminal>& openTerminals);

	bool AdoptTerminals(std::vector<OpenTerminal>& openTerminals, bool folderHasSavedList, bool trusted);

	std::vector<TerminalTabState> CaptureTerminals(const std::vector<OpenTerminal>& openTerminals);

	std::string TerminalLabelTitle(const std::string& live, const std::string& remembered);

	struct TerminalGridSize
	{
		int Columns = 0;
		int Rows = 0;

		bool operator==(const TerminalGridSize&) const = default;
	};

	bool TerminalSizeIsPlausible(TerminalGridSize measured, TerminalGridSize previous);

	struct TerminalToStart
	{
		std::string Id;
		std::size_t Shell = 0;
		std::filesystem::path WorkingDirectory;
		std::string Title;
	};

	struct TerminalRestorePlan
	{
		std::vector<TerminalToStart> Start;
		std::vector<std::string> AlreadyOpen;
		std::vector<std::string> UnknownProfiles;
	};

	TerminalRestorePlan PlanTerminalRestore(
		const std::vector<TerminalTabState>& saved, const std::vector<Core::ShellProfile>& shells,
		const std::vector<OpenTerminal>& openTerminals,
		const std::function<std::filesystem::path(const std::filesystem::path&)>& resolveDirectory,
		bool trusted);

	bool OpensDefaultTerminal(const std::optional<std::vector<TerminalTabState>>& saved,
							  std::size_t openCount, bool trusted);

	bool KeepsTerminals(const std::optional<std::vector<TerminalTabState>>& saved, bool openedDefault);

	std::optional<std::vector<TerminalTabState>> HeldBackTerminals(
		const std::optional<std::vector<TerminalTabState>>& saved, bool trusted);

	std::optional<std::vector<TerminalTabState>> CaptureFolderTerminals(
		const std::vector<OpenTerminal>& openTerminals, bool keepsTerminals,
		const std::optional<std::vector<TerminalTabState>>& heldBack);

	std::filesystem::path TerminalDirectoryFor(const std::filesystem::path& path, bool isDirectory);

	// WARNING: GLFW drops the fake Left Ctrl that AltGr sends on Windows, so AltGr arrives as Alt
	// alone; a real Alt+letter arrives as WM_SYSCHAR and never reaches the character queue.
	bool TerminalTakesTypedText(Core::KeyModifier held);

	// WARNING: A press that typed a character must not keep Control or Alt, or EncodeKey turns
	// `ą` into ESC a on top of itself.
	Core::KeyModifier TerminalKeyModifiers(Core::KeyModifier held, bool producedCharacter);

	struct TerminalScrollbarThumb
	{
		float Top = 0.0f;
		float Height = 0.0f;
	};

	TerminalScrollbarThumb ComputeTerminalScrollbarThumb(int offset, int rows, int scrollback,
														 float trackHeight, float minHeight);

	int TerminalScrollOffsetForThumbTop(float thumbTop, int scrollback, float trackHeight,
										float thumbHeight);

	enum class TerminalShortcut
	{
		None,
		Copy,
		Paste,
		Interrupt,
		Swallow
	};

	TerminalShortcut ClassifyTerminalShortcut(Core::KeyCode key, Core::KeyModifier held,
											  bool hasSelection);

	enum class TerminalStripAction
	{
		StartDefault,
		Hide,
		BringForward,
		Show
	};

	TerminalStripAction ChooseTerminalStripAction(std::size_t openCount, bool hidden, bool anyDrawn);

	struct TerminalStripLook
	{
		bool Open = false;
		bool RunningHidden = false;
	};

	TerminalStripLook TerminalStripLookOf(std::size_t openCount, bool hidden);

	std::optional<std::size_t> TerminalToBringForward(const std::vector<std::string>& ids,
													  const std::string& lastFocused);

	enum class TerminalKind
	{
		Shell,
		Command
	};

	bool TerminalTabCloses(TerminalKind kind, bool closeRequested, bool processAlive);

	std::string ExitCodeText(std::uint32_t exitCode);

	std::string ProcessExitedText(std::optional<std::uint32_t> exitCode);

	void WriteProcessExitedLine(Core::TerminalScreen& screen, std::optional<std::uint32_t> exitCode);

	// WARNING: For a screen whose program has ended and will never paint it again. Rows above scroll into the
	// scrollback so the last line written stays in view, and columns never narrow, so no line loses its end.
	void ResizeEndedScreen(Core::TerminalScreen& screen, std::uint16_t columns, std::uint16_t rows);

	std::string ExitedTerminalTitle(const std::string& title);

	bool TerminalScreenIsBlank(const Core::TerminalScreen& screen);

	std::string TerminalStartingText(const std::string& profileName);
}
