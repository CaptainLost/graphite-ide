#pragma once

#include "Git/Lines/LineReader.h"

#include "Core/Git/Lines/LineModel.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Graphite {

	// What a reference's history is compared with. Exactly one of Picked, Base, OldCopy and Line is set, the one What
	// names; Nothing sets none.
	struct ShownComparison
	{
		enum class Kind
		{
			Nothing, // a name the read lacks, or a ref with no line
			Pick,	 // the line the user picked for a base or an old copy
			Base,	 // a base with no pick: compared with nothing
			OldCopy, // an old copy with no pick: compared with nothing
			Line,	 // a branch's own line, as the model drew it
		};

		Kind What = Kind::Nothing;
		std::string_view Shown; // the ref whose row stands for it: BranchLines::ShownAs
		std::optional<std::size_t> Picked; // into LineRead::Refs
		const Core::LineBase* Base = nullptr;
		const Core::OldCopy* OldCopy = nullptr;
		const Core::BranchLine* Line = nullptr;
	};

	// What the read says about each reference, found by its full name.
	// WARNING: Holds the read by reference and views into its names; the read must outlive it.
	class BranchLines
	{
	public:
		explicit BranchLines(const LineRead& read);

		std::optional<std::size_t> IndexOf(std::string_view fullName) const; // into LineRead::Refs

		const Core::BranchLine* LineOf(std::string_view fullName) const; // null for a base and for a name not read
		const Core::LineBase* BaseNamed(std::string_view fullName) const;
		const Core::OldCopy* OldCopyNamed(std::string_view fullName) const;

		// The base or old copy whose row a local branch is shown in (SameRowAs), while it has no commits of its own and the
		// user picked no line for it; else `fullName` itself. A view of the read's name or of `fullName`.
		std::string_view ShownAs(std::string_view fullName) const;

		// The full name of what the branch is compared with; empty when it is compared with nothing or is no branch line.
		std::optional<std::string> ComparedWithName(std::string_view fullName) const;

		// What the user picked to compare a base or an old copy with, into LineRead::Refs. The rule compares neither with
		// anything; the model gives a base no line, so a base's pick is read from the answers, and an old copy's is its
		// line's. Empty when nothing is picked, when the pick is nothing, when it names a ref the read lacks, and for any
		// other ref.
		std::optional<std::size_t> PickedLineOf(std::string_view fullName) const;

		// What the history of `fullName` is compared with, read from the ref that stands for it (ShownAs): the user's pick
		// first, then a base or an old copy, which the rule compares with nothing, then the branch's own line. Every view
		// that says what a history is compared with asks this, so they cannot disagree on the order.
		ShownComparison ComparisonOf(std::string_view fullName) const;

	private:
		const LineRead& m_Read;
		std::unordered_map<std::string_view, std::size_t> m_RefByName;

		// Per ref: its place in Model.Branches, Model.Bases and Model.OldCopies.
		std::vector<std::optional<std::size_t>> m_LineOfRef;
		std::vector<std::optional<std::size_t>> m_BaseOfRef;
		std::vector<std::optional<std::size_t>> m_OldCopyOfRef;
	};
}
