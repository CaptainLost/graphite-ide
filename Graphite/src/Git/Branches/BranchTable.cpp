#include "Git/Branches/BranchTable.h"

#include "Git/Branches/BranchListFilter.h"

#include "Core/Base/Ascii.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Graphite {

	namespace {

		constexpr const char* kWhereSeparator = " \xC2\xB7 "; // " · "
		constexpr const char* kLocalWord = "local";
		constexpr const char* kWorktreeWord = "worktree";

		const BranchAuthors kNoAuthors;

		std::chrono::sys_seconds TipTimeOf(const Core::Branch& branch)
		{
			return branch.TipCommit.has_value() ? branch.TipCommit->Committer.When : std::chrono::sys_seconds{};
		}

		void AddAuthorsOf(const Core::Branch& branch, const BranchAuthors& authors, std::vector<BranchAuthor>& into)
		{
			for (BranchAuthor& author : AuthorsOfBranch(branch, authors))
			{
				const bool seen = std::ranges::any_of(into, [&](const BranchAuthor& held) { return held.Key == author.Key; });

				if (!seen)
				{
					into.push_back(std::move(author));
				}
			}
		}

		std::string RemoteNameOf(const Core::Branch& remote)
		{
			const std::size_t slash = remote.Name.find('/');
			return slash == std::string::npos ? remote.Name : remote.Name.substr(0, slash);
		}

		std::unordered_map<std::string, const Core::PatchMatch*> MatchesByFullName(const WorkFacts* facts)
		{
			std::unordered_map<std::string, const Core::PatchMatch*> matches;

			if (facts == nullptr)
			{
				return matches;
			}

			for (const BranchFacts& answer : facts->Branches)
			{
				if (answer.Match.has_value())
				{
					matches.emplace(answer.FullName, &*answer.Match);
				}
			}

			return matches;
		}

		BranchTableRow RowOf(const std::vector<Core::Branch>& branches, std::optional<std::size_t> local,
							 std::optional<std::size_t> remote, const BranchTableInput& input,
							 const std::unordered_map<std::string, const Core::PatchMatch*>& matches)
		{
			const BranchAuthors& authors = input.Authors != nullptr ? *input.Authors : kNoAuthors;

			BranchTableRow row;
			row.Local = local;
			row.Remote = remote;

			const Core::Branch* newest = nullptr;

			for (const std::optional<std::size_t>& index : {local, remote})
			{
				if (!index.has_value())
				{
					continue;
				}

				const Core::Branch& branch = branches[*index];
				AddAuthorsOf(branch, authors, row.Authors);

				if (newest == nullptr || TipTimeOf(branch) > TipTimeOf(*newest))
				{
					newest = &branch;
				}

				if (branch.TipCommit.has_value())
				{
					const std::chrono::sys_seconds when = TipTimeOf(branch);

					if (!row.TipTime.has_value() || when > *row.TipTime)
					{
						row.TipTime = when;
					}
				}
			}

			if (remote.has_value())
			{
				row.RemoteName = RemoteNameOf(branches[*remote]);
			}

			const Core::Branch& shown = local.has_value() ? branches[*local] : branches[*remote];
			row.Name = shown.Name;
			row.IsHead = local.has_value() && shown.IsHead;
			row.InWorktree = local.has_value() && input.HeldByWorktree.contains(shown.FullName);

			if (newest != nullptr && newest->TipCommit.has_value())
			{
				row.Subject = newest->TipCommit->Summary;
			}

			if (local.has_value())
			{
				const auto found = matches.find(shown.FullName);

				if (found != matches.end())
				{
					row.Trunk = *found->second;
				}
			}

			return row;
		}

		bool IsStale(const BranchTableRow& row, std::chrono::sys_seconds now)
		{
			return row.TipTime.has_value() && now - *row.TipTime >= kStaleAfter;
		}

		bool IsMine(const BranchTableRow& row, const BranchAuthors* authors)
		{
			if (authors == nullptr)
			{
				return false;
			}

			return std::ranges::any_of(row.Authors,
									   [&](const BranchAuthor& author) { return IsUserKey(author.Key, *authors); });
		}

		bool InScope(const BranchTableRow& row, BranchScope scope, const BranchTableInput& input)
		{
			switch (scope)
			{
			case BranchScope::All:
				return true;
			case BranchScope::Local:
				return row.Local.has_value();
			case BranchScope::RemoteOnly:
				return !row.Local.has_value();
			case BranchScope::Mine:
				return IsMine(row, input.Authors);
			case BranchScope::Stale:
				return IsStale(row, input.Now);
			}

			return false;
		}

		bool MatchesFilter(const BranchTableRow& row, const std::vector<Core::Branch>& branches,
						   const std::vector<std::string>& words)
		{
			const Core::Branch& shown = row.Local.has_value() ? branches[*row.Local] : branches[*row.Remote];

			// A merged row also answers to its upstream's name.
			if (MatchesWords(shown, row.Authors, words, false))
			{
				return true;
			}

			return row.Local.has_value() && row.Remote.has_value() &&
				   MatchesWords(branches[*row.Remote], row.Authors, words, false);
		}

		// Rows in the order branches list them, before any sort.
		std::vector<BranchTableRow> RowsMatchingFilter(const BranchTableInput& input)
		{
			std::vector<BranchTableRow> rows;

			if (input.Branches == nullptr)
			{
				return rows;
			}

			const std::vector<Core::Branch>& branches = *input.Branches;
			const std::unordered_map<std::string, const Core::PatchMatch*> matches = MatchesByFullName(input.Facts);
			const std::vector<std::string> words = LoweredWordsOf(input.Filter);

			std::unordered_map<std::string, std::size_t> remoteByName;

			for (std::size_t index = 0; index < branches.size(); ++index)
			{
				if (branches[index].Kind == Core::RefKind::Remote)
				{
					remoteByName.emplace(branches[index].Name, index);
				}
			}

			std::unordered_set<std::size_t> pairedRemotes;
			std::vector<std::pair<std::optional<std::size_t>, std::optional<std::size_t>>> pairs;

			for (std::size_t index = 0; index < branches.size(); ++index)
			{
				const Core::Branch& branch = branches[index];

				if (branch.Kind != Core::RefKind::Local)
				{
					continue;
				}

				std::optional<std::size_t> remote;

				if (branch.Upstream.has_value())
				{
					const auto found = remoteByName.find(*branch.Upstream);

					if (found != remoteByName.end())
					{
						remote = found->second;
						pairedRemotes.insert(found->second);
					}
				}

				pairs.emplace_back(index, remote);
			}

			for (std::size_t index = 0; index < branches.size(); ++index)
			{
				if (branches[index].Kind == Core::RefKind::Remote && !pairedRemotes.contains(index))
				{
					pairs.emplace_back(std::nullopt, index);
				}
			}

			for (const auto& [local, remote] : pairs)
			{
				BranchTableRow row = RowOf(branches, local, remote, input, matches);

				if (MatchesFilter(row, branches, words))
				{
					rows.push_back(std::move(row));
				}
			}

			return rows;
		}

		// Rows whose value is missing sort as if it were the smallest.
		int CompareValues(const BranchTableRow& left, const BranchTableRow& right, BranchColumn column)
		{
			const auto three = [](const auto& a, const auto& b) { return a < b ? -1 : (b < a ? 1 : 0); };

			switch (column)
			{
			case BranchColumn::Name:
				return three(Core::Lowered(left.Name), Core::Lowered(right.Name));
			case BranchColumn::Where:
				return three(WhereText(left), WhereText(right));
			case BranchColumn::Trunk:
			{
				const auto key = [](const BranchTableRow& row)
				{
					return row.Trunk.has_value() ? std::pair{row.Trunk->Ahead + 1, row.Trunk->Behind}
												 : std::pair<std::size_t, std::size_t>{0, 0};
				};
				return three(key(left), key(right));
			}
			case BranchColumn::Age:
				return three(left.TipTime, right.TipTime);
			case BranchColumn::Authors:
			{
				const auto key = [](const BranchTableRow& row)
				{ return row.Authors.empty() ? std::string{} : Core::Lowered(row.Authors.front().Name); };
				return three(key(left), key(right));
			}
			case BranchColumn::Subject:
				return three(Core::Lowered(left.Subject), Core::Lowered(right.Subject));
			}

			return 0;
		}

		// Age's ascending order is oldest first; the default, newest first, is its descending order.
		void Sort(std::vector<BranchTableRow>& rows, BranchColumn column, bool ascending)
		{
			std::ranges::stable_sort(rows,
									 [&](const BranchTableRow& left, const BranchTableRow& right)
									 {
										 const int byValue = CompareValues(left, right, column);

										 if (byValue != 0)
										 {
											 return ascending ? byValue < 0 : byValue > 0;
										 }

										 return Core::Lowered(left.Name) < Core::Lowered(right.Name);
									 });
		}
	}

	std::vector<BranchTableRow> BuildBranchTable(const BranchTableInput& input)
	{
		std::vector<BranchTableRow> rows = RowsMatchingFilter(input);

		std::erase_if(rows, [&](const BranchTableRow& row) { return !InScope(row, input.Scope, input); });
		Sort(rows, input.SortBy, input.Ascending);

		return rows;
	}

	std::array<std::size_t, 5> CountBranchScopes(const BranchTableInput& input)
	{
		std::array<std::size_t, 5> counts{};

		for (const BranchTableRow& row : RowsMatchingFilter(input))
		{
			for (std::size_t scope = 0; scope < counts.size(); ++scope)
			{
				if (InScope(row, static_cast<BranchScope>(scope), input))
				{
					++counts[scope];
				}
			}
		}

		return counts;
	}

	std::string WhereText(const BranchTableRow& row)
	{
		std::string text;

		if (row.Local.has_value())
		{
			text = kLocalWord;
		}

		if (row.Remote.has_value())
		{
			text += (text.empty() ? std::string{} : std::string{kWhereSeparator}) + row.RemoteName;
		}

		if (row.InWorktree)
		{
			text += std::string{kWhereSeparator} + kWorktreeWord;
		}

		return text;
	}
}
