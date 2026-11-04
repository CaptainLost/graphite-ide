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

		// The full name of what the branch is compared with; empty when it is compared with nothing or is no branch line.
		std::optional<std::string> ComparedWithName(std::string_view fullName) const;

	private:
		const LineRead& m_Read;
		std::unordered_map<std::string_view, std::size_t> m_RefByName;

		// Per ref: its place in Model.Branches, Model.Bases and Model.OldCopies.
		std::vector<std::optional<std::size_t>> m_LineOfRef;
		std::vector<std::optional<std::size_t>> m_BaseOfRef;
		std::vector<std::optional<std::size_t>> m_OldCopyOfRef;
	};
}
