#include "Git/Lines/BranchLines.h"

#include <map>
#include <string>

namespace Graphite {

	BranchLines::BranchLines(const LineRead& read)
		: m_Read(read),
		  m_LineOfRef(read.Refs.size()),
		  m_BaseOfRef(read.Refs.size()),
		  m_OldCopyOfRef(read.Refs.size())
	{
		m_RefByName.reserve(read.Refs.size());

		for (std::size_t ref = 0; ref < read.Refs.size(); ++ref)
		{
			m_RefByName.emplace(read.Refs[ref].FullName, ref);
		}

		for (std::size_t line = 0; line < read.Model.Branches.size(); ++line)
		{
			m_LineOfRef[read.Model.Branches[line].Ref] = line;
		}

		for (std::size_t base = 0; base < read.Model.Bases.size(); ++base)
		{
			m_BaseOfRef[read.Model.Bases[base].Ref] = base;
		}

		for (std::size_t copy = 0; copy < read.Model.OldCopies.size(); ++copy)
		{
			m_OldCopyOfRef[read.Model.OldCopies[copy].Ref] = copy;
		}
	}

	std::optional<std::size_t> BranchLines::IndexOf(std::string_view fullName) const
	{
		const std::unordered_map<std::string_view, std::size_t>::const_iterator found = m_RefByName.find(fullName);

		if (found == m_RefByName.end())
		{
			return std::nullopt;
		}

		const auto& [name, ref] = *found;
		return ref;
	}

	const Core::BranchLine* BranchLines::LineOf(std::string_view fullName) const
	{
		const std::optional<std::size_t> ref = IndexOf(fullName);

		if (!ref.has_value() || !m_LineOfRef[*ref].has_value())
		{
			return nullptr;
		}

		return &m_Read.Model.Branches[*m_LineOfRef[*ref]];
	}

	const Core::LineBase* BranchLines::BaseNamed(std::string_view fullName) const
	{
		const std::optional<std::size_t> ref = IndexOf(fullName);

		if (!ref.has_value() || !m_BaseOfRef[*ref].has_value())
		{
			return nullptr;
		}

		return &m_Read.Model.Bases[*m_BaseOfRef[*ref]];
	}

	const Core::OldCopy* BranchLines::OldCopyNamed(std::string_view fullName) const
	{
		const std::optional<std::size_t> ref = IndexOf(fullName);

		if (!ref.has_value() || !m_OldCopyOfRef[*ref].has_value())
		{
			return nullptr;
		}

		return &m_Read.Model.OldCopies[*m_OldCopyOfRef[*ref]];
	}

	std::string_view BranchLines::ShownAs(std::string_view fullName) const
	{
		const Core::BranchLine* const line = LineOf(fullName);

		if (line == nullptr || !line->SameRowAs.has_value() || line->State != Core::BranchState::NoOwnCommits || line->ByAnswer)
		{
			return fullName;
		}

		const std::size_t row = *line->SameRowAs;
		const bool standsForAnother = m_BaseOfRef[row].has_value() || m_OldCopyOfRef[row].has_value();

		return standsForAnother ? std::string_view{m_Read.Refs[row].FullName} : fullName;
	}

	std::optional<std::string> BranchLines::ComparedWithName(std::string_view fullName) const
	{
		const Core::BranchLine* line = LineOf(fullName);

		if (line == nullptr || !line->ComparedWith.has_value())
		{
			return std::nullopt;
		}

		return m_Read.Refs[*line->ComparedWith].FullName;
	}

	std::optional<std::size_t> BranchLines::PickedLineOf(std::string_view fullName) const
	{
		if (OldCopyNamed(fullName) != nullptr)
		{
			const Core::BranchLine* const line = LineOf(fullName);

			return line != nullptr && line->ByAnswer ? line->ComparedWith : std::nullopt;
		}

		if (BaseNamed(fullName) == nullptr)
		{
			return std::nullopt;
		}

		const std::map<std::string, std::optional<std::string>>& comparisons = m_Read.Answers.Comparisons;
		const std::map<std::string, std::optional<std::string>>::const_iterator answer = comparisons.find(std::string{fullName});

		if (answer == comparisons.end())
		{
			return std::nullopt;
		}

		const auto& [name, target] = *answer;

		if (!target.has_value())
		{
			return std::nullopt;
		}

		const std::optional<std::size_t> picked = IndexOf(*target);
		const bool isItself = picked == IndexOf(fullName);

		return isItself ? std::nullopt : picked;
	}
}
