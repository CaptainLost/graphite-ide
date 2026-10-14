#include "Terminal/TerminalRules.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace Graphite {

	namespace {
		constexpr int kMinimumCellsPerSide = 2;
		constexpr int kRoomyColumns = 20;
		constexpr std::size_t kFirstShellIndex = 0;
		constexpr float kNoThumbTravel = 0.0f;
		constexpr float kTopOfTrack = 0.0f;
		constexpr float kFractionStart = 0.0f;
		constexpr float kFractionEnd = 1.0f;
		constexpr int kNoScrollback = 0;
		constexpr int kMinimumVisibleRows = 1;

		bool IsTerminalOpen(const std::vector<OpenTerminal>& openTerminals, const std::string& terminalId)
		{
			for (const OpenTerminal& openTerminal : openTerminals)
			{
				if (openTerminal.Id == terminalId)
				{
					return true;
				}
			}

			return false;
		}

		bool IsTerminalPlanned(const TerminalRestorePlan& plan, const std::string& terminalId)
		{
			for (const TerminalToStart& planned : plan.Start)
			{
				if (planned.Id == terminalId)
				{
					return true;
				}
			}

			return false;
		}

		std::optional<std::size_t> FindShellIndex(const std::vector<Core::ShellProfile>& shells,
												  const std::string& profileName)
		{
			for (std::size_t shellIndex = 0; shellIndex < shells.size(); ++shellIndex)
			{
				if (shells[shellIndex].Name == profileName)
				{
					return shellIndex;
				}
			}

			return std::nullopt;
		}
	}

	std::string TerminalTitleFor(const std::string& title, const std::string& profileName)
	{
		if (title.empty())
		{
			return profileName;
		}

		const std::string leaf{Core::AfterLastPathSeparator(title)};

		if (leaf.empty())
		{
			return profileName;
		}

		return Core::Lowered(leaf).ends_with(".exe") ? profileName : leaf;
	}

	std::size_t DefaultShellIndex(const std::vector<Core::ShellProfile>& shells, std::string_view chosen)
	{
		for (std::size_t shellIndex = 0; shellIndex < shells.size(); ++shellIndex)
		{
			if (shells[shellIndex].Name == chosen)
			{
				return shellIndex;
			}
		}

		return kFirstShellIndex;
	}

	std::string NewTerminalCommandId(std::string_view profileName)
	{
		std::string slug;
		bool dashPending = false;

		for (const char character : profileName)
		{
			const unsigned char characterByte = static_cast<unsigned char>(character);

			if (std::isalnum(characterByte) != 0)
			{
				if (dashPending && !slug.empty())
				{
					slug.push_back('-');
				}

				dashPending = false;
				slug.push_back(Core::LowerAscii(static_cast<char>(characterByte)));
			}
			else
			{
				dashPending = true;
			}
		}

		return "terminal.new." + (slug.empty() ? std::string{"shell"} : slug);
	}

	std::vector<TerminalTabState> CaptureTerminals(const std::vector<OpenTerminal>& openTerminals)
	{
		std::vector<TerminalTabState> kept;

		for (const OpenTerminal& terminal : openTerminals)
		{
			if (terminal.FromCommand || terminal.CarriedOver)
			{
				continue;
			}

			kept.push_back(TerminalTabState{.Id = terminal.Id,
											.ProfileName = terminal.ProfileName,
											.WorkingDirectory = terminal.WorkingDirectory,
											.Title = terminal.Title});
		}

		return kept;
	}

	void CarryOverTerminals(std::vector<OpenTerminal>& openTerminals)
	{
		for (OpenTerminal& terminal : openTerminals)
		{
			if (!terminal.Unowned)
			{
				terminal.CarriedOver = true;
			}
		}
	}

	bool AdoptTerminals(std::vector<OpenTerminal>& openTerminals, bool folderHasSavedList, bool trusted)
	{
		bool anyAdopted = false;

		for (OpenTerminal& terminal : openTerminals)
		{
			if (!terminal.Unowned)
			{
				continue;
			}

			if (!trusted)
			{
				terminal.CarriedOver = true;
				continue;
			}

			const bool startupShellDeferredToSavedList = terminal.AutoOpened && folderHasSavedList;
			const bool adopted = !terminal.FromCommand && !startupShellDeferredToSavedList;

			terminal.CarriedOver = !adopted;
			terminal.Unowned = false;
			anyAdopted = anyAdopted || adopted;
		}

		return anyAdopted;
	}

	std::string TerminalLabelTitle(const std::string& live, const std::string& remembered)
	{
		return live.empty() ? remembered : live;
	}

	bool TerminalSizeIsPlausible(TerminalGridSize measured, TerminalGridSize previous)
	{
		if (measured.Columns < kMinimumCellsPerSide || measured.Rows < kMinimumCellsPerSide)
		{
			return false;
		}

		return measured.Columns >= kRoomyColumns || measured == previous;
	}

	TerminalRestorePlan PlanTerminalRestore(
		const std::vector<TerminalTabState>& saved, const std::vector<Core::ShellProfile>& shells,
		const std::vector<OpenTerminal>& openTerminals,
		const std::function<std::filesystem::path(const std::filesystem::path&)>& resolveDirectory,
		bool trusted)
	{
		TerminalRestorePlan plan;

		if (!trusted)
		{
			return plan;
		}

		for (const TerminalTabState& terminal : saved)
		{
			if (IsTerminalOpen(openTerminals, terminal.Id))
			{
				plan.AlreadyOpen.push_back(terminal.Id);
				continue;
			}

			// WARNING: Two windows under one identity would be one window to ImGui.
			if (IsTerminalPlanned(plan, terminal.Id))
			{
				continue;
			}

			const std::optional<std::size_t> shellIndex = FindShellIndex(shells, terminal.ProfileName);

			if (!shellIndex.has_value())
			{
				plan.UnknownProfiles.push_back(terminal.ProfileName);
				continue;
			}

			plan.Start.push_back(TerminalToStart{.Id = terminal.Id,
												 .Shell = *shellIndex,
												 .WorkingDirectory = resolveDirectory(terminal.WorkingDirectory),
												 .Title = terminal.Title});
		}

		return plan;
	}

	bool OpensDefaultTerminal(const std::optional<std::vector<TerminalTabState>>& saved,
							  std::size_t openCount, bool trusted)
	{
		return trusted && !saved.has_value() && openCount == 0;
	}

	bool KeepsTerminals(const std::optional<std::vector<TerminalTabState>>& saved, bool openedDefault)
	{
		return saved.has_value() || openedDefault;
	}

	std::optional<std::vector<TerminalTabState>> HeldBackTerminals(
		const std::optional<std::vector<TerminalTabState>>& saved, bool trusted)
	{
		return trusted ? std::nullopt : saved;
	}

	std::optional<std::vector<TerminalTabState>> CaptureFolderTerminals(
		const std::vector<OpenTerminal>& openTerminals, bool keepsTerminals,
		const std::optional<std::vector<TerminalTabState>>& heldBack)
	{
		if (heldBack)
		{
			return heldBack;
		}

		if (!keepsTerminals)
		{
			return std::nullopt;
		}

		return CaptureTerminals(openTerminals);
	}

	std::filesystem::path TerminalDirectoryFor(const std::filesystem::path& path, bool isDirectory)
	{
		return isDirectory ? path : path.parent_path();
	}

	bool TerminalTakesTypedText(Core::KeyModifier held)
	{
		return !Core::HasModifier(held, Core::KeyModifier::Control) ||
			   Core::HasModifier(held, Core::KeyModifier::Alt);
	}

	Core::KeyModifier TerminalKeyModifiers(Core::KeyModifier held, bool producedCharacter)
	{
		if (!producedCharacter)
		{
			return held;
		}

		return held & (Core::KeyModifier::Shift | Core::KeyModifier::Super);
	}

	TerminalScrollbarThumb ComputeTerminalScrollbarThumb(int offset, int rows, int scrollback,
														 float trackHeight, float minHeight)
	{
		const float track = std::max(kNoThumbTravel, trackHeight);

		if (scrollback <= kNoScrollback || track <= kNoThumbTravel)
		{
			return TerminalScrollbarThumb{.Top = kTopOfTrack, .Height = track};
		}

		const int visibleRows = std::max(kMinimumVisibleRows, rows);
		const float visibleShare =
			static_cast<float>(visibleRows) / static_cast<float>(visibleRows + scrollback);
		const float thumbHeight = std::min(track, std::max(minHeight, track * visibleShare));

		const int clampedOffset = std::clamp(offset, kNoScrollback, scrollback);
		const float fractionFromTop =
			static_cast<float>(scrollback - clampedOffset) / static_cast<float>(scrollback);

		return TerminalScrollbarThumb{.Top = (track - thumbHeight) * fractionFromTop,
									  .Height = thumbHeight};
	}

	int TerminalScrollOffsetForThumbTop(float thumbTop, int scrollback, float trackHeight,
										float thumbHeight)
	{
		const float travel = trackHeight - thumbHeight;

		if (scrollback <= kNoScrollback || travel <= kNoThumbTravel)
		{
			return kNoScrollback;
		}

		const float fractionFromTop = std::clamp(thumbTop / travel, kFractionStart, kFractionEnd);
		const int linesFromTop =
			static_cast<int>(std::lround(fractionFromTop * static_cast<float>(scrollback)));

		return std::clamp(scrollback - linesFromTop, kNoScrollback, scrollback);
	}

	TerminalShortcut ClassifyTerminalShortcut(Core::KeyCode key, Core::KeyModifier held,
											  bool hasSelection)
	{
		const bool shiftHeld = Core::HasModifier(held, Core::KeyModifier::Shift);

		if (!Core::HasModifier(held, Core::KeyModifier::Control) ||
			Core::HasModifier(held, Core::KeyModifier::Alt) ||
			Core::HasModifier(held, Core::KeyModifier::Super))
		{
			return TerminalShortcut::None;
		}

		if (key == Core::KeyCode::C)
		{
			if (hasSelection)
			{
				return TerminalShortcut::Copy;
			}

			return shiftHeld ? TerminalShortcut::Swallow : TerminalShortcut::Interrupt;
		}

		if (key == Core::KeyCode::V)
		{
			return TerminalShortcut::Paste;
		}

		return TerminalShortcut::None;
	}
}
