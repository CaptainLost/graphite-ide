#include "Git/Branches/BranchLookup.h"

namespace Graphite {

	const Core::Branch* CheckedOutBranch(const std::vector<Core::Branch>& branches)
	{
		for (const Core::Branch& branch : branches)
		{
			if (branch.Kind == Core::RefKind::Local && branch.IsHead)
			{
				return &branch;
			}
		}

		return nullptr;
	}

	const Core::Branch* BranchWithFullName(const std::vector<Core::Branch>& branches, std::string_view fullName)
	{
		for (const Core::Branch& branch : branches)
		{
			if (branch.FullName == fullName)
			{
				return &branch;
			}
		}

		return nullptr;
	}

	std::string UpstreamOfWalk(const std::vector<Core::Branch>& branches, const std::optional<std::string>& ref)
	{
		const Core::Branch* const walked = ref.has_value() ? BranchWithFullName(branches, *ref) : CheckedOutBranch(branches);

		return walked != nullptr ? walked->Upstream.value_or(std::string{}) : std::string{};
	}

	const Core::Branch* RemoteBranchNamed(const std::vector<Core::Branch>& branches, const std::string& name)
	{
		for (const Core::Branch& branch : branches)
		{
			if (branch.Kind == Core::RefKind::Remote && branch.Name == name)
			{
				return &branch;
			}
		}

		return nullptr;
	}
}
