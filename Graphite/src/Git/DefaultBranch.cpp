#include "Git/DefaultBranch.h"

#include <string_view>

namespace Graphite {

	namespace {

		constexpr std::string_view kOriginPrefix = "origin/";

		const Core::Branch* OriginDefault(const std::vector<Core::Branch>& branches)
		{
			for (const Core::Branch& branch : branches)
			{
				if (branch.Kind == Core::RefKind::Remote && branch.IsRemoteDefault &&
					branch.Name.starts_with(kOriginPrefix))
				{
					return &branch;
				}
			}

			return nullptr;
		}

		void AddUnlessListed(std::vector<Core::Oid>& tips, const Core::Oid& tip)
		{
			for (const Core::Oid& listed : tips)
			{
				if (listed == tip)
				{
					return;
				}
			}

			tips.push_back(tip);
		}
	}

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

	const Core::Branch* DefaultBranch(const std::vector<Core::Branch>& branches)
	{
		const Core::Branch* originDefault = OriginDefault(branches);

		if (originDefault == nullptr)
		{
			return nullptr;
		}

		const std::string_view sameName = std::string_view{originDefault->Name}.substr(kOriginPrefix.size());
		const Core::Branch* firstTracking = nullptr;

		for (const Core::Branch& branch : branches)
		{
			const bool tracksOriginDefault =
				branch.Kind == Core::RefKind::Local && branch.Upstream == originDefault->Name;

			if (!tracksOriginDefault)
			{
				continue;
			}

			if (branch.Name == sameName)
			{
				return &branch;
			}

			if (firstTracking == nullptr)
			{
				firstTracking = &branch;
			}
		}

		return firstTracking != nullptr ? firstTracking : originDefault;
	}

	const Core::Branch* TrunkBranch(const std::vector<Core::Branch>& branches)
	{
		const Core::Branch* defaultBranch = DefaultBranch(branches);

		return defaultBranch != nullptr ? defaultBranch : CheckedOutBranch(branches);
	}

	std::vector<Core::Oid> TrunkTips(const std::vector<Core::Branch>& branches)
	{
		const Core::Branch* trunk = TrunkBranch(branches);

		std::vector<Core::Oid> tips;

		if (trunk == nullptr)
		{
			return tips;
		}

		tips.push_back(trunk->Tip);

		if (!trunk->Upstream.has_value())
		{
			return tips;
		}

		if (const Core::Branch* upstream = RemoteBranchNamed(branches, *trunk->Upstream); upstream != nullptr)
		{
			AddUnlessListed(tips, upstream->Tip);
		}

		return tips;
	}
}
