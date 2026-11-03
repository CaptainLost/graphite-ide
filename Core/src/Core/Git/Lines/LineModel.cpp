#include "corepch.h"

#include "Core/Git/Lines/LineModel.h"

#include "Core/Git/Lines/LineSplit.h"
#include "Core/Git/Lines/LineThresholds.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Core {

	namespace {

		using Index = AncestryGraph::Index;

		// Real repositories settle in two passes; the cap only keeps a pathological history from going round for ever.
		constexpr std::size_t kMaxBasePasses = 6;

		// The input refs as the model looks them up.
		struct RefTable
		{
			std::span<const LineRef> Refs;
			std::vector<Index> Tips;                                      // per ref; kNoCommit when the graph lacks it
			std::unordered_map<Index, std::vector<std::size_t>> AtCommit; // the refs at each tip, the one that speaks for it first
			std::unordered_map<std::string_view, std::size_t> ByName;     // the first ref of each full name
			std::vector<std::optional<std::size_t>> RowPartner;           // a local branch and its same-named upstream, both ways
			std::unordered_map<std::string_view, std::size_t> RefsPerRemote;
		};

		// Where several refs share a commit, a remote one speaks for it before a local one, then the full names decide.
		bool SpeaksBefore(const LineRef& first, const LineRef& second)
		{
			const bool firstIsRemote = first.Kind == RefKind::Remote;
			const bool secondIsRemote = second.Kind == RefKind::Remote;

			if (firstIsRemote != secondIsRemote)
			{
				return firstIsRemote;
			}

			return first.FullName < second.FullName;
		}

		std::optional<std::size_t> FindRef(const RefTable& table, std::string_view fullName)
		{
			if (!table.ByName.contains(fullName))
			{
				return std::nullopt;
			}

			return table.ByName.at(fullName);
		}

		// A local branch and its upstream are one row only when their short names match.
		std::optional<std::size_t> SameNamedUpstream(const RefTable& table, std::size_t ref)
		{
			const LineRef& branch = table.Refs[ref];

			if (branch.Kind != RefKind::Local || !branch.UpstreamFullName.has_value())
			{
				return std::nullopt;
			}

			const std::optional<std::size_t> upstream = FindRef(table, *branch.UpstreamFullName);
			const bool isAnotherRefWithTheSameShortName = upstream.has_value() && *upstream != ref
														  && table.Refs[*upstream].ShortName == branch.ShortName;
			return isAnotherRefWithTheSameShortName ? upstream : std::nullopt;
		}

		void PairRows(RefTable& table)
		{
			for (std::size_t refIndex = 0; refIndex < table.Refs.size(); ++refIndex)
			{
				const std::optional<std::size_t> upstream = SameNamedUpstream(table, refIndex);

				if (upstream.has_value())
				{
					table.RowPartner[refIndex] = *upstream;
					table.RowPartner[*upstream] = refIndex;
				}
			}
		}

		RefTable BuildRefTable(const AncestryGraph& graph, std::span<const LineRef> refs)
		{
			RefTable table;
			table.Refs = refs;
			table.Tips.reserve(refs.size());
			table.RowPartner.assign(refs.size(), std::nullopt);

			for (std::size_t refIndex = 0; refIndex < refs.size(); ++refIndex)
			{
				const Index tip = graph.Find(refs[refIndex].Tip);
				table.Tips.push_back(tip);
				table.ByName.emplace(refs[refIndex].FullName, refIndex);

				if (!refs[refIndex].Remote.empty())
				{
					++table.RefsPerRemote[refs[refIndex].Remote];
				}

				if (tip != AncestryGraph::kNoCommit)
				{
					table.AtCommit[tip].push_back(refIndex);
				}
			}

			for (std::pair<const Index, std::vector<std::size_t>>& refsAtCommit : table.AtCommit)
			{
				std::stable_sort(refsAtCommit.second.begin(), refsAtCommit.second.end(),
								 [&refs](std::size_t first, std::size_t second) { return SpeaksBefore(refs[first], refs[second]); });
			}

			PairRows(table);
			return table;
		}

		// The ref that speaks for a commit, leaving `excluded` out; nullopt when no other ref points there.
		std::optional<std::size_t> RefAt(const RefTable& table, Index commit, std::optional<std::size_t> excluded)
		{
			if (!table.AtCommit.contains(commit))
			{
				return std::nullopt;
			}

			for (const std::size_t refIndex : table.AtCommit.at(commit))
			{
				const bool isExcluded = excluded.has_value() && *excluded == refIndex;

				if (!isExcluded)
				{
					return refIndex;
				}
			}

			return std::nullopt;
		}

		std::optional<std::size_t> FirstRefNotRemovedAt(const RefTable& table, Index commit, const std::vector<bool>& removed)
		{
			if (!table.AtCommit.contains(commit))
			{
				return std::nullopt;
			}

			for (const std::size_t refIndex : table.AtCommit.at(commit))
			{
				if (!removed[refIndex])
				{
					return refIndex;
				}
			}

			return std::nullopt;
		}

		bool IsActive(const AncestryGraph& graph, Index tip, std::int64_t now)
		{
			return now - graph.TimeOf(tip) <= kActiveWindow;
		}

		std::vector<Index> BaseTipsOf(const RefTable& table, std::span<const LineBase> bases)
		{
			std::vector<Index> baseTips;
			baseTips.reserve(bases.size());

			for (const LineBase& base : bases)
			{
				baseTips.push_back(table.Tips[base.Ref]);
			}

			return baseTips;
		}

		// A split together with what the walk to a ref's line looks up in it.
		struct IndexedSplit
		{
			LineSplit Split;
			std::vector<std::vector<Index>> TipsOnLine;         // per line, the commits on it that refs point at, bottom first
			std::vector<std::optional<std::size_t>> BaseOfLine; // per line, the base whose line it is
		};

		bool IsLowerOnLine(const LineSplit& split, Index first, Index second)
		{
			return split.PositionOf[first] < split.PositionOf[second];
		}

		bool IsBelowPosition(const LineSplit& split, Index commit, std::uint32_t position)
		{
			return split.PositionOf[commit] < position;
		}

		std::vector<std::vector<Index>> TipsOnEachLine(const LineSplit& split, const RefTable& table)
		{
			std::vector<std::vector<Index>> tipsOnLine(split.Lines.size());

			for (const std::pair<const Index, std::vector<std::size_t>>& refsAtCommit : table.AtCommit)
			{
				const std::uint32_t line = split.LineOf[refsAtCommit.first];

				if (line != kNoLine)
				{
					tipsOnLine[line].push_back(refsAtCommit.first);
				}
			}

			for (std::vector<Index>& tips : tipsOnLine)
			{
				std::sort(tips.begin(), tips.end(), [&split](Index first, Index second) { return IsLowerOnLine(split, first, second); });
			}

			return tipsOnLine;
		}

		std::vector<std::optional<std::size_t>> BaseOfEachLine(const LineSplit& split, const RefTable& table, std::span<const LineBase> bases)
		{
			std::vector<std::optional<std::size_t>> baseOfLine(split.Lines.size());

			for (const LineBase& base : bases)
			{
				const std::uint32_t line = split.LineOf[table.Tips[base.Ref]];

				if (line != kNoLine && !baseOfLine[line].has_value())
				{
					baseOfLine[line] = base.Ref;
				}
			}

			return baseOfLine;
		}

		IndexedSplit SplitAndIndex(const AncestryGraph& graph, const RefTable& table, std::span<const LineBase> bases)
		{
			IndexedSplit indexed;
			indexed.Split = SplitLines(graph, table.Tips, BaseTipsOf(table, bases));
			indexed.TipsOnLine = TipsOnEachLine(indexed.Split, table);
			indexed.BaseOfLine = BaseOfEachLine(indexed.Split, table, bases);
			return indexed;
		}

		// The first of a line's tips at or above `position`; the number of its tips when none is.
		std::size_t FirstTipAtOrAbove(const LineSplit& split, const std::vector<Index>& tipsOnLine, std::uint32_t position)
		{
			const std::vector<Index>::const_iterator firstAtOrAbove = std::lower_bound(
				tipsOnLine.begin(), tipsOnLine.end(), position,
				[&split](Index commit, std::uint32_t wanted) { return IsBelowPosition(split, commit, wanted); });
			return static_cast<std::size_t>(firstAtOrAbove - tipsOnLine.begin());
		}

		std::optional<std::size_t> NearestRefBelowOnItsLine(const RefTable& table, const IndexedSplit& indexed, Index tip,
															 std::optional<std::size_t> rowPartner)
		{
			const std::vector<Index>& tipsOnLine = indexed.TipsOnLine[indexed.Split.LineOf[tip]];

			for (std::size_t slot = FirstTipAtOrAbove(indexed.Split, tipsOnLine, indexed.Split.PositionOf[tip]); slot > 0; --slot)
			{
				const std::optional<std::size_t> other = RefAt(table, tipsOnLine[slot - 1], rowPartner);

				if (other.has_value())
				{
					return other;
				}
			}

			return std::nullopt;
		}

		std::optional<std::size_t> LowestRefAtOrAbove(const RefTable& table, const IndexedSplit& indexed, Index commit,
													  std::optional<std::size_t> excluded)
		{
			const std::vector<Index>& tipsOnLine = indexed.TipsOnLine[indexed.Split.LineOf[commit]];

			for (std::size_t slot = FirstTipAtOrAbove(indexed.Split, tipsOnLine, indexed.Split.PositionOf[commit]); slot < tipsOnLine.size(); ++slot)
			{
				const std::optional<std::size_t> other = RefAt(table, tipsOnLine[slot], excluded);

				if (other.has_value())
				{
					return other;
				}
			}

			return std::nullopt;
		}

		// Where the line of `tip` joins another: that line's base, or the lowest ref at or above the join on it.
		std::optional<std::size_t> ComparisonAtJoin(const RefTable& table, const IndexedSplit& indexed, Index tip,
													 std::optional<std::size_t> excluded)
		{
			const Index join = indexed.Split.Lines[indexed.Split.LineOf[tip]].Join;

			if (join == AncestryGraph::kNoCommit)
			{
				return std::nullopt;
			}

			const std::optional<std::size_t> baseOfJoinLine = indexed.BaseOfLine[indexed.Split.LineOf[join]];

			if (baseOfJoinLine.has_value())
			{
				return baseOfJoinLine;
			}

			return LowestRefAtOrAbove(table, indexed, join, excluded);
		}

		// What a ref is compared with before any answer: the nearest other ref below its tip on its own line (a copy at
		// its own commit and its row partner do not count); else, where its line joins another, that line's base, or
		// the lowest ref at or above the join on that line.
		std::optional<std::size_t> AutomaticComparison(const RefTable& table, const IndexedSplit& indexed, std::size_t ref)
		{
			const Index tip = table.Tips[ref];

			if (tip == AncestryGraph::kNoCommit || indexed.Split.LineOf[tip] == kNoLine)
			{
				return std::nullopt;
			}

			const std::optional<std::size_t> builtOn = NearestRefBelowOnItsLine(table, indexed, tip, table.RowPartner[ref]);

			if (builtOn.has_value())
			{
				return builtOn;
			}

			return ComparisonAtJoin(table, indexed, tip, std::nullopt);
		}

		std::vector<bool> MarkRemovedBases(const RefTable& table, const LineAnswers& answers)
		{
			std::vector<bool> removed(table.Refs.size(), false);

			for (const std::string& name : answers.RemovedBases)
			{
				const std::optional<std::size_t> ref = FindRef(table, name);

				if (ref.has_value())
				{
					removed[*ref] = true;
				}
			}

			return removed;
		}

		// A line's guess: the ref at its top that the user has not removed, a remote one before a local one.
		std::optional<std::size_t> GuessAtTopOf(const RefTable& table, const LineSplit& split, std::uint32_t line, const std::vector<bool>& removed)
		{
			return FirstRefNotRemovedAt(table, split.Lines[line].Top, removed);
		}

		// Per line, how many active tips would be compared with a ref on it. The refs at one commit are copies of
		// each other and vote once.
		std::vector<std::size_t> CountVotesPerLine(const AncestryGraph& graph, const RefTable& table, const IndexedSplit& unpinned, std::int64_t now)
		{
			std::vector<std::size_t> votes(unpinned.Split.Lines.size(), 0);

			for (const std::pair<const Index, std::vector<std::size_t>>& refsAtCommit : table.AtCommit)
			{
				if (!IsActive(graph, refsAtCommit.first, now))
				{
					continue;
				}

				const std::optional<std::size_t> comparedWith = AutomaticComparison(table, unpinned, refsAtCommit.second.front());

				if (!comparedWith.has_value())
				{
					continue;
				}

				const std::uint32_t line = unpinned.Split.LineOf[table.Tips[*comparedWith]];

				if (line != kNoLine)
				{
					++votes[line];
				}
			}

			return votes;
		}

		// The lines with the most votes, among those with a ref at their top to guess.
		std::vector<std::uint32_t> MostVotedLines(const RefTable& table, const LineSplit& split, const std::vector<std::size_t>& votes,
												  const std::vector<bool>& removed)
		{
			std::size_t most = 0;
			std::vector<std::uint32_t> lines;

			for (std::uint32_t line = 0; line < votes.size(); ++line)
			{
				const bool hasAsManyVotes = votes[line] > 0 && votes[line] >= most;

				if (!hasAsManyVotes || !GuessAtTopOf(table, split, line, removed).has_value())
				{
					continue;
				}

				if (votes[line] > most)
				{
					most = votes[line];
					lines.clear();
				}

				lines.push_back(line);
			}

			return lines;
		}

		struct VotedLine
		{
			std::size_t Guess = 0;   // the ref at its top that the user has not removed
			std::size_t Authors = 0; // distinct authors of its own commits
			std::int64_t TopTime = 0;
		};

		VotedLine DescribeVotedLine(const AncestryGraph& graph, const RefTable& table, const LineSplit& split, std::uint32_t line,
									const std::vector<bool>& removed, std::int64_t now)
		{
			VotedLine voted;
			voted.Guess = GuessAtTopOf(table, split, line, removed).value();
			voted.Authors = MeasureLine(graph, split, line, {}, now).Authors;
			voted.TopTime = graph.TimeOf(split.Lines[line].Top);
			return voted;
		}

		// Of two lines with as many votes, more distinct authors win, then the newer top, then the guess that speaks
		// first at a shared commit.
		bool IsBetterVotedLine(const RefTable& table, const VotedLine& candidate, const VotedLine& current)
		{
			if (candidate.Authors != current.Authors)
			{
				return candidate.Authors > current.Authors;
			}

			if (candidate.TopTime != current.TopTime)
			{
				return candidate.TopTime > current.TopTime;
			}

			return SpeaksBefore(table.Refs[candidate.Guess], table.Refs[current.Guess]);
		}

		std::optional<std::size_t> GuessFromVotes(const AncestryGraph& graph, const RefTable& table, const IndexedSplit& unpinned,
												  const std::vector<bool>& removed, std::int64_t now)
		{
			const std::vector<std::size_t> votes = CountVotesPerLine(graph, table, unpinned, now);
			const std::vector<std::uint32_t> mostVoted = MostVotedLines(table, unpinned.Split, votes, removed);

			if (mostVoted.empty())
			{
				return std::nullopt;
			}

			if (mostVoted.size() == 1)
			{
				return GuessAtTopOf(table, unpinned.Split, mostVoted.front(), removed);
			}

			VotedLine best = DescribeVotedLine(graph, table, unpinned.Split, mostVoted.front(), removed, now);

			for (std::size_t position = 1; position < mostVoted.size(); ++position)
			{
				const VotedLine candidate = DescribeVotedLine(graph, table, unpinned.Split, mostVoted[position], removed, now);

				if (IsBetterVotedLine(table, candidate, best))
				{
					best = candidate;
				}
			}

			return best.Guess;
		}

		// With no vote, the guess is the top of the line from a root that has the most tips above it, passing over a
		// root line whose refs at the top the user all removed.
		std::optional<std::size_t> TopOfBusiestRootLine(const AncestryGraph& graph, const RefTable& table, const IndexedSplit& unpinned,
														const std::vector<bool>& removed)
		{
			const LineSplit& split = unpinned.Split;
			std::vector<std::uint32_t> rootLineOf(split.Lines.size(), 0);
			std::vector<std::size_t> tipsAbove(split.Lines.size(), 0);

			// A line is started from a commit that is already on a line, so the line its join is on comes before it.
			for (std::uint32_t line = 0; line < split.Lines.size(); ++line)
			{
				const Index join = split.Lines[line].Join;
				rootLineOf[line] = join == AncestryGraph::kNoCommit ? line : rootLineOf[split.LineOf[join]];
				tipsAbove[rootLineOf[line]] += unpinned.TipsOnLine[line].size();
			}

			std::optional<std::uint32_t> busiest;

			for (std::uint32_t line = 0; line < split.Lines.size(); ++line)
			{
				const bool isRootLine = split.Lines[line].Join == AncestryGraph::kNoCommit;

				if (!isRootLine || !GuessAtTopOf(table, split, line, removed).has_value())
				{
					continue;
				}

				const bool hasMoreTips = busiest.has_value() && tipsAbove[line] > tipsAbove[*busiest];
				const bool hasAsManyTipsAndANewerTop = busiest.has_value() && tipsAbove[line] == tipsAbove[*busiest]
													   && graph.TimeOf(split.Lines[line].Top) > graph.TimeOf(split.Lines[*busiest].Top);

				if (!busiest.has_value() || hasMoreTips || hasAsManyTipsAndANewerTop)
				{
					busiest = line;
				}
			}

			if (!busiest.has_value())
			{
				return std::nullopt;
			}

			return GuessAtTopOf(table, split, *busiest, removed);
		}

		// The line most active refs would be compared with, with nothing pinned.
		std::optional<std::size_t> GuessMainLine(const AncestryGraph& graph, const RefTable& table, const std::vector<bool>& removed, std::int64_t now)
		{
			const IndexedSplit unpinned = SplitAndIndex(graph, table, {});
			const std::optional<std::size_t> fromVotes = GuessFromVotes(graph, table, unpinned, removed, now);

			if (fromVotes.has_value())
			{
				return fromVotes;
			}

			return TopOfBusiestRootLine(graph, table, unpinned, removed);
		}

		struct BaseList
		{
			std::vector<LineBase> Bases;
			std::vector<bool> IsBase; // per ref
		};

		bool CanBecomeBase(const RefTable& table, const BaseList& list, const std::vector<bool>& removed, std::size_t ref)
		{
			return table.Tips[ref] != AncestryGraph::kNoCommit && !removed[ref] && !list.IsBase[ref];
		}

		void AddBase(BaseList& list, std::size_t ref, BaseReason reason)
		{
			list.Bases.push_back(LineBase{.Ref = ref, .Reason = reason});
			list.IsBase[ref] = true;
		}

		void AddNamedBase(const RefTable& table, const std::vector<bool>& removed, const std::string& name, BaseReason reason, BaseList& list)
		{
			const std::optional<std::size_t> ref = FindRef(table, name);

			if (ref.has_value() && CanBecomeBase(table, list, removed, *ref))
			{
				AddBase(list, *ref, reason);
			}
		}

		BaseList StartingBases(const RefTable& table, const LineAnswers& answers, const std::vector<bool>& removed)
		{
			BaseList list;
			list.IsBase.assign(table.Refs.size(), false);

			for (std::size_t refIndex = 0; refIndex < table.Refs.size(); ++refIndex)
			{
				if (table.Refs[refIndex].IsRemoteDefault && CanBecomeBase(table, list, removed, refIndex))
				{
					AddBase(list, refIndex, BaseReason::ServerDefault);
				}
			}

			if (answers.MainLine.has_value())
			{
				AddNamedBase(table, removed, *answers.MainLine, BaseReason::MainLineByUser, list);
			}

			for (const std::string& name : answers.AddedBases)
			{
				AddNamedBase(table, removed, name, BaseReason::AddedByUser, list);
			}

			for (const std::string& name : answers.KeptBases)
			{
				AddNamedBase(table, removed, name, BaseReason::Kept, list);
			}

			return list;
		}

		bool HasMainLineBase(const BaseList& list)
		{
			for (const LineBase& base : list.Bases)
			{
				if (base.Reason == BaseReason::ServerDefault || base.Reason == BaseReason::MainLineByUser)
				{
					return true;
				}
			}

			return false;
		}

		bool IsBaseOf(const LineBase& base, std::size_t ref)
		{
			return base.Ref == ref;
		}

		// The guess goes first, as a server default would; a ref the answers already made a base moves there.
		void MakeGuessedMainLine(BaseList& list, std::size_t ref)
		{
			std::erase_if(list.Bases, [ref](const LineBase& base) { return IsBaseOf(base, ref); });
			list.Bases.insert(list.Bases.begin(), LineBase{.Ref = ref, .Reason = BaseReason::MainLineGuess});
			list.IsBase[ref] = true;
		}

		BaseReason RuleThatHolds(const LineQualities& qualities)
		{
			return qualities.MergedLines >= kMergedLinesForBase ? BaseReason::MergedLines : BaseReason::Authors;
		}

		// Measures every line that is no base's line and adds each that qualifies, unless the user removed the ref that
		// speaks for its top. True when any was added.
		bool AddBasesFoundByRules(const AncestryGraph& graph, const RefTable& table, const IndexedSplit& indexed,
								  const std::vector<bool>& removed, std::int64_t now, BaseList& list)
		{
			const std::vector<Index> baseTips = BaseTipsOf(table, list.Bases);
			bool anyAdded = false;

			for (std::uint32_t line = 0; line < indexed.Split.Lines.size(); ++line)
			{
				if (indexed.BaseOfLine[line].has_value())
				{
					continue;
				}

				const std::optional<std::size_t> representative = RefAt(table, indexed.Split.Lines[line].Top, std::nullopt);

				if (!representative.has_value() || !CanBecomeBase(table, list, removed, *representative))
				{
					continue;
				}

				const LineQualities qualities = MeasureLine(graph, indexed.Split, line, baseTips, now);

				if (QualifiesAsBase(qualities))
				{
					AddBase(list, *representative, RuleThatHolds(qualities));
					anyAdded = true;
				}
			}

			return anyAdded;
		}

		// Splits with the bases so far and adds the lines that qualify until a pass adds none. The split returned is
		// the one of the bases as they end up.
		IndexedSplit FindBasesByRules(const AncestryGraph& graph, const RefTable& table, const std::vector<bool>& removed,
									  std::int64_t now, BaseList& list)
		{
			IndexedSplit indexed = SplitAndIndex(graph, table, list.Bases);

			for (std::size_t pass = 0; pass < kMaxBasePasses; ++pass)
			{
				if (!AddBasesFoundByRules(graph, table, indexed, removed, now, list))
				{
					break;
				}

				indexed = SplitAndIndex(graph, table, list.Bases);
			}

			return indexed;
		}

		Index FirstParentOf(const AncestryGraph& graph, Index commit)
		{
			const std::span<const Index> parents = graph.ParentsOf(commit);
			return parents.empty() ? AncestryGraph::kNoCommit : parents.front();
		}

		// Commit times are only numbers here: the walk allows a day of skew below the commit it looks for.
		bool IsOnFirstParentChainBelow(const AncestryGraph& graph, Index commit, Index tip)
		{
			const std::int64_t floor = graph.TimeOf(commit) - kSecondsPerDay;

			for (Index below = FirstParentOf(graph, tip); below != AncestryGraph::kNoCommit && graph.TimeOf(below) >= floor;
				 below = FirstParentOf(graph, below))
			{
				if (below == commit)
				{
					return true;
				}
			}

			return false;
		}

		std::size_t RefsOnRemote(const RefTable& table, const std::string& remote)
		{
			return table.RefsPerRemote.contains(remote) ? table.RefsPerRemote.at(remote) : 0;
		}

		// Of two defaults at one commit, the one whose remote has fewer branches is the old copy; with as many, the one
		// whose remote name comes later.
		bool LosesToDefaultAtSameCommit(const RefTable& table, const LineRef& defaultRef, const LineRef& otherDefault)
		{
			const std::size_t ownCount = RefsOnRemote(table, defaultRef.Remote);
			const std::size_t otherCount = RefsOnRemote(table, otherDefault.Remote);

			if (ownCount != otherCount)
			{
				return ownCount < otherCount;
			}

			return defaultRef.Remote > otherDefault.Remote;
		}

		// A fork's stale default: a server default that a base on another remote with at least as many refs has gone
		// on from along its first parents, or another remote's default at the same commit on a busier remote. A
		// colleague's remote with one integration branch built on the default's tip leaves the default alone.
		bool IsOldCopyOf(const AncestryGraph& graph, const RefTable& table, const LineBase& candidate, const LineBase& other)
		{
			const LineRef& candidateRef = table.Refs[candidate.Ref];
			const LineRef& otherRef = table.Refs[other.Ref];
			const bool otherIsOnAnotherRemote = !otherRef.Remote.empty() && otherRef.Remote != candidateRef.Remote;

			if (candidate.Reason != BaseReason::ServerDefault || !otherIsOnAnotherRemote)
			{
				return false;
			}

			const Index candidateTip = table.Tips[candidate.Ref];
			const Index otherTip = table.Tips[other.Ref];

			if (candidateTip == otherTip)
			{
				return other.Reason == BaseReason::ServerDefault && LosesToDefaultAtSameCommit(table, candidateRef, otherRef);
			}

			const bool otherRemoteHasAsManyRefs = RefsOnRemote(table, otherRef.Remote) >= RefsOnRemote(table, candidateRef.Remote);
			return otherRemoteHasAsManyRefs && IsOnFirstParentChainBelow(graph, candidateTip, otherTip);
		}

		// The first base, other than the one at `position` and those marked skipped, that it is an old copy of.
		std::optional<std::size_t> FirstBaseItCopies(const AncestryGraph& graph, const RefTable& table, const std::vector<LineBase>& bases,
													 std::size_t position, const std::vector<bool>& skipped)
		{
			for (std::size_t other = 0; other < bases.size(); ++other)
			{
				if (other != position && !skipped[other] && IsOldCopyOf(graph, table, bases[position], bases[other]))
				{
					return other;
				}
			}

			return std::nullopt;
		}

		// An old copy stops being a base. True when any did. Of names the first base it copies until
		// NameWhatOldCopiesCopy has the split without the old copies.
		bool SetAsideOldCopies(const AncestryGraph& graph, const RefTable& table, BaseList& list, std::vector<OldCopy>& oldCopies)
		{
			const std::vector<LineBase>& bases = list.Bases;
			const std::vector<bool> noneSkipped(bases.size(), false);
			std::vector<bool> isOldCopy(bases.size(), false);

			for (std::size_t position = 0; position < bases.size(); ++position)
			{
				isOldCopy[position] = FirstBaseItCopies(graph, table, bases, position, noneSkipped).has_value();
			}

			std::vector<LineBase> remaining;

			for (std::size_t position = 0; position < bases.size(); ++position)
			{
				// The bases it copies may all be old copies themselves (one went on along a remote that is its own,
				// or a skewed clock cut a walk short); with none left to name, it stays a base.
				const std::optional<std::size_t> newer = isOldCopy[position] ? FirstBaseItCopies(graph, table, bases, position, isOldCopy)
																			 : std::nullopt;

				if (newer.has_value())
				{
					oldCopies.push_back(OldCopy{.Ref = bases[position].Ref, .Of = bases[*newer].Ref});
					list.IsBase[bases[position].Ref] = false;
				}
				else
				{
					remaining.push_back(bases[position]);
				}
			}

			const bool anySetAside = remaining.size() != bases.size();
			list.Bases = std::move(remaining);
			return anySetAside;
		}

		// An old copy is a copy of the base whose line holds its tip once the old copies are no bases; a base listed
		// before that one, such as a release line forked above the copy's tip, does not take it.
		void NameWhatOldCopiesCopy(const RefTable& table, const IndexedSplit& indexed, std::vector<OldCopy>& oldCopies)
		{
			for (OldCopy& copy : oldCopies)
			{
				const std::uint32_t line = indexed.Split.LineOf[table.Tips[copy.Ref]];

				if (line != kNoLine && indexed.BaseOfLine[line].has_value())
				{
					copy.Of = *indexed.BaseOfLine[line];
				}
			}
		}

		std::vector<bool> RemovedOrOldCopies(const std::vector<bool>& removed, const std::vector<OldCopy>& oldCopies)
		{
			std::vector<bool> excluded = removed;

			for (const OldCopy& copy : oldCopies)
			{
				excluded[copy.Ref] = true;
			}

			return excluded;
		}

		// No default and no answer, or only ones the graph lacks, the user removed, that name no branch or that are
		// old copies. True when a guess was made.
		bool GuessUnlessAMainLineStands(const AncestryGraph& graph, const RefTable& table, const std::vector<bool>& notToGuess,
										std::int64_t now, BaseList& list)
		{
			if (HasMainLineBase(list))
			{
				return false;
			}

			const std::optional<std::size_t> guess = GuessMainLine(graph, table, notToGuess, now);

			if (!guess.has_value())
			{
				return false;
			}

			MakeGuessedMainLine(list, *guess);
			return true;
		}

		// Each base's line as the final split draws it. A kept base its line still qualifies for reports the rule. A
		// base whose tip sits on a line another base listed before it owns has no stretch of its own to measure until
		// it has a commit of its own.
		void DescribeBases(const AncestryGraph& graph, const RefTable& table, const IndexedSplit& indexed, std::int64_t now, std::vector<LineBase>& bases)
		{
			const std::vector<Index> baseTips = BaseTipsOf(table, bases);

			for (LineBase& base : bases)
			{
				const Index tip = table.Tips[base.Ref];
				const std::uint32_t line = indexed.Split.LineOf[tip];
				const bool ownsItsLine = line != kNoLine && indexed.BaseOfLine[line] == base.Ref;
				base.Qualities = ownsItsLine ? MeasureLine(graph, indexed.Split, line, baseTips, now) : LineQualities{};
				base.Active = IsActive(graph, tip, now);

				if (base.Reason == BaseReason::Kept && QualifiesAsBase(base.Qualities))
				{
					base.Reason = RuleThatHolds(base.Qualities);
				}
			}
		}

		// The walk hops from each line's bottom to the commit it grew from, so it takes as many steps as there are
		// lines under `top`, not as many as there are commits.
		bool IsOnFirstParentChainOf(const LineSplit& split, Index commit, Index top)
		{
			for (Index current = top; current != AncestryGraph::kNoCommit && split.LineOf[current] != kNoLine;
				 current = split.Lines[split.LineOf[current]].Join)
			{
				if (split.LineOf[current] == split.LineOf[commit])
				{
					return split.PositionOf[commit] <= split.PositionOf[current];
				}
			}

			return false;
		}

		struct ComparisonAnswer
		{
			bool Applies = false;
			std::optional<std::size_t> ComparedWith; // empty when the user compares the branch with nothing
		};

		// An answer naming a ref the input lacks, or one whose tip the graph lacks, is stale; the rule decides instead.
		ComparisonAnswer AnswerFor(const RefTable& table, const LineAnswers& answers, std::size_t ref)
		{
			const std::string& name = table.Refs[ref].FullName;

			if (!answers.Comparisons.contains(name))
			{
				return ComparisonAnswer{};
			}

			const std::optional<std::string>& answered = answers.Comparisons.at(name);

			if (!answered.has_value())
			{
				return ComparisonAnswer{.Applies = true};
			}

			const std::optional<std::size_t> comparedWith = FindRef(table, *answered);
			const bool namesACommitTheGraphHolds = comparedWith.has_value() && table.Tips[*comparedWith] != AncestryGraph::kNoCommit;

			if (!namesACommitTheGraphHolds)
			{
				return ComparisonAnswer{};
			}

			return ComparisonAnswer{.Applies = true, .ComparedWith = comparedWith};
		}

		// A tip on a base's line has no commits of its own, and AutomaticComparison would look below it for another tip.
		std::optional<std::size_t> ComparisonOfOwnLine(const RefTable& table, const IndexedSplit& indexed, std::size_t ref)
		{
			const Index tip = table.Tips[ref];

			if (tip == AncestryGraph::kNoCommit)
			{
				return std::nullopt;
			}

			const std::uint32_t ownLine = indexed.Split.LineOf[tip];

			if (ownLine != kNoLine && indexed.BaseOfLine[ownLine].has_value())
			{
				return indexed.BaseOfLine[ownLine];
			}

			return AutomaticComparison(table, indexed, ref);
		}

		// A local branch and its same-named upstream are one row. A copy of a base sits in the base's row and is told
		// against the base, even when a branch it was fast-forwarded past lies below it. Any other row is never compared
		// with itself: a ref the rule would compare with its partner takes the partner's comparison.
		std::optional<std::size_t> ComparisonByRule(const RefTable& table, const IndexedSplit& indexed, const std::vector<bool>& isBase,
													std::size_t ref)
		{
			const std::optional<std::size_t> partner = table.RowPartner[ref];

			if (partner.has_value() && isBase[*partner])
			{
				return partner;
			}

			const std::optional<std::size_t> comparison = ComparisonOfOwnLine(table, indexed, ref);
			const bool namesItsOwnRow = comparison.has_value() && comparison == partner;

			if (!namesItsOwnRow)
			{
				return comparison;
			}

			const std::optional<std::size_t> partnersComparison = ComparisonOfOwnLine(table, indexed, *partner);

			if (partnersComparison != ref)
			{
				return partnersComparison;
			}

			return ComparisonAtJoin(table, indexed, table.Tips[ref], partner);
		}

		// Nothing to compare with means the history under the tip ends in a root, or where a shallow clone cut it.
		BranchState StateWithNothingToCompare(const AncestryGraph& graph, const LineSplit& split, Index tip)
		{
			std::uint32_t line = split.LineOf[tip];

			if (line == kNoLine)
			{
				return BranchState::NoCommonHistory;
			}

			while (split.Lines[line].Join != AncestryGraph::kNoCommit)
			{
				line = split.LineOf[split.Lines[line].Join];
			}

			return graph.ParentsCutAt(split.Lines[line].Bottom) ? BranchState::HistoryCutOff : BranchState::NoCommonHistory;
		}

		ComparedKind KindOf(const std::vector<bool>& isBase, std::optional<std::size_t> comparedWith)
		{
			if (!comparedWith.has_value())
			{
				return ComparedKind::None;
			}

			return isBase[*comparedWith] ? ComparedKind::Base : ComparedKind::Branch;
		}

		// Everything but the count. A branch compared with a ref whose first parents do not hold its tip is left
		// NotMerged for CountAhead, which decides between that and MergedByHistory.
		BranchLine DescribeBranch(const AncestryGraph& graph, const RefTable& table, const IndexedSplit& indexed,
								  const std::vector<bool>& isBase, const LineAnswers& answers, std::size_t ref)
		{
			BranchLine branch{.Ref = ref, .SameRowAs = SameNamedUpstream(table, ref)};
			const Index tip = table.Tips[ref];

			if (tip == AncestryGraph::kNoCommit)
			{
				branch.State = BranchState::TipMissing;
				return branch;
			}

			branch.AutomaticComparedWith = ComparisonByRule(table, indexed, isBase, ref);
			branch.AutomaticKind = KindOf(isBase, branch.AutomaticComparedWith);

			const ComparisonAnswer answer = AnswerFor(table, answers, ref);
			branch.ByAnswer = answer.Applies;
			branch.ComparedWith = answer.Applies ? answer.ComparedWith : branch.AutomaticComparedWith;
			branch.Kind = KindOf(isBase, branch.ComparedWith);

			if (!branch.ComparedWith.has_value())
			{
				branch.State = answer.Applies ? BranchState::NotCompared : StateWithNothingToCompare(graph, indexed.Split, tip);
				return branch;
			}

			const bool hasNoOwnCommits = IsOnFirstParentChainOf(indexed.Split, tip, table.Tips[*branch.ComparedWith]);
			branch.State = hasNoOwnCommits ? BranchState::NoOwnCommits : BranchState::NotMerged;
			return branch;
		}

		bool NeedsAheadCount(const BranchLine& branch)
		{
			return branch.ComparedWith.has_value() && branch.State == BranchState::NotMerged;
		}

		constexpr std::uint8_t kSeen = 1;
		constexpr std::uint8_t kFromTip = 1;
		constexpr std::uint8_t kFromCompared = 2;

		// One graph-sized array of marks shared by thousands of walks; each walk clears only what it marked.
		struct WalkScratch
		{
			std::vector<std::uint8_t> Marks; // per commit
			std::vector<Index> Touched;
			std::vector<Index> Pending;
		};

		void ClearMarks(WalkScratch& scratch)
		{
			for (const Index commit : scratch.Touched)
			{
				scratch.Marks[commit] = 0;
			}

			scratch.Touched.clear();
		}

		void MarkSeen(WalkScratch& scratch, Index commit)
		{
			scratch.Marks[commit] = kSeen;
			scratch.Touched.push_back(commit);
		}

		std::vector<bool> CommitsReachableFrom(const AncestryGraph& graph, Index start)
		{
			std::vector<bool> reached(graph.Size(), false);
			std::vector<Index> pending{start};
			reached[start] = true;

			while (!pending.empty())
			{
				const Index commit = pending.back();
				pending.pop_back();

				for (const Index parent : graph.ParentsOf(commit))
				{
					if (!reached[parent])
					{
						reached[parent] = true;
						pending.push_back(parent);
					}
				}
			}

			return reached;
		}

		// The walk never passes a commit `reached` holds, so it visits only the commits it counts and their edge.
		std::size_t CountUnreached(const AncestryGraph& graph, const std::vector<bool>& reached, Index tip, WalkScratch& scratch)
		{
			std::size_t unreached = 0;
			MarkSeen(scratch, tip);
			scratch.Pending.push_back(tip);

			while (!scratch.Pending.empty())
			{
				const Index commit = scratch.Pending.back();
				scratch.Pending.pop_back();

				if (reached[commit])
				{
					continue;
				}

				++unreached;

				for (const Index parent : graph.ParentsOf(commit))
				{
					if (scratch.Marks[parent] == 0)
					{
						MarkSeen(scratch, parent);
						scratch.Pending.push_back(parent);
					}
				}
			}

			ClearMarks(scratch);
			return unreached;
		}

		enum class Visit : std::uint8_t
		{
			NotSeen,
			Open,
			Done
		};

		void PushUnseenParents(const AncestryGraph& graph, const std::vector<Visit>& visits, Index commit, std::vector<Index>& pending)
		{
			for (const Index parent : graph.ParentsOf(commit))
			{
				if (visits[parent] == Visit::NotSeen)
				{
					pending.push_back(parent);
				}
			}
		}

		std::uint32_t HighestGenerationOfParents(const AncestryGraph& graph, const std::vector<std::uint32_t>& generations, Index commit)
		{
			std::uint32_t highest = 0;

			for (const Index parent : graph.ParentsOf(commit))
			{
				highest = std::max(highest, generations[parent]);
			}

			return highest;
		}

		// One more than the highest of its parents', so every descendant of a commit has a higher generation, whatever
		// the clocks said. Depth first on an explicit stack: a first-parent chain can be hundreds of thousands long.
		std::vector<std::uint32_t> GenerationsOf(const AncestryGraph& graph)
		{
			std::vector<std::uint32_t> generations(graph.Size(), 0);
			std::vector<Visit> visits(graph.Size(), Visit::NotSeen);
			std::vector<Index> pending;

			for (Index start = 0; start < graph.Size(); ++start)
			{
				if (visits[start] == Visit::Done)
				{
					continue;
				}

				pending.push_back(start);

				while (!pending.empty())
				{
					const Index commit = pending.back();

					if (visits[commit] == Visit::NotSeen)
					{
						visits[commit] = Visit::Open;
						PushUnseenParents(graph, visits, commit, pending);
						continue;
					}

					pending.pop_back();

					if (visits[commit] == Visit::Open)
					{
						generations[commit] = HighestGenerationOfParents(graph, generations, commit) + 1;
						visits[commit] = Visit::Done;
					}
				}
			}

			return generations;
		}

		struct QueuedCommit
		{
			std::uint32_t Generation = 0;
			Index Commit = AncestryGraph::kNoCommit;
		};

		bool HasLowerGeneration(const QueuedCommit& first, const QueuedCommit& second)
		{
			return first.Generation < second.Generation;
		}

		struct JointWalk
		{
			const std::vector<std::uint32_t>& Generations;
			WalkScratch& Scratch;
			std::vector<QueuedCommit> Queue; // a heap, highest generation first
			std::size_t QueuedFromTipOnly = 0;
		};

		// A commit is queued once, when first reached. It is never reached again after it leaves the queue: all its
		// children have higher generations and left before it.
		void ReachJointly(JointWalk& walk, Index commit, std::uint8_t from)
		{
			const std::uint8_t before = walk.Scratch.Marks[commit];
			const auto after = static_cast<std::uint8_t>(before | from);

			if (after == before)
			{
				return;
			}

			walk.Scratch.Marks[commit] = after;

			if (before == 0)
			{
				walk.Scratch.Touched.push_back(commit);
				walk.Queue.push_back(QueuedCommit{.Generation = walk.Generations[commit], .Commit = commit});
				std::push_heap(walk.Queue.begin(), walk.Queue.end(), HasLowerGeneration);
			}

			if (after == kFromTip)
			{
				++walk.QueuedFromTipOnly;
			}
			else if (before == kFromTip)
			{
				--walk.QueuedFromTipOnly;
			}
		}

		// Walks from the tip and from what it is compared with together, highest generation first, and stops once no
		// queued commit is reached from the tip alone: the rest can only lead to commits both reach. Generations, not
		// commit times, order the walk, so a skewed clock cannot stop it before a commit's last child is seen.
		std::size_t CountAheadJointly(const AncestryGraph& graph, const std::vector<std::uint32_t>& generations, WalkScratch& scratch,
									  Index tip, Index compared)
		{
			JointWalk walk{.Generations = generations, .Scratch = scratch};
			ReachJointly(walk, tip, kFromTip);
			ReachJointly(walk, compared, kFromCompared);
			std::size_t ahead = 0;

			while (walk.QueuedFromTipOnly > 0 && !walk.Queue.empty())
			{
				std::pop_heap(walk.Queue.begin(), walk.Queue.end(), HasLowerGeneration);
				const Index commit = walk.Queue.back().Commit;
				walk.Queue.pop_back();
				const std::uint8_t from = scratch.Marks[commit];

				if (from == kFromTip)
				{
					--walk.QueuedFromTipOnly;
					++ahead;
				}

				for (const Index parent : graph.ParentsOf(commit))
				{
					ReachJointly(walk, parent, from);
				}
			}

			ClearMarks(scratch);
			return ahead;
		}

		bool IsBaseTip(const RefTable& table, const std::vector<bool>& isBase, Index commit)
		{
			for (const std::size_t refIndex : table.AtCommit.at(commit))
			{
				if (isBase[refIndex])
				{
					return true;
				}
			}

			return false;
		}

		void RecordAhead(BranchLine& branch, std::size_t ahead)
		{
			branch.Ahead = ahead;
			branch.State = ahead == 0 ? BranchState::MergedByHistory : BranchState::NotMerged;
		}

		// Thousands of branches are compared with a few bases, each of which can reach most of the history: the
		// branches compared with one base share one walk of what that base reaches. A branch compared with another
		// branch walks together with it instead, which ends where their histories meet.
		void CountAhead(const AncestryGraph& graph, const RefTable& table, const std::vector<bool>& isBase, std::vector<BranchLine>& branches)
		{
			std::unordered_map<Index, std::vector<std::size_t>> comparedWithBaseTip; // positions in branches, per base tip
			std::vector<std::size_t> comparedWithBranch;

			for (std::size_t position = 0; position < branches.size(); ++position)
			{
				if (!NeedsAheadCount(branches[position]))
				{
					continue;
				}

				const Index comparedTip = table.Tips[*branches[position].ComparedWith];

				if (IsBaseTip(table, isBase, comparedTip))
				{
					comparedWithBaseTip[comparedTip].push_back(position);
				}
				else
				{
					comparedWithBranch.push_back(position);
				}
			}

			if (comparedWithBaseTip.empty() && comparedWithBranch.empty())
			{
				return;
			}

			WalkScratch scratch;
			scratch.Marks.assign(graph.Size(), 0);

			for (const auto& [baseTip, positions] : comparedWithBaseTip)
			{
				const std::vector<bool> reached = CommitsReachableFrom(graph, baseTip);

				for (const std::size_t position : positions)
				{
					BranchLine& branch = branches[position];
					RecordAhead(branch, CountUnreached(graph, reached, table.Tips[branch.Ref], scratch));
				}
			}

			if (comparedWithBranch.empty())
			{
				return;
			}

			const std::vector<std::uint32_t> generations = GenerationsOf(graph);

			for (const std::size_t position : comparedWithBranch)
			{
				BranchLine& branch = branches[position];
				const Index tip = table.Tips[branch.Ref];
				const Index comparedTip = table.Tips[*branch.ComparedWith];
				RecordAhead(branch, CountAheadJointly(graph, generations, scratch, tip, comparedTip));
			}
		}

		std::vector<BranchLine> DescribeBranches(const AncestryGraph& graph, const RefTable& table, const IndexedSplit& indexed,
												 const std::vector<bool>& isBase, const LineAnswers& answers)
		{
			std::vector<BranchLine> branches;

			for (std::size_t refIndex = 0; refIndex < table.Refs.size(); ++refIndex)
			{
				if (!isBase[refIndex])
				{
					branches.push_back(DescribeBranch(graph, table, indexed, isBase, answers, refIndex));
				}
			}

			CountAhead(graph, table, isBase, branches);
			return branches;
		}
	}

	LineModel ComputeLines(const LineModelInput& input)
	{
		const AncestryGraph& graph = input.Graph;
		const RefTable table = BuildRefTable(graph, input.Refs);
		const std::vector<bool> removed = MarkRemovedBases(table, input.Answers);
		LineModel model;
		BaseList list = StartingBases(table, input.Answers, removed);
		model.MainLineGuessed = GuessUnlessAMainLineStands(graph, table, removed, input.Now, list);

		IndexedSplit indexed = FindBasesByRules(graph, table, removed, input.Now, list);

		// Run once, after the rules. A fork's default set aside can leave no base standing for the main line.
		if (SetAsideOldCopies(graph, table, list, model.OldCopies))
		{
			const bool guessed = GuessUnlessAMainLineStands(graph, table, RemovedOrOldCopies(removed, model.OldCopies), input.Now, list);
			model.MainLineGuessed = model.MainLineGuessed || guessed;
			indexed = SplitAndIndex(graph, table, list.Bases);
			NameWhatOldCopiesCopy(table, indexed, model.OldCopies);
		}

		DescribeBases(graph, table, indexed, input.Now, list.Bases);
		model.Branches = DescribeBranches(graph, table, indexed, list.IsBase, input.Answers);
		model.Bases = std::move(list.Bases);
		return model;
	}
}
