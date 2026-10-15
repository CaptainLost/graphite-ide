#include "Git/Branches/BranchRows.h"

#include "Git/History/CommitDetails.h"

#include "Core/Base/Ascii.h"

#include <algorithm>
#include <format>
#include <map>

namespace Graphite {

	namespace {

		constexpr const char* kLocalHeading = "Local";
		constexpr const char* kRemoteHeading = "Remote";
		constexpr const char* kLoadingLabel = "Reading authors";
		constexpr const char* kFailurePrefix = "Cannot read authors: ";
		constexpr const char* kNoMatchesLabel = "No matching branches";
		constexpr const char* kUserSuffix = " (you)";

		constexpr std::size_t kTopLevelDepth = 0;
		constexpr std::size_t kInsideGroupDepth = 1;
		constexpr std::size_t kMostAuthorsNamed = 3;

		constexpr Core::RefKind kSectionsInOrder[] = {Core::RefKind::Local, Core::RefKind::Remote};

		struct Person
		{
			std::string Name;
			std::chrono::sys_seconds When;
		};

		using PeopleByKey = std::map<std::string, Person>;
		using CountsByKey = std::map<std::string, std::size_t>;

		struct ListedBranches
		{
			std::vector<std::size_t> Indices;
			std::size_t Total = 0;
		};

		struct AuthorGroup
		{
			std::string Key;
			std::string Label;
			bool IsUser = false;
			std::vector<std::size_t> Indices;
		};

		const char* HeadingOf(Core::RefKind section)
		{
			return section == Core::RefKind::Local ? kLocalHeading : kRemoteHeading;
		}

		bool IsListedBefore(const Core::Branch& branch, const Core::Branch& otherBranch, BranchSort sort)
		{
			if (branch.IsHead != otherBranch.IsHead)
			{
				return branch.IsHead;
			}

			if (sort == BranchSort::Name)
			{
				return branch.Name < otherBranch.Name;
			}

			const bool hasTime = branch.TipCommit.has_value();
			const bool otherHasTime = otherBranch.TipCommit.has_value();

			if (hasTime != otherHasTime)
			{
				return hasTime;
			}

			if (hasTime)
			{
				const std::chrono::sys_seconds when = branch.TipCommit->Committer.When;
				const std::chrono::sys_seconds otherWhen = otherBranch.TipCommit->Committer.When;

				if (when != otherWhen)
				{
					return sort == BranchSort::Newest ? when > otherWhen : when < otherWhen;
				}
			}

			return branch.Name < otherBranch.Name;
		}

		bool ContainsKey(const std::vector<std::string>& keys, const std::string& key)
		{
			for (const std::string& listedKey : keys)
			{
				if (listedKey == key)
				{
					return true;
				}
			}

			return false;
		}

		std::vector<std::string> DistinctKeysOf(const std::vector<BranchAuthor>& authors)
		{
			std::vector<std::string> keys;

			for (const BranchAuthor& author : authors)
			{
				if (!ContainsKey(keys, author.Key))
				{
					keys.push_back(author.Key);
				}
			}

			return keys;
		}

		bool AnyKeyTicked(const std::vector<std::string>& keys, const std::set<std::string>& ticked)
		{
			for (const std::string& key : keys)
			{
				if (ticked.contains(key))
				{
					return true;
				}
			}

			return false;
		}

		bool PassesAuthors(const Core::Branch& branch, const BranchRowsInput& input)
		{
			if (input.Filter.Authors.empty() || input.Authors == nullptr)
			{
				return true;
			}

			return AnyKeyTicked(DistinctKeysOf(AuthorsOfBranch(branch, *input.Authors)), input.Filter.Authors);
		}

		ListedBranches ListSection(const BranchRowsInput& input, Core::RefKind section,
								   const std::vector<std::string>& words)
		{
			ListedBranches listed;
			const std::vector<Core::Branch>& branches = *input.Branches;

			for (std::size_t branchIndex = 0; branchIndex < branches.size(); ++branchIndex)
			{
				const Core::Branch& branch = branches[branchIndex];

				if (branch.Kind != section)
				{
					continue;
				}

				++listed.Total;

				if (MatchesWords(branch, words, input.Filter.AlsoMessage) && PassesAuthors(branch, input))
				{
					listed.Indices.push_back(branchIndex);
				}
			}

			const BranchSort sort = input.Options.Sort;
			std::sort(listed.Indices.begin(), listed.Indices.end(),
					  [&branches, sort](std::size_t branchIndex, std::size_t otherIndex) {
						  return IsListedBefore(branches[branchIndex], branches[otherIndex], sort);
					  });

			return listed;
		}

		void RememberNewerPerson(PeopleByKey& people, const BranchAuthor& author)
		{
			const PeopleByKey::iterator held = people.find(author.Key);

			if (held == people.end())
			{
				people.emplace(author.Key, Person{.Name = author.Name, .When = author.When});
				return;
			}

			auto& [heldKey, heldPerson] = *held;

			if (author.When > heldPerson.When)
			{
				heldPerson = Person{.Name = author.Name, .When = author.When};
			}
		}

		PeopleByKey PeopleOf(const std::vector<Core::Branch>& branches, const BranchAuthors& authors)
		{
			PeopleByKey people;

			for (const Core::Branch& branch : branches)
			{
				for (const BranchAuthor& author : AuthorsOfBranch(branch, authors))
				{
					RememberNewerPerson(people, author);
				}
			}

			return people;
		}

		std::string NameOfKey(const std::string& key, const PeopleByKey& people, const BranchAuthors& authors)
		{
			if (key.empty())
			{
				return std::string{kUnknownAuthorName};
			}

			const PeopleByKey::const_iterator found = people.find(key);

			if (found != people.end())
			{
				const auto& [foundKey, person] = *found;
				return person.Name;
			}

			return IsUserKey(key, authors) ? *authors.UserEmail : key;
		}

		// The user first, the unknown author last, everyone else by name, then by e-mail.
		bool IsAuthorOrderedBefore(bool isUser, const std::string& key, const std::string& name, bool otherIsUser,
								   const std::string& otherKey, const std::string& otherName)
		{
			if (isUser != otherIsUser)
			{
				return isUser;
			}

			const bool isUnknown = key.empty();
			const bool otherIsUnknown = otherKey.empty();

			if (isUnknown != otherIsUnknown)
			{
				return otherIsUnknown;
			}

			const std::string loweredName = Core::Lowered(name);
			const std::string otherLoweredName = Core::Lowered(otherName);

			if (loweredName != otherLoweredName)
			{
				return loweredName < otherLoweredName;
			}

			return key < otherKey;
		}

		bool IsGroupBefore(const AuthorGroup& group, const AuthorGroup& otherGroup)
		{
			return IsAuthorOrderedBefore(group.IsUser, group.Key, group.Label, otherGroup.IsUser, otherGroup.Key,
										 otherGroup.Label);
		}

		bool IsChoiceBefore(const AuthorChoice& choice, const AuthorChoice& otherChoice)
		{
			return IsAuthorOrderedBefore(choice.IsUser, choice.Key, choice.Name, otherChoice.IsUser, otherChoice.Key,
										 otherChoice.Name);
		}

		AuthorGroup* GroupWithKey(std::vector<AuthorGroup>& groups, const std::string& key)
		{
			for (AuthorGroup& group : groups)
			{
				if (group.Key == key)
				{
					return &group;
				}
			}

			return nullptr;
		}

		std::vector<AuthorGroup> GroupByAuthor(const BranchRowsInput& input, const std::vector<std::size_t>& sortedIndices,
											   const PeopleByKey& people)
		{
			std::vector<AuthorGroup> groups;

			for (const std::size_t branchIndex : sortedIndices)
			{
				const Core::Branch& branch = (*input.Branches)[branchIndex];

				for (const std::string& key : DistinctKeysOf(AuthorsOfBranch(branch, *input.Authors)))
				{
					const bool hiddenByAuthorFilter = !input.Filter.Authors.empty() && !input.Filter.Authors.contains(key);

					if (hiddenByAuthorFilter)
					{
						continue;
					}

					AuthorGroup* group = GroupWithKey(groups, key);

					if (group == nullptr)
					{
						const bool isUser = IsUserKey(key, *input.Authors);
						const std::string name = NameOfKey(key, people, *input.Authors);

						groups.push_back(AuthorGroup{.Key = key, .Label = isUser ? name + kUserSuffix : name, .IsUser = isUser});
						group = &groups.back();
					}

					group->Indices.push_back(branchIndex);
				}
			}

			std::sort(groups.begin(), groups.end(), IsGroupBefore);

			return groups;
		}

		void AddBranchRows(std::vector<BranchRow>& rows, Core::RefKind section, const std::vector<std::size_t>& indices,
						   std::size_t depth, const std::string& authorKey)
		{
			for (const std::size_t branchIndex : indices)
			{
				rows.push_back(BranchRow{.Kind = BranchRowKind::Branch, .Section = section, .Depth = depth,
										 .BranchIndex = branchIndex, .AuthorKey = authorKey});
			}
		}

		void AddGroupedRows(std::vector<BranchRow>& rows, const BranchRowsInput& input, Core::RefKind section,
							const std::vector<std::size_t>& indices, const PeopleByKey& people)
		{
			for (const AuthorGroup& group : GroupByAuthor(input, indices, people))
			{
				const AuthorGroupKey groupKey{.Section = section, .Author = group.Key};
				const bool collapsed = !input.ExpandedGroups.contains(groupKey);

				rows.push_back(BranchRow{.Kind = BranchRowKind::AuthorGroup, .Section = section, .Depth = kTopLevelDepth,
										 .Label = group.Label, .AuthorKey = group.Key, .Listed = group.Indices.size(),
										 .Collapsed = collapsed});

				if (!collapsed)
				{
					AddBranchRows(rows, section, group.Indices, kInsideGroupDepth, group.Key);
				}
			}
		}

		BranchRow WaitingRow(const BranchRowsInput& input, Core::RefKind section)
		{
			if (input.AuthorsFailure.has_value())
			{
				return BranchRow{.Kind = BranchRowKind::Failed, .Section = section,
								 .Label = AuthorsFailureText(*input.AuthorsFailure)};
			}

			return BranchRow{.Kind = BranchRowKind::Loading, .Section = section, .Label = kLoadingLabel};
		}

		CountsByKey CountPassingBranchesPerKey(const BranchRowsInput& input, const std::vector<std::string>& words)
		{
			CountsByKey counts;

			for (const Core::Branch& branch : *input.Branches)
			{
				if (!MatchesWords(branch, words, input.Filter.AlsoMessage))
				{
					continue;
				}

				for (const std::string& key : DistinctKeysOf(AuthorsOfBranch(branch, *input.Authors)))
				{
					++counts[key];
				}
			}

			return counts;
		}
}

	std::size_t SectionIndexOf(Core::RefKind section)
	{
		return section == Core::RefKind::Local ? 0 : 1;
	}

	bool ViewNeedsAuthors(const BranchRowsInput& input)
	{
		return input.Options.Group == BranchGrouping::Author || !input.Filter.Authors.empty();
	}

	std::vector<BranchRow> BuildBranchRows(const BranchRowsInput& input)
	{
		std::vector<BranchRow> rows;

		if (input.Branches == nullptr)
		{
			return rows;
		}

		const std::vector<std::string> words = LoweredWordsOf(input.Filter.Text);
		const bool waitingForAuthors = ViewNeedsAuthors(input) && input.Authors == nullptr;
		const bool needsPeople = input.Authors != nullptr && input.Options.Group == BranchGrouping::Author;
		const PeopleByKey people = needsPeople ? PeopleOf(*input.Branches, *input.Authors) : PeopleByKey{};

		for (const Core::RefKind section : kSectionsInOrder)
		{
			const ListedBranches listed = ListSection(input, section, words);
			const bool collapsed = input.CollapsedSections[SectionIndexOf(section)];
			const std::size_t shownCount = waitingForAuthors ? listed.Total : listed.Indices.size();

			rows.push_back(BranchRow{.Kind = BranchRowKind::Section, .Section = section, .Label = HeadingOf(section),
									 .Listed = shownCount, .Total = listed.Total, .Collapsed = collapsed});

			if (collapsed || listed.Total == 0)
			{
				continue;
			}

			if (waitingForAuthors)
			{
				rows.push_back(WaitingRow(input, section));
				continue;
			}

			if (listed.Indices.empty())
			{
				rows.push_back(BranchRow{.Kind = BranchRowKind::NoMatches, .Section = section, .Label = kNoMatchesLabel});
				continue;
			}

			if (input.Options.Group == BranchGrouping::Author)
			{
				AddGroupedRows(rows, input, section, listed.Indices, people);
			}
			else
			{
				const std::string noGroup;
				AddBranchRows(rows, section, listed.Indices, kTopLevelDepth, noGroup);
			}
		}

		return rows;
	}

	std::vector<AuthorChoice> ListAuthorChoices(const BranchRowsInput& input)
	{
		std::vector<AuthorChoice> choices;

		if (input.Branches == nullptr || input.Authors == nullptr)
		{
			return choices;
		}

		const std::vector<std::string> words = LoweredWordsOf(input.Filter.Text);
		const PeopleByKey people = PeopleOf(*input.Branches, *input.Authors);

		std::set<std::string> keys = input.Filter.Authors;

		const CountsByKey counts = CountPassingBranchesPerKey(input, words);

		for (const auto& [key, count] : counts)
		{
			keys.insert(key);
		}

		for (const std::string& key : keys)
		{
			std::size_t branchCount = 0;
			const CountsByKey::const_iterator found = counts.find(key);

			if (found != counts.end())
			{
				const auto& [foundKey, foundCount] = *found;
				branchCount = foundCount;
			}
			const bool ticked = input.Filter.Authors.contains(key);

			if (branchCount == 0 && !ticked)
			{
				continue;
			}

			choices.push_back(AuthorChoice{.Key = key, .Name = NameOfKey(key, people, *input.Authors),
										   .Branches = branchCount, .IsUser = IsUserKey(key, *input.Authors),
										   .Ticked = ticked});
		}

		std::sort(choices.begin(), choices.end(), IsChoiceBefore);

		return choices;
	}

	std::string SectionHeadingText(const BranchRow& row)
	{
		if (row.Listed == row.Total)
		{
			return std::format("{} ({})", row.Label, row.Total);
		}

		return std::format("{} ({} of {})", row.Label, row.Listed, row.Total);
	}

	std::string AuthorGroupText(const BranchRow& row)
	{
		return std::format("{} ({})", row.Label, row.Listed);
	}

	std::string AuthorChoiceText(const AuthorChoice& choice)
	{
		return std::format("{}{} ({})", choice.Name, choice.IsUser ? kUserSuffix : "", choice.Branches);
	}

	std::string AuthorsFailureText(std::string_view message)
	{
		return std::string{kFailurePrefix} + std::string{message};
	}

	std::string BranchTooltipText(const Core::Branch& branch, std::string_view label,
								  const std::vector<BranchAuthor>& authors, std::chrono::minutes localOffset)
	{
		std::string text{label};

		if (branch.TipCommit.has_value() && !branch.TipCommit->Summary.empty())
		{
			text += "\n" + branch.TipCommit->Summary;
		}

		if (!authors.empty())
		{
			text += authors.size() == 1 ? "\nAuthor: " : "\nAuthors: ";

			const std::size_t namedCount = std::min(authors.size(), kMostAuthorsNamed);

			for (std::size_t authorIndex = 0; authorIndex < namedCount; ++authorIndex)
			{
				text += authorIndex == 0 ? authors[authorIndex].Name : ", " + authors[authorIndex].Name;
			}

			if (authors.size() > namedCount)
			{
				text += std::format(" +{}", authors.size() - namedCount);
			}
		}

		if (branch.TipCommit.has_value())
		{
			text += "\n" + FormattedTimestamp(branch.TipCommit->Committer.When, localOffset);
		}

		return text;
	}
}