#pragma once

#include "Git/Branches/BranchAuthors.h"
#include "Git/Lines/BranchLines.h"
#include "Git/Lines/LineReader.h"

#include "Core/Base/Base.h"
#include "Core/Base/Result.h"
#include "Core/Git/Head.h"
#include "Core/Git/Stash.h"
#include "Core/Git/Tag.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
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
		Tag,
		Link,
		Notice // one dim line in a section: its list is being read, its read failed, or it is empty
	};

	enum class SidebarSection
	{
		Yours,
		Bases,
		Remote,
		Tags,
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

		// The open-state key: "yours", "bases", "bases/quiet", "remote/<name>", "remote/<name>/<0|1|2>", "tags", "tags/<folder>", "stashes".
		std::string Key;

		std::string Label; // section title, group label, link text or notice text; a tag row's name without its folder
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
		std::optional<std::size_t> Tag;       // index into the tag list
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

		// Whether the branch at this index of read.Model.Branches is known merged by content. Asked only while the rows are
		// built, never between builds, so a verdict that lands later changes no row until the next build. Null for none.
		const std::function<bool(std::size_t branch)>* MergedByContent = nullptr;

		// The tags, newest first; null while unread, and a failed read keeps the list it last had. A repository whose list
		// is empty has no Tags section.
		const std::vector<Core::Tag>* Tags = nullptr;
		const Core::Error* TagsError = nullptr; // the last read of the tag list failed
	};

	inline constexpr std::size_t kFlatGroupMost = 15;

	// The open-state key of the Yours section, the first of the sections.
	inline constexpr const char* kYoursSectionKey = "yours";

	std::vector<SidebarRow> BuildSidebarRows(const SidebarInput& input);

	// The authors a search matches against: those in hand when it began, for as long as it runs, so an authors walk that
	// lands mid-search adds no hits by itself; the newest ones otherwise.
	class SearchedAuthors
	{
	public:
		Core::Ref<const BranchAuthors> Follow(Core::Ref<const BranchAuthors> newest, std::string_view search);

	private:
		Core::Ref<const BranchAuthors> m_Held;
		bool m_Searching = false;
	};

	// What the Git window's sidebar rows are built from: they are built again whenever any of it changes.
	struct SidebarInputs
	{
		Core::Ref<const LineRead> Read;
		Core::Ref<const std::vector<Core::StashEntry>> Stashes;
		std::optional<std::string> StashesError; // a failed read keeps the list it last had, so it is compared apart
		Core::Ref<const std::vector<Core::Tag>> Tags;
		std::optional<std::string> TagsError; // compared apart for the same reason
		Core::Ref<const BranchAuthors> Authors;
		std::string Search;

		// Moved by every section or group opened or closed.
		std::uint64_t ChoicesVersion = 0;

		// Moved by every remote's merged branches shown or hidden.
		std::uint64_t MergedChoicesVersion = 0;

		// Moved every time the window is drawn after a frame it was not.
		std::uint64_t Openings = 0;

		std::string ShownRef;

		friend bool operator==(const SidebarInputs&, const SidebarInputs&) = default;
	};

	// The branches the sidebar hides as merged by content: those the content checks had found at the last of the moments
	// spec §6 rule 3 names, a new read, a change of the search, Show or Hide merged, and the window opened again. Rows
	// built for any other reason (a branch selected, a section or group opened or closed, the authors read landing) hide
	// the same branches, so no row goes from under the mouse; a verdict found since shows as the row's mark alone until the
	// next such moment.
	class MergedByContentSnapshot
	{
	public:
		// At such a moment since the last snapshot, asks `mergedNow` of every branch of the read again; otherwise keeps what
		// it has. `mergedNow` takes an index into inputs.Read->Model.Branches.
		void Follow(const SidebarInputs& inputs, const std::function<bool(std::size_t branch)>& mergedNow);

		// For an index into the Model.Branches of the read the snapshot was taken of; false for one it does not know.
		bool IsMerged(std::size_t branch) const;

	private:
		struct Moment
		{
			Core::Ref<const LineRead> Read;
			std::string Search;
			std::uint64_t MergedChoicesVersion = 0;
			std::uint64_t Openings = 0;

			friend bool operator==(const Moment&, const Moment&) = default;
		};

		std::optional<Moment> m_TakenAt;
		std::vector<bool> m_Merged;
	};

	// The ref a branch or base row opens, into LineRead::Refs: a base's local copy while the copy is checked out or has
	// commits of its own, so the history holds what it adds; else the row's own ref, so a base shows its newest commits.
	// Empty for another kind of row.
	std::optional<std::size_t> RefOpenedBy(const SidebarRow& row, const LineRead& read, const BranchLines& lines);

	// The ref whose menu a branch or base row opens: a base's local copy whenever it has one, so Checkout, Rename and Delete
	// act on the local branch. Empty for another kind of row.
	std::optional<std::size_t> RefOfRowMenu(const SidebarRow& row);

	// The refs a branch or base row's menu acts on, into LineRead::Refs; all empty for another kind of row.
	struct RowMenuRefs
	{
		std::optional<std::size_t> Menu;     // the branch's own items, Checkout to Copy Branch Name: RefOfRowMenu
		std::optional<std::size_t> Lines;    // Use as base, Stop using as base, Use as main line: the row's own ref
		std::optional<std::size_t> Compared; // Compare with… opens its history and picker, as a click does: RefOpenedBy
	};

	RowMenuRefs RowMenuRefsOf(const SidebarRow& row, const LineRead& read, const BranchLines& lines);

	// The row that shows `ref`, into LineRead::Refs: a branch row of it, or the base row of it or of its local copy. Empty
	// when none of `rows` shows it.
	std::optional<std::size_t> RowShowingRef(const std::vector<SidebarRow>& rows, std::size_t ref);

	// What a click on the window bar's HEAD chip does to the sidebar for the checked-out branch's row to be listed.
	struct HeadRowReveal
	{
		std::size_t Ref = 0;            // the checked-out branch, into LineRead::Refs
		bool ClearsSearch = false;      // the search hides the row
		std::vector<std::string> Opens; // the keys of the section and the group holding the row that are closed, section first
	};

	// Empty when HEAD is unread, detached or has no commits yet, or when no row of the sidebar would show its branch.
	std::optional<HeadRowReveal> RevealHeadRow(const Core::HeadState* head, const SidebarInput& input);

	// The rows that need no line model, for while the lines are read: the Tags and Stashes sections, as BuildSidebarRows
	// lists them. Nothing is listed until the tag list has landed or failed, so the two sections arrive in one frame and
	// the Stashes never move down a row when the Tags appear. The shown ref opens the section and folder holding it.
	std::vector<SidebarRow> BuildRowsWithoutLines(const std::vector<Core::StashEntry>* stashes, const Core::Error* stashesError,
										   const std::vector<Core::Tag>* tags, const Core::Error* tagsError,
										   std::string_view search, const std::map<std::string, bool>& openByUser,
													 std::string_view shownRef);
}
