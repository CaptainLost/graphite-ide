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

			return BranchAuthor{.Key = AuthorKeyOf(signature.Email), .Name = shownName, .When = signature.When};
		}
	}

	std::string AuthorKeyOf(std::string_view email)
	{
		return Core::Lowered(Core::Trimmed(email));
	}

	Core::OwnAuthorsRequest OwnAuthorsRequestFor(const std::vector<Core::Branch>& branches)
	{
		Core::OwnAuthorsRequest request;

		for (const Core::Branch& branch : branches)
		{
			if (!branch.Tip.IsZero())
			{
				request.Tips.push_back(branch.Tip);
			}
		}

		request.Hidden = TrunkTips(branches);

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
		return !key.empty() && authors.UserEmail.has_value() && AuthorKeyOf(*authors.UserEmail) == key;
	}
}