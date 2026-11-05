#pragma once

#include "Git/Branches/BranchAuthors.h"
#include "Git/Lines/BranchLines.h"
#include "Git/Lines/LineReader.h"

#include "Core/Base/Result.h"
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
		Link,
		Notice // one dim line in a section: its list is being read, its read failed, or it is empty
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

		std::string Label; // section title, group label, link text or notice text
		std::string Note;  // a remote section's "not on this computer"
		std::size_t Count = 0;
		std::optional<std::size_t> Total; // while searching: "n of total"
		bool Counted = true;              // a section whose list is unread shows "…" in the count's place
		bool CountFailed = false;         // its read failed: the count's place is empty and the section's notice says why
		bool Waiting = false;             // a notice of a list on its way: a loader turns beside it
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
		const std::vector<Core::StashEntry>* Stashes; // null while unread; a failed read keeps the list it last had
		const BranchAuthors* Authors;                 // null while unread
		std::string_view Search;
		const std::map<std::string, bool>& OpenByUser; // key -> open; absent keys use the defaults
		const std::set<std::string>& ShowingMerged;    // remote names
		const Core::Error* StashesError = nullptr;     // the last read of the stash list failed

		// The full name of the branch whose history is shown; empty for none. Its row stays listed when it is merged, and
		// the group holding it opens unless the user closed that group, so a branch the user looks at never goes from the
		// list.
		std::string_view ShownRef;
	};

	inline constexpr std::size_t kFlatGroupMost = 15;

	// The open-state key of the Yours section, the first of the sections.
	inline constexpr const char* kYoursSectionKey = "yours";

	std::vector<SidebarRow> BuildSidebarRows(const SidebarInput& input);

	// The rows that need no line model, for while the lines are read: the Stashes section alone, as BuildSidebarRows
	// lists it.
	// The ref a branch or base row opens, into LineRead::Refs: a base's local copy while the copy is checked out or has
	// commits of its own, so the history holds what it adds; else the row's own ref, so a base shows its newest commits.
	// Empty for another kind of row.
	std::optional<std::size_t> RefOpenedBy(const SidebarRow& row, const LineRead& read, const BranchLines& lines);

	// The ref whose menu a branch or base row opens: a base's local copy whenever it has one, so Checkout, Rename and Delete
	// act on the local branch. Empty for another kind of row.
	std::optional<std::size_t> RefOfRowMenu(const SidebarRow& row);

	std::vector<SidebarRow> BuildStashRows(const std::vector<Core::StashEntry>* stashes, const Core::Error* stashesError,
										   std::string_view search, const std::map<std::string, bool>& openByUser);
}
