#include <doctest/doctest.h>

#include "Panels/TerminalRules.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using namespace Graphite;

namespace {
	const std::filesystem::path kRoot = std::filesystem::path{"D:\\Work\\Repo"};

	std::vector<Core::ShellProfile> Shells()
	{
		return {Core::ShellProfile{"PowerShell 7", "pwsh.exe", {}},
				Core::ShellProfile{"Command Prompt", "cmd.exe", {}}};
	}

	// Every saved directory exists unless it is named "gone".
	std::filesystem::path Resolve(const std::filesystem::path& saved)
	{
		return saved.empty() || saved.filename() == "gone" ? kRoot : saved;
	}
}

TEST_CASE("A shell announcing its own executable is titled by its profile")
{
	CHECK(TerminalTitleFor("C:\\Program Files\\PowerShell\\7\\pwsh.exe", "PowerShell 7") == "PowerShell 7");
	CHECK(TerminalTitleFor("", "cmd") == "cmd");
	CHECK(TerminalTitleFor("C:\\dir\\", "cmd") == "cmd");
	CHECK(TerminalTitleFor("vim main.cpp", "cmd") == "vim main.cpp");
	CHECK(TerminalTitleFor("D:\\Work\\build", "cmd") == "build");
}

TEST_CASE("A shell's command id is its name, lowercased, with runs of anything else as one dash")
{
	CHECK(NewTerminalCommandId("PowerShell 7") == "terminal.new.powershell-7");
	CHECK(NewTerminalCommandId("Git Bash (MINGW64)") == "terminal.new.git-bash-mingw64");
	CHECK(NewTerminalCommandId("  ") == "terminal.new.shell");
}

TEST_CASE("Capture leaves out terminals started by a command and those carried over from another folder")
{
	const std::vector<OpenTerminal> open = {
		OpenTerminal{"{A}", "PowerShell 7", kRoot},
		OpenTerminal{"{B}", "claude", kRoot, true, false},
		OpenTerminal{"{C}", "Command Prompt", kRoot / "src", false, true},
		OpenTerminal{"{D}", "Command Prompt", kRoot / "docs"}};

	CHECK(CaptureTerminals(open) == std::vector<TerminalTabState>{
										TerminalTabState{"{A}", "PowerShell 7", kRoot},
										TerminalTabState{"{D}", "Command Prompt", kRoot / "docs"}});
}

TEST_CASE("Restore starts each saved terminal with its own id, in saved order, by profile name")
{
	const std::vector<TerminalTabState> saved = {TerminalTabState{"{A}", "Command Prompt", kRoot / "src"},
												 TerminalTabState{"{B}", "PowerShell 7", kRoot}};

	const TerminalRestorePlan plan = PlanTerminalRestore(saved, Shells(), {}, Resolve);

	REQUIRE(plan.Start.size() == 2);
	CHECK(plan.Start[0].Id == "{A}");
	CHECK(plan.Start[0].Shell == 1);
	CHECK(plan.Start[0].WorkingDirectory == kRoot / "src");
	CHECK(plan.Start[1].Id == "{B}");
	CHECK(plan.Start[1].Shell == 0);
	CHECK(plan.UnknownProfiles.empty());
}

TEST_CASE("A saved terminal whose shell is no longer installed is skipped and named, the rest restored")
{
	const std::vector<TerminalTabState> saved = {TerminalTabState{"{A}", "Nushell", kRoot},
												 TerminalTabState{"{B}", "PowerShell 7", kRoot}};

	const TerminalRestorePlan plan = PlanTerminalRestore(saved, Shells(), {}, Resolve);

	REQUIRE(plan.Start.size() == 1);
	CHECK(plan.Start[0].Id == "{B}");
	CHECK(plan.UnknownProfiles == std::vector<std::string>{"Nushell"});
}

TEST_CASE("A saved directory that is gone starts the terminal in the folder instead")
{
	const std::vector<TerminalTabState> saved = {TerminalTabState{"{A}", "PowerShell 7", kRoot / "gone"}};

	const TerminalRestorePlan plan = PlanTerminalRestore(saved, Shells(), {}, Resolve);

	REQUIRE(plan.Start.size() == 1);
	CHECK(plan.Start[0].WorkingDirectory == kRoot);
}

TEST_CASE("A saved terminal that is already open is not started twice")
{
	// The same folder opened over itself: its terminals were carried over, and the saved list
	// names them again. The open one is the same window and stays; it belongs to this folder.
	const std::vector<TerminalTabState> saved = {TerminalTabState{"{A}", "PowerShell 7", kRoot},
												 TerminalTabState{"{B}", "PowerShell 7", kRoot}};
	const std::vector<OpenTerminal> open = {OpenTerminal{"{A}", "PowerShell 7", kRoot, false, true}};

	const TerminalRestorePlan plan = PlanTerminalRestore(saved, Shells(), open, Resolve);

	REQUIRE(plan.Start.size() == 1);
	CHECK(plan.Start[0].Id == "{B}");
	CHECK(plan.AlreadyOpen == std::vector<std::string>{"{A}"});
}

TEST_CASE("A saved list naming one id twice starts it once")
{
	const std::vector<TerminalTabState> saved = {TerminalTabState{"{A}", "PowerShell 7", kRoot},
												 TerminalTabState{"{A}", "Command Prompt", kRoot}};

	CHECK(PlanTerminalRestore(saved, Shells(), {}, Resolve).Start.size() == 1);
}

TEST_CASE("Only a folder that never kept terminals, with none open, opens a default one")
{
	CHECK(OpensDefaultTerminal(std::nullopt, 0));
	CHECK_FALSE(OpensDefaultTerminal(std::nullopt, 2));
	CHECK_FALSE(OpensDefaultTerminal(std::vector<TerminalTabState>{}, 0));
}

TEST_CASE("A folder whose only terminals were carried over stays absent")
{
	// Opened while another folder's shells were still up: no default shell opened, and the
	// reader started none here. Writing [] would read back as "all closed on purpose", and the
	// folder would open with no terminal from then on.
	const bool keeps = KeepsTerminals(std::nullopt, OpensDefaultTerminal(std::nullopt, 1));
	const std::vector<OpenTerminal> open = {OpenTerminal{"{A}", "PowerShell 7", kRoot, false, true}};

	CHECK_FALSE(keeps);
	CHECK(CaptureFolderTerminals(open, keeps) == std::nullopt);
}

TEST_CASE("A folder whose only terminals were started by a command stays absent")
{
	const std::vector<OpenTerminal> open = {OpenTerminal{"{A}", "claude", kRoot, true, false}};

	CHECK(CaptureFolderTerminals(open, KeepsTerminals(std::nullopt, false)) == std::nullopt);
}

TEST_CASE("A folder whose own terminals were all closed is kept empty")
{
	// It opened its default shell, so it has had a terminal of its own; with that closed the
	// list is empty rather than absent, and the next launch opens nothing.
	const bool keeps = KeepsTerminals(std::nullopt, OpensDefaultTerminal(std::nullopt, 0));

	CHECK(keeps);
	CHECK(CaptureFolderTerminals({}, keeps) == std::vector<TerminalTabState>{});
}

TEST_CASE("A folder with a restored list keeps it")
{
	const std::vector<TerminalTabState> saved = {TerminalTabState{"{A}", "PowerShell 7", kRoot}};
	const std::vector<OpenTerminal> open = {OpenTerminal{"{A}", "PowerShell 7", kRoot}};

	CHECK(KeepsTerminals(saved, false));
	CHECK(KeepsTerminals(std::vector<TerminalTabState>{}, false));
	CHECK(CaptureFolderTerminals(open, KeepsTerminals(saved, false)) == saved);
}

TEST_CASE("A terminal opened on a folder starts in it, and on a file in the file's folder")
{
	CHECK(TerminalDirectoryFor(kRoot / "src", true) == kRoot / "src");
	CHECK(TerminalDirectoryFor(kRoot / "src" / "main.cpp", false) == kRoot / "src");
	CHECK(TerminalDirectoryFor(kRoot, true) == kRoot);
}

// GLFW drops the fake Left Ctrl that AltGr sends on Windows, so ImGui sees an AltGr letter as
// Alt alone. A real Alt+letter arrives as WM_SYSCHAR, which never reaches the character queue,
// so a character produced under Alt is AltGr typing and nothing else.
TEST_CASE("A terminal takes typed text under Alt, AltGr and no modifier, but not under Ctrl alone")
{
	using Core::KeyModifier;

	CHECK(TerminalTakesTypedText(KeyModifier::None));
	CHECK(TerminalTakesTypedText(KeyModifier::Shift));
	CHECK(TerminalTakesTypedText(KeyModifier::Alt));
	CHECK(TerminalTakesTypedText(KeyModifier::Control | KeyModifier::Alt));
	CHECK_FALSE(TerminalTakesTypedText(KeyModifier::Control));
	CHECK_FALSE(TerminalTakesTypedText(KeyModifier::Control | KeyModifier::Shift));
}

TEST_CASE("A key that typed a character sends no Ctrl or Alt along with it")
{
	using Core::KeyModifier;

	// AltGr+A for ą, as ImGui reports it on Windows: Alt alone.
	CHECK(TerminalKeyModifiers(KeyModifier::Alt, true) == KeyModifier::None);
	CHECK(TerminalKeyModifiers(KeyModifier::Control | KeyModifier::Alt, true) == KeyModifier::None);
	CHECK(TerminalKeyModifiers(KeyModifier::Alt | KeyModifier::Shift, true) == KeyModifier::Shift);

	// Alt+B and Ctrl+Alt+Left type nothing and stay chords.
	CHECK(TerminalKeyModifiers(KeyModifier::Alt, false) == KeyModifier::Alt);
	CHECK(TerminalKeyModifiers(KeyModifier::Control | KeyModifier::Alt, false) ==
		  (KeyModifier::Control | KeyModifier::Alt));
	CHECK(TerminalKeyModifiers(KeyModifier::Control | KeyModifier::Super, false) ==
		  (KeyModifier::Control | KeyModifier::Super));
}
