#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {
	class CommandRegistry;
	class Keymap;
}

namespace Graphite {

	// One command as the palette lists it: the label a reader knows it by and the shortcut that also runs it.
	struct PaletteEntry
	{
		std::string Id;
		std::string Label;
		std::string Shortcut;
	};

	// The commands the palette can run now, by label: every command with a label and something to run that is enabled,
	// but `leaveOut` (the palette's own command). A label's tab and what follows it are dropped.
	std::vector<PaletteEntry> ListPaletteEntries(const Core::CommandRegistry& commands, const Core::Keymap& keys,
												 std::string_view leaveOut);

	// The indices of the entries whose label matches what was typed, best first, matched as Go to File matches a path.
	std::vector<std::size_t> RankPaletteEntries(std::string_view typed, const std::vector<PaletteEntry>& entries);

	// Ctrl+Shift+P: a field over the editors that finds a command by a few letters of its label and runs it. The
	// commands are listed as it opens, so what it offers is what is enabled at that moment.
	class CommandPalette
	{
	public:
		using RunCommand = std::function<void(const std::string& id)>;

		explicit CommandPalette(RunCommand run);

		void Open(std::vector<PaletteEntry> entries);

		bool IsOpen() const { return m_Open; }

		// Draws over the main viewport while open; closes on Escape, on a click outside, or once a command is run.
		void Render();

	private:
		void Rank();

		RunCommand m_Run;

		bool m_Open = false;
		bool m_JustOpened = false;
		std::string m_Typed;
		std::vector<PaletteEntry> m_Entries;
		std::vector<std::size_t> m_Matches;
		std::size_t m_Selected = 0;
		bool m_SelectionMoved = false;
		std::string m_RankedFor;
		bool m_Ranked = false;
	};
}
