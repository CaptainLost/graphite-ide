#pragma once

#include "Git/Sidebar/SidebarRows.h"
#include "Git/Sidebar/SidebarText.h"
#include "Theme/ColorScheme.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	struct SidebarSelection
	{
		std::optional<std::size_t> Ref; // a base's local copy selects the base's row
		std::optional<std::size_t> Stash;
	};

	struct SidebarColours
	{
		std::array<ImU32, 7> Tones{}; // by SidebarTone
		ImU32 Accent = 0;             // the HEAD dot
		ImU32 Link = 0;               // a Link row
		ImU32 Right = 0;              // "2 to push", "3 to pull"
		ImU32 Rule = 0;               // the line after a section's title
		ImU32 Worktree = 0;           // the marker of a branch checked out in a worktree

		ImU32 Of(SidebarTone tone) const { return Tones[static_cast<std::size_t>(tone)]; }
	};

	SidebarColours ReadSidebarColours(const ResolvedScheme& scheme);

	// Mockup 38 px at a 13 px font.
	float SidebarTwoLineRowHeight();

	// Mockup 300 px at a 13 px font: the width the owner gives the column.
	float SidebarColumnWidth();

	// The search field, then the rows. Rows of two heights (a section, group, link or stash is one list row, a branch
	// or base two text lines) are drawn from a prefix sum of their heights, so a list of thousands costs the rows in view.
	class SidebarView
	{
	public:
		// With `waiting`, its one row comes first, in place of the sections that need the model, and `rows` are those that
		// work without it (the stashes); null `rows` are none. Only the rows in view are described (`textOf`) and drawn.
		void Draw(const std::vector<SidebarRow>* rows, const std::function<SidebarRowText(const SidebarRow&)>& textOf,
				  const std::optional<SidebarWaitingRow>& waiting, const SidebarSelection& selected,
				  const SidebarColours& colours);

		std::function<void(const SidebarRow&)> OnClick;     // Branch, Base, Stash, Group, Section, Link rows
		std::function<void(const SidebarRow&)> DrawRowMenu; // right-click on Branch, Base and Stash rows; fills an open popup. Runs inside the row loop: the owner must not rebuild rows from it

		const std::string& GetSearch() const { return m_Search; }

		// The one programmatic scroll: applied by the next Draw. A rebuild of the rows keeps the scroll position.
		void ScrollToTop() { m_ScrollToTop = true; }

	private:
		std::string m_Search;
		bool m_ScrollToTop = false;
		std::vector<float> m_RowTops; // reused every frame: row i starts at m_RowTops[i], the list ends at the last
	};
}
