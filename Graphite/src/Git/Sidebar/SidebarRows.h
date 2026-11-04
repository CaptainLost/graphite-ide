#pragma once

#include "Git/Branches/BranchAuthors.h"
#include "Git/Lines/LineReader.h"

#include "Core/Git/Stash.h"

#include <chrono>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	enum class SidebarRowKind
	{
		Section,
		Group,
		Branch,
		Base,
		Stash,
		Link
	};

	enum class SidebarSection
	{
		Yours,
		Bases,
		Remote,
		Stashes
	};

	enum class SidebarLink
	{
		WhyBases,
		ToYours,
		ShowMerged,
		HideMerged
	};

	struct SidebarRow
	{
		SidebarRowKind Kind = SidebarRowKind::Section;
		SidebarSection Section = SidebarSection::Yours;

		// The open-state key: "yours", "bases", "bases/quiet", "remote/<name>", "remote/<name>/<0|1|2>", "stashes".
		std::string Key;

		std::string Label; // section title, group label or link text
		std::string Note;  // a remote section's "not on this computer"
		std::size_t Count = 0;
		std::optional<std::size_t> Total; // while searching: "n of total"
		bool Open = false;
		std::size_t Depth = 0;
		std::optional<std::size_t> Ref;       // Branch, Base: index into LineRead::Refs
		std::optional<std::size_t> LocalCopy; // Base: the local branch that is one row with it
		std::optional<std::size_t> Stash;     // index into the stash list
		std::optional<SidebarLink> Link;
		std::string Remote; // Remote section, its groups and links
	};

	struct SidebarInput
	{
		const LineRead& Read;
		const std::vector<Core::StashEntry>* Stashes; // null while unread
		const BranchAuthors* Authors;                 // null while unread
		std::string_view Search;
		const std::map<std::string, bool>& OpenByUser; // key -> open; absent keys use the defaults
		const std::set<std::string>& ShowingMerged;    // remote names
		std::chrono::sys_seconds Now;
	};

	inline constexpr std::size_t kFlatGroupMost = 15;

	// The open-state key of the Yours section, the first of the sections.
	inline constexpr const char* kYoursSectionKey = "yours";

	std::vector<SidebarRow> BuildSidebarRows(const SidebarInput& input);

	// The rows that need no line model, for while the lines are read: the Stashes section alone, as BuildSidebarRows
	// lists it.
	std::vector<SidebarRow> BuildStashRows(const std::vector<Core::StashEntry>* stashes, std::string_view search,
										   const std::map<std::string, bool>& openByUser);
}
