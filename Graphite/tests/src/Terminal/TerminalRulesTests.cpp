#include <doctest/doctest.h>

#include "Terminal/TerminalRules.h"

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
		OpenTerminal{.Id = "{B}", .ProfileName = "claude", .WorkingDirectory = kRoot, .FromCommand = true},
		OpenTerminal{.Id = "{C}", .ProfileName = "Command Prompt", .WorkingDirectory = kRoot / "src", .CarriedOver = true},
		OpenTerminal{"{D}", "Command Prompt", kRoot / "docs"}};

	CHECK(CaptureTerminals(open) == std::vector<TerminalTabState>{
										TerminalTabState{"{A}", "PowerShell 7", kRoot},
										TerminalTabState{"{D}", "Command Prompt", kRoot / "docs"}});
}

TEST_CASE("A shell opened with no folder is adopted by the folder opened next, and not by the one after")
{
	OpenTerminal shell{"{A}", "PowerShell 7", kRoot};
	shell.Unowned = true;
	std::vector<OpenTerminal> open = {shell};

	CarryOverTerminals(open);
	CHECK(AdoptTerminals(open, false, true));
	CHECK(CaptureTerminals(open) == std::vector<TerminalTabState>{TerminalTabState{"{A}", "PowerShell 7", kRoot}});
	CHECK(CaptureFolderTerminals(open, KeepsTerminals(std::nullopt, true), std::nullopt).has_value());

	CarryOverTerminals(open);

	CHECK_FALSE(AdoptTerminals(open, false, true));
	CHECK(CaptureTerminals(open).empty());
	CHECK_FALSE(CaptureFolderTerminals(open, KeepsTerminals(std::nullopt, false), std::nullopt).has_value());
}

TEST_CASE("Carrying over on its own leaves a shell nobody owned alone")
{
	OpenTerminal shell{"{A}", "PowerShell 7", kRoot};
	shell.Unowned = true;
	std::vector<OpenTerminal> open = {shell};

	CarryOverTerminals(open);

	CHECK(open[0].Unowned);
	CHECK_FALSE(open[0].CarriedOver);
	CHECK(AdoptTerminals(open, false, true));
}

TEST_CASE("A terminal carried over from one folder to another is never adopted")
{
	OpenTerminal shell{"{A}", "PowerShell 7", kRoot};
	std::vector<OpenTerminal> open = {shell};

	CarryOverTerminals(open);

	CHECK_FALSE(AdoptTerminals(open, false, true));
	CHECK(open[0].CarriedOver);
	CHECK(CaptureTerminals(open).empty());
}

TEST_CASE("A command's window is never adopted")
{
	OpenTerminal command{.Id = "{B}", .ProfileName = "claude", .WorkingDirectory = kRoot, .FromCommand = true};
	command.Unowned = true;
	std::vector<OpenTerminal> open = {command};

	CarryOverTerminals(open);

	CHECK_FALSE(AdoptTerminals(open, false, true));
	CHECK(CaptureTerminals(open).empty());
}

TEST_CASE("The startup shell is adopted only by a folder with no terminals saved, a shell the reader opened always")
{
	OpenTerminal startup{"{S}", "PowerShell 7", kRoot};
	startup.Unowned = true;
	startup.AutoOpened = true;
	OpenTerminal chosen{"{U}", "Command Prompt", kRoot};
	chosen.Unowned = true;

	std::vector<OpenTerminal> fresh = {startup, chosen};
	CarryOverTerminals(fresh);
	CHECK(AdoptTerminals(fresh, false, true));
	CHECK(CaptureTerminals(fresh).size() == 2);

	std::vector<OpenTerminal> withSaved = {startup, chosen};
	CarryOverTerminals(withSaved);
	CHECK(AdoptTerminals(withSaved, true, true));
	REQUIRE(CaptureTerminals(withSaved).size() == 1);
	CHECK(CaptureTerminals(withSaved)[0].Id == "{U}");

	std::vector<OpenTerminal> only = {startup};
	CarryOverTerminals(only);
	CHECK_FALSE(AdoptTerminals(only, true, true));
	CHECK(CaptureTerminals(only).empty());
}

TEST_CASE("A folder in restricted mode adopts neither a shell opened with no folder nor the startup shell")
{
	OpenTerminal startup{"{S}", "PowerShell 7", kRoot};
	startup.Unowned = true;
	startup.AutoOpened = true;
	OpenTerminal chosen{"{U}", "Command Prompt", kRoot};
	chosen.Unowned = true;

	std::vector<OpenTerminal> fresh = {startup, chosen};
	CarryOverTerminals(fresh);
	CHECK_FALSE(AdoptTerminals(fresh, false, false));
	CHECK(CaptureTerminals(fresh).empty());
	CHECK(CaptureFolderTerminals(fresh, KeepsTerminals(std::nullopt, false), HeldBackTerminals(std::nullopt, false)) ==
		  std::nullopt);

	const std::vector<TerminalTabState> saved = {TerminalTabState{"{T}", "PowerShell 7", kRoot / "tools"}};
	std::vector<OpenTerminal> withSaved = {startup, chosen};
	CarryOverTerminals(withSaved);
	CHECK_FALSE(AdoptTerminals(withSaved, true, false));
	CHECK(CaptureFolderTerminals(withSaved, KeepsTerminals(saved, false), HeldBackTerminals(saved, false)) == saved);
}

TEST_CASE("Shells a restricted folder did not adopt stay nobody's, and the next trusted folder adopts them")
{
	OpenTerminal chosen{"{U}", "Command Prompt", kRoot};
	chosen.Unowned = true;
	std::vector<OpenTerminal> open = {chosen};

	CarryOverTerminals(open);
	CHECK_FALSE(AdoptTerminals(open, false, false));
	CHECK(open[0].Unowned);
	CHECK(open[0].CarriedOver);

	open.push_back(OpenTerminal{"{A}", "PowerShell 7", kRoot});
	CHECK(CaptureFolderTerminals(open, true, std::nullopt) ==
		  std::vector<TerminalTabState>{TerminalTabState{"{A}", "PowerShell 7", kRoot}});

	CarryOverTerminals(open);
	CHECK(AdoptTerminals(open, false, true));
	CHECK(CaptureTerminals(open) == std::vector<TerminalTabState>{TerminalTabState{"{U}", "Command Prompt", kRoot}});
}

TEST_CASE("Capture keeps the saved order and the last title, and restore hands the title back")
{
	OpenTerminal first{"{A}", "Command Prompt", kRoot};
	first.Title = "build";
	OpenTerminal second{"{B}", "PowerShell 7", kRoot};

	const std::vector<TerminalTabState> captured = CaptureTerminals({first, second});

	REQUIRE(captured.size() == 2);
	CHECK(captured[0].Id == "{A}");
	CHECK(captured[0].Title == "build");
	CHECK(captured[1].Id == "{B}");

	const TerminalRestorePlan plan = PlanTerminalRestore(captured, Shells(), {}, Resolve, true);

	REQUIRE(plan.Start.size() == 2);
	CHECK(plan.Start[0].Id == "{A}");
	CHECK(plan.Start[0].Title == "build");
	CHECK(plan.Start[1].Id == "{B}");
}

TEST_CASE("A tab shows the title the shell announces now, else the one it announced last time")
{
	CHECK(TerminalLabelTitle("", "vim main.cpp") == "vim main.cpp");
	CHECK(TerminalLabelTitle("make", "vim main.cpp") == "make");
	CHECK(TerminalLabelTitle("", "").empty());
}

TEST_CASE("A grid measured as a sliver is not worth resizing the shell to")
{
	CHECK_FALSE(TerminalSizeIsPlausible({0, 0}, {0, 0}));
	CHECK_FALSE(TerminalSizeIsPlausible({1, 25}, {1, 25}));
	CHECK_FALSE(TerminalSizeIsPlausible({80, 1}, {80, 1}));
	CHECK(TerminalSizeIsPlausible({20, 2}, {0, 0}));
	CHECK(TerminalSizeIsPlausible({120, 30}, {0, 0}));
}

TEST_CASE("A narrow grid resizes the shell once it measured the same on the frame before")
{
	CHECK_FALSE(TerminalSizeIsPlausible({19, 25}, {0, 0}));
	CHECK_FALSE(TerminalSizeIsPlausible({12, 25}, {19, 25}));

	CHECK(TerminalSizeIsPlausible({12, 25}, {12, 25}));
	CHECK(TerminalSizeIsPlausible({2, 2}, {2, 2}));
}

TEST_CASE("Restore starts each saved terminal with its own id, in saved order, by profile name")
{
	const std::vector<TerminalTabState> saved = {TerminalTabState{"{A}", "Command Prompt", kRoot / "src"},
												 TerminalTabState{"{B}", "PowerShell 7", kRoot}};

	const TerminalRestorePlan plan = PlanTerminalRestore(saved, Shells(), {}, Resolve, true);

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

	const TerminalRestorePlan plan = PlanTerminalRestore(saved, Shells(), {}, Resolve, true);

	REQUIRE(plan.Start.size() == 1);
	CHECK(plan.Start[0].Id == "{B}");
	CHECK(plan.UnknownProfiles == std::vector<std::string>{"Nushell"});
}

TEST_CASE("A saved directory that is gone starts the terminal in the folder instead")
{
	const std::vector<TerminalTabState> saved = {TerminalTabState{"{A}", "PowerShell 7", kRoot / "gone"}};

	const TerminalRestorePlan plan = PlanTerminalRestore(saved, Shells(), {}, Resolve, true);

	REQUIRE(plan.Start.size() == 1);
	CHECK(plan.Start[0].WorkingDirectory == kRoot);
}

TEST_CASE("A saved terminal that is already open is not started twice")
{
	const std::vector<TerminalTabState> saved = {TerminalTabState{"{A}", "PowerShell 7", kRoot},
												 TerminalTabState{"{B}", "PowerShell 7", kRoot}};
	const std::vector<OpenTerminal> open = {OpenTerminal{.Id = "{A}", .ProfileName = "PowerShell 7", .WorkingDirectory = kRoot, .CarriedOver = true}};

	const TerminalRestorePlan plan = PlanTerminalRestore(saved, Shells(), open, Resolve, true);

	REQUIRE(plan.Start.size() == 1);
	CHECK(plan.Start[0].Id == "{B}");
	CHECK(plan.AlreadyOpen == std::vector<std::string>{"{A}"});
}

TEST_CASE("A saved list naming one id twice starts it once")
{
	const std::vector<TerminalTabState> saved = {TerminalTabState{"{A}", "PowerShell 7", kRoot},
												 TerminalTabState{"{A}", "Command Prompt", kRoot}};

	CHECK(PlanTerminalRestore(saved, Shells(), {}, Resolve, true).Start.size() == 1);
}

TEST_CASE("Only a folder that never kept terminals, with none open, opens a default one")
{
	CHECK(OpensDefaultTerminal(std::nullopt, 0, true));
	CHECK_FALSE(OpensDefaultTerminal(std::nullopt, 2, true));
	CHECK_FALSE(OpensDefaultTerminal(std::vector<TerminalTabState>{}, 0, true));
}

TEST_CASE("A folder in restricted mode starts none of its saved terminals")
{
	const std::vector<TerminalTabState> saved = {TerminalTabState{"{A}", "PowerShell 7", "C:\\Windows"},
												 TerminalTabState{"{B}", "Command Prompt", kRoot}};

	const TerminalRestorePlan plan = PlanTerminalRestore(saved, Shells(), {}, Resolve, false);

	CHECK(plan.Start.empty());
	CHECK(plan.AlreadyOpen.empty());
	CHECK(plan.UnknownProfiles.empty());
}

TEST_CASE("A folder in restricted mode opens no default terminal")
{
	CHECK_FALSE(OpensDefaultTerminal(std::nullopt, 0, false));
	CHECK_FALSE(OpensDefaultTerminal(std::vector<TerminalTabState>{}, 0, false));
}

TEST_CASE("A folder whose only terminals were carried over stays absent")
{
	const bool keeps = KeepsTerminals(std::nullopt, OpensDefaultTerminal(std::nullopt, 1, true));
	const std::vector<OpenTerminal> open = {OpenTerminal{.Id = "{A}", .ProfileName = "PowerShell 7", .WorkingDirectory = kRoot, .CarriedOver = true}};

	CHECK_FALSE(keeps);
	CHECK(CaptureFolderTerminals(open, keeps, std::nullopt) == std::nullopt);
}

TEST_CASE("A folder whose only terminals were started by a command stays absent")
{
	const std::vector<OpenTerminal> open = {OpenTerminal{.Id = "{A}", .ProfileName = "claude", .WorkingDirectory = kRoot, .FromCommand = true}};

	CHECK(CaptureFolderTerminals(open, KeepsTerminals(std::nullopt, false), std::nullopt) == std::nullopt);
}

TEST_CASE("A folder whose own terminals were all closed is kept empty")
{
	const bool keeps = KeepsTerminals(std::nullopt, OpensDefaultTerminal(std::nullopt, 0, true));

	CHECK(keeps);
	CHECK(CaptureFolderTerminals({}, keeps, std::nullopt) == std::vector<TerminalTabState>{});
}

TEST_CASE("A folder with a restored list keeps it")
{
	const std::vector<TerminalTabState> saved = {TerminalTabState{"{A}", "PowerShell 7", kRoot}};
	const std::vector<OpenTerminal> open = {OpenTerminal{"{A}", "PowerShell 7", kRoot}};

	CHECK(KeepsTerminals(saved, false));
	CHECK(KeepsTerminals(std::vector<TerminalTabState>{}, false));
	CHECK(CaptureFolderTerminals(open, KeepsTerminals(saved, false), std::nullopt) == saved);
}

TEST_CASE("A folder in restricted mode records its saved list unchanged although it started none of it")
{
	const std::vector<TerminalTabState> saved = {TerminalTabState{"{A}", "PowerShell 7", kRoot},
												 TerminalTabState{"{B}", "Command Prompt", kRoot / "src"}};
	const std::optional<std::vector<TerminalTabState>> heldBack = HeldBackTerminals(saved, false);

	CHECK(heldBack == saved);
	CHECK(CaptureFolderTerminals({}, KeepsTerminals(saved, false), heldBack) == saved);

	const std::vector<OpenTerminal> carried = {OpenTerminal{.Id = "{C}", .ProfileName = "PowerShell 7", .WorkingDirectory = kRoot, .CarriedOver = true}};
	CHECK(CaptureFolderTerminals(carried, KeepsTerminals(saved, false), heldBack) == saved);
}

TEST_CASE("A folder in restricted mode keeps an empty saved list empty and an absent one absent")
{
	const std::vector<TerminalTabState> empty;

	CHECK(CaptureFolderTerminals({}, KeepsTerminals(empty, false), HeldBackTerminals(empty, false)) == empty);
	CHECK(HeldBackTerminals(std::nullopt, false) == std::nullopt);
	CHECK(CaptureFolderTerminals({}, KeepsTerminals(std::nullopt, false), HeldBackTerminals(std::nullopt, false)) ==
		  std::nullopt);
}

TEST_CASE("A trusted folder holds nothing back")
{
	const std::vector<TerminalTabState> saved = {TerminalTabState{"{A}", "PowerShell 7", kRoot}};

	CHECK(HeldBackTerminals(saved, true) == std::nullopt);
	CHECK(HeldBackTerminals(std::nullopt, true) == std::nullopt);
}

TEST_CASE("A terminal started by hand in restricted mode records what is open instead")
{
	const std::vector<OpenTerminal> open = {OpenTerminal{"{N}", "Command Prompt", kRoot}};

	CHECK(CaptureFolderTerminals(open, true, std::nullopt) ==
		  std::vector<TerminalTabState>{TerminalTabState{"{N}", "Command Prompt", kRoot}});
	CHECK(CaptureFolderTerminals({}, true, std::nullopt) == std::vector<TerminalTabState>{});
}

TEST_CASE("A terminal opened on a folder starts in it, and on a file in the file's folder")
{
	CHECK(TerminalDirectoryFor(kRoot / "src", true) == kRoot / "src");
	CHECK(TerminalDirectoryFor(kRoot / "src" / "main.cpp", false) == kRoot / "src");
	CHECK(TerminalDirectoryFor(kRoot, true) == kRoot);
}

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

	CHECK(TerminalKeyModifiers(KeyModifier::Alt, true) == KeyModifier::None);
	CHECK(TerminalKeyModifiers(KeyModifier::Control | KeyModifier::Alt, true) == KeyModifier::None);
	CHECK(TerminalKeyModifiers(KeyModifier::Alt | KeyModifier::Shift, true) == KeyModifier::Shift);

	CHECK(TerminalKeyModifiers(KeyModifier::Alt, false) == KeyModifier::Alt);
	CHECK(TerminalKeyModifiers(KeyModifier::Control | KeyModifier::Alt, false) ==
		  (KeyModifier::Control | KeyModifier::Alt));
	CHECK(TerminalKeyModifiers(KeyModifier::Control | KeyModifier::Super, false) ==
		  (KeyModifier::Control | KeyModifier::Super));
}

TEST_CASE("Ctrl+C copies a selection and interrupts without one")
{
	using Core::KeyCode;
	using Core::KeyModifier;

	CHECK(ClassifyTerminalShortcut(KeyCode::C, KeyModifier::Control, true) == TerminalShortcut::Copy);
	CHECK(ClassifyTerminalShortcut(KeyCode::C, KeyModifier::Control, false) == TerminalShortcut::Interrupt);
}

TEST_CASE("Ctrl+Shift+C only ever copies and never becomes an interrupt")
{
	using Core::KeyCode;
	using Core::KeyModifier;

	const KeyModifier chord = KeyModifier::Control | KeyModifier::Shift;

	CHECK(ClassifyTerminalShortcut(KeyCode::C, chord, true) == TerminalShortcut::Copy);
	CHECK(ClassifyTerminalShortcut(KeyCode::C, chord, false) == TerminalShortcut::Swallow);
}

TEST_CASE("Ctrl+V and Ctrl+Shift+V both paste")
{
	using Core::KeyCode;
	using Core::KeyModifier;

	CHECK(ClassifyTerminalShortcut(KeyCode::V, KeyModifier::Control, false) == TerminalShortcut::Paste);
	CHECK(ClassifyTerminalShortcut(KeyCode::V, KeyModifier::Control | KeyModifier::Shift, true) ==
		  TerminalShortcut::Paste);
}

TEST_CASE("Other chords are not the view's gestures")
{
	using Core::KeyCode;
	using Core::KeyModifier;

	CHECK(ClassifyTerminalShortcut(KeyCode::V, KeyModifier::None, false) == TerminalShortcut::None);
	CHECK(ClassifyTerminalShortcut(KeyCode::C, KeyModifier::Shift, true) == TerminalShortcut::None);
	CHECK(ClassifyTerminalShortcut(KeyCode::A, KeyModifier::Control, false) == TerminalShortcut::None);

	CHECK(ClassifyTerminalShortcut(KeyCode::C, KeyModifier::Control | KeyModifier::Alt, true) ==
		  TerminalShortcut::None);
	CHECK(ClassifyTerminalShortcut(KeyCode::V, KeyModifier::Control | KeyModifier::Super, false) ==
		  TerminalShortcut::None);
}

TEST_CASE("The scrollbar thumb sits at the bottom at the tail and at the top at the oldest line")
{
	const TerminalScrollbarThumb tail = ComputeTerminalScrollbarThumb(0, 10, 90, 100.0f, 8.0f);
	CHECK(tail.Height == doctest::Approx(10.0f));
	CHECK(tail.Top == doctest::Approx(90.0f));

	const TerminalScrollbarThumb oldest = ComputeTerminalScrollbarThumb(90, 10, 90, 100.0f, 8.0f);
	CHECK(oldest.Top == doctest::Approx(0.0f));

	const TerminalScrollbarThumb middle = ComputeTerminalScrollbarThumb(45, 10, 90, 100.0f, 8.0f);
	CHECK(middle.Top == doctest::Approx(45.0f));
}

TEST_CASE("The scrollbar thumb never shrinks below its minimum and fills the track without history")
{
	const TerminalScrollbarThumb small = ComputeTerminalScrollbarThumb(0, 10, 9990, 100.0f, 8.0f);
	CHECK(small.Height == doctest::Approx(8.0f));
	CHECK(small.Top == doctest::Approx(92.0f));

	const TerminalScrollbarThumb none = ComputeTerminalScrollbarThumb(0, 10, 0, 100.0f, 8.0f);
	CHECK(none.Top == doctest::Approx(0.0f));
	CHECK(none.Height == doctest::Approx(100.0f));

	CHECK(ComputeTerminalScrollbarThumb(0, 10, 50, 5.0f, 8.0f).Height == doctest::Approx(5.0f));
	CHECK(ComputeTerminalScrollbarThumb(3, 0, 50, 0.0f, 8.0f).Height == doctest::Approx(0.0f));

	CHECK(ComputeTerminalScrollbarThumb(500, 10, 90, 100.0f, 8.0f).Top == doctest::Approx(0.0f));
}

TEST_CASE("A thumb dragged to a position maps back to the offset it stands for")
{
	CHECK(TerminalScrollOffsetForThumbTop(90.0f, 90, 100.0f, 10.0f) == 0);
	CHECK(TerminalScrollOffsetForThumbTop(0.0f, 90, 100.0f, 10.0f) == 90);
	CHECK(TerminalScrollOffsetForThumbTop(45.0f, 90, 100.0f, 10.0f) == 45);

	CHECK(TerminalScrollOffsetForThumbTop(-20.0f, 90, 100.0f, 10.0f) == 90);
	CHECK(TerminalScrollOffsetForThumbTop(500.0f, 90, 100.0f, 10.0f) == 0);

	CHECK(TerminalScrollOffsetForThumbTop(10.0f, 0, 100.0f, 100.0f) == 0);
	CHECK(TerminalScrollOffsetForThumbTop(10.0f, 90, 100.0f, 100.0f) == 0);
}

TEST_CASE("Dragging a thumb round-trips through the offset")
{
	for (int offset = 0; offset <= 90; ++offset)
	{
		const TerminalScrollbarThumb thumb = ComputeTerminalScrollbarThumb(offset, 10, 90, 100.0f, 8.0f);
		CHECK(TerminalScrollOffsetForThumbTop(thumb.Top, 90, 100.0f, thumb.Height) == offset);
	}
}

TEST_CASE("The default shell is the one chosen by name, else the first")
{
	CHECK(DefaultShellIndex(Shells(), "Command Prompt") == 1);
	CHECK(DefaultShellIndex(Shells(), "PowerShell 7") == 0);
	CHECK(DefaultShellIndex(Shells(), "") == 0);
	CHECK(DefaultShellIndex(Shells(), "Git Bash") == 0);
}

TEST_CASE("With no terminal the strip starts the default shell whatever the flag says")
{
	CHECK(ChooseTerminalStripAction(0, false, false) == TerminalStripAction::StartDefault);
	CHECK(ChooseTerminalStripAction(0, true, false) == TerminalStripAction::StartDefault);
}

TEST_CASE("Hidden terminals are shown")
{
	CHECK(ChooseTerminalStripAction(2, true, false) == TerminalStripAction::Show);
}

TEST_CASE("Shown terminals with one drawn are hidden")
{
	CHECK(ChooseTerminalStripAction(2, false, true) == TerminalStripAction::Hide);
}

TEST_CASE("Shown terminals all behind other tabs are brought forward")
{
	CHECK(ChooseTerminalStripAction(2, false, false) == TerminalStripAction::BringForward);
}

TEST_CASE("The strip reads open only for shown terminals that exist")
{
	const TerminalStripLook none = TerminalStripLookOf(0, false);
	CHECK_FALSE(none.Open);
	CHECK_FALSE(none.RunningHidden);

	const TerminalStripLook noneHidden = TerminalStripLookOf(0, true);
	CHECK_FALSE(noneHidden.Open);
	CHECK_FALSE(noneHidden.RunningHidden);

	const TerminalStripLook shown = TerminalStripLookOf(2, false);
	CHECK(shown.Open);
	CHECK_FALSE(shown.RunningHidden);

	const TerminalStripLook hidden = TerminalStripLookOf(2, true);
	CHECK_FALSE(hidden.Open);
	CHECK(hidden.RunningHidden);
}

TEST_CASE("The terminal focused last is the one brought forward")
{
	CHECK(TerminalToBringForward({"a", "b", "c"}, "b") == std::optional<std::size_t>{1});
}

TEST_CASE("A terminal focused last and since closed falls back to the last one open")
{
	CHECK(TerminalToBringForward({"a", "c"}, "b") == std::optional<std::size_t>{1});
	CHECK(TerminalToBringForward({"a", "c"}, "") == std::optional<std::size_t>{1});
}

TEST_CASE("No terminal means nothing to bring forward")
{
	CHECK(TerminalToBringForward({}, "a") == std::nullopt);
}
