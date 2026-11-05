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

	private:
		const LineRead& m_Read;
		std::unordered_map<std::string_view, std::size_t> m_RefByName;

		// Per ref: its place in Model.Branches, Model.Bases and Model.OldCopies.
		std::vector<std::optional<std::size_t>> m_LineOfRef;
		std::vector<std::optional<std::size_t>> m_BaseOfRef;
		std::vector<std::optional<std::size_t>> m_OldCopyOfRef;
	};
}
