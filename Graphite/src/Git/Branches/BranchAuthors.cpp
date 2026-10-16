#include "Git/Branches/BranchAuthors.h"

#include "Git/DefaultBranch.h"

#include "Core/Base/Ascii.h"

#include <algorithm>

namespace Graphite {

	namespace {

		bool IsOidBefore(const Core::Oid& oid, const Core::Oid& otherOid)
		{
			return oid.Bytes < otherOid.Bytes;
		}

		void SortWithoutRepeats(std::vector<Core::Oid>& oids)
		{
			std::sort(oids.begin(), oids.end(), IsOidBefore);
			oids.erase(std::unique(oids.begin(), oids.end()), oids.end());
		}

		bool IsReadableTip(const Core::Oid& tip, const std::vector<Core::Branch>& branches)
		{
			for (const Core::Branch& branch : branches)
			{
				if (branch.Tip == tip && branch.TipCommit.has_value())
				{
					return true;
				}
			}

			return false;
		}

		const Core::TipAuthors* AnswerFor(const Core::Oid& tip, const BranchAuthors& authors)
		{
			const std::unordered_map<Core::Oid, Core::TipAuthors>::const_iterator found = authors.ByTip.find(tip);

			if (found == authors.ByTip.end())
			{
				return nullptr;
			}

			const auto& [foundTip, answer] = *found;
			return &answer;
		}

		BranchAuthor AuthorFrom(const Core::Signature& signature)
		{
			const std::string trimmedName = Core::Trimmed(signature.Name);
			const std::string shownName = trimmedName.empty() ? Core::Trimmed(signature.Email) : trimmedName;

			return BranchAuthor{.Key = Core::AuthorKeyOf(signature.Email), .Name = shownName, .When = signature.When};
		}
	}

	Core::OwnAuthorsRequest OwnAuthorsRequestFor(const std::vector<Core::Branch>& branches)
	{
		Core::OwnAuthorsRequest request;

		// WARNING: The walk fails as a whole on a tip whose commit cannot be read, such as a missing
		// object or a shallow boundary, so such a tip is neither walked nor hidden.
		for (const Core::Branch& branch : branches)
		{
			if (branch.TipCommit.has_value())
			{
				request.Tips.push_back(branch.Tip);
			}
		}

		for (const Core::Oid& trunkTip : TrunkTips(branches))
		{
			if (IsReadableTip(trunkTip, branches))
			{
				request.Hidden.push_back(trunkTip);
			}
		}

		SortWithoutRepeats(request.Tips);
		SortWithoutRepeats(request.Hidden);

		return request;
	}

	std::vector<BranchAuthor> AuthorsOfBranch(const Core::Branch& branch, const BranchAuthors& authors)
	{
		std::vector<BranchAuthor> found;

		const Core::TipAuthors* answer = AnswerFor(branch.Tip, authors);
		const bool hasOwnCommits = answer != nullptr && answer->OwnCommits > 0;

		if (hasOwnCommits)
		{
			for (const Core::Signature& author : answer->Authors)
			{
				found.push_back(AuthorFrom(author));
			}

			return found;
		}

		if (branch.TipCommit.has_value())
		{
			found.push_back(AuthorFrom(branch.TipCommit->Author));
			return found;
		}

		found.push_back(BranchAuthor{.Key = std::string{}, .Name = std::string{kUnknownAuthorName}, .When = {}});
		return found;
	}

	bool IsUserKey(std::string_view key, const BranchAuthors& authors)
	{
		return !key.empty() && authors.UserEmail.has_value() && Core::AuthorKeyOf(*authors.UserEmail) == key;
	}
}
