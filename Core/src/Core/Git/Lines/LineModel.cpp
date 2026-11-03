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
		void PairRows(RefTable& table)
		{
			for (std::size_t refIndex = 0; refIndex < table.Refs.size(); ++refIndex)
			{
				const LineRef& ref = table.Refs[refIndex];

				if (ref.Kind != RefKind::Local || !ref.UpstreamFullName.has_value())
				{
					continue;
				}

				const std::optional<std::size_t> upstream = FindRef(table, *ref.UpstreamFullName);

				if (upstream.has_value() && table.Refs[*upstream].ShortName == ref.ShortName)
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

		std::optional<std::size_t> LowestRefAtOrAbove(const RefTable& table, const IndexedSplit& indexed, Index commit)
		{
			const std::vector<Index>& tipsOnLine = indexed.TipsOnLine[indexed.Split.LineOf[commit]];
			const std::size_t slot = FirstTipAtOrAbove(indexed.Split, tipsOnLine, indexed.Split.PositionOf[commit]);

			if (slot == tipsOnLine.size())
			{
				return std::nullopt;
			}

			return RefAt(table, tipsOnLine[slot], std::nullopt);
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

			return LowestRefAtOrAbove(table, indexed, join);
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

		// How often each ref is what an active ref would be compared with. A ref the user removed passes the count on to
		// the next ref at its commit, so that removing it does not throw away its line's count.
		std::vector<std::size_t> CountTimesNamed(const AncestryGraph& graph, const RefTable& table, const IndexedSplit& indexed,
												 const std::vector<bool>& removed, std::int64_t now)
		{
			std::vector<std::size_t> timesNamed(table.Refs.size(), 0);

			for (std::size_t refIndex = 0; refIndex < table.Refs.size(); ++refIndex)
			{
				const Index tip = table.Tips[refIndex];

				if (tip == AncestryGraph::kNoCommit || !IsActive(graph, tip, now))
				{
					continue;
				}

				const std::optional<std::size_t> comparedWith = AutomaticComparison(table, indexed, refIndex);

				if (!comparedWith.has_value())
				{
					continue;
				}

				const std::optional<std::size_t> counted = removed[*comparedWith]
															   ? FirstRefNotRemovedAt(table, table.Tips[*comparedWith], removed)
															   : comparedWith;

				if (counted.has_value())
				{
					++timesNamed[*counted];
				}
			}

			return timesNamed;
		}

		std::size_t MostTimesNamed(const std::vector<std::size_t>& timesNamed, const std::vector<bool>& removed)
		{
			std::size_t most = 0;

			for (std::size_t refIndex = 0; refIndex < timesNamed.size(); ++refIndex)
			{
				if (!removed[refIndex] && timesNamed[refIndex] > most)
				{
					most = timesNamed[refIndex];
				}
			}

			return most;
		}

		// A guess named as often as another wins with more distinct authors on its line, then with the newer tip, then
		// by speaking first at a shared commit.
		bool IsBetterGuess(const AncestryGraph& graph, const RefTable& table, std::size_t candidate, std::size_t candidateAuthors,
						   std::size_t current, std::size_t currentAuthors)
		{
			if (candidateAuthors != currentAuthors)
			{
				return candidateAuthors > currentAuthors;
			}

			const std::int64_t candidateTime = graph.TimeOf(table.Tips[candidate]);
			const std::int64_t currentTime = graph.TimeOf(table.Tips[current]);

			if (candidateTime != currentTime)
			{
				return candidateTime > currentTime;
			}

			return SpeaksBefore(table.Refs[candidate], table.Refs[current]);
		}

		std::vector<std::size_t> RefsNamedTimes(const std::vector<std::size_t>& timesNamed, const std::vector<bool>& removed, std::size_t times)
		{
			std::vector<std::size_t> refs;

			for (std::size_t refIndex = 0; refIndex < timesNamed.size(); ++refIndex)
			{
				if (!removed[refIndex] && timesNamed[refIndex] == times)
				{
					refs.push_back(refIndex);
				}
			}

			return refs;
		}

		// Per ref, the distinct authors of the line its tip is on; each line is measured once.
		std::vector<std::size_t> AuthorsOnLinesOf(const AncestryGraph& graph, const RefTable& table, const IndexedSplit& unpinned,
												  const std::vector<std::size_t>& refs, std::int64_t now)
		{
			std::vector<std::optional<std::size_t>> authorsOnLine(unpinned.Split.Lines.size());
			std::vector<std::size_t> authors;
			authors.reserve(refs.size());

			for (const std::size_t ref : refs)
			{
				const std::uint32_t line = unpinned.Split.LineOf[table.Tips[ref]];

				if (!authorsOnLine[line].has_value())
				{
					authorsOnLine[line] = MeasureLine(graph, unpinned.Split, line, {}, now).Authors;
				}

				authors.push_back(*authorsOnLine[line]);
			}

			return authors;
		}

		std::optional<std::size_t> MostNamedRef(const AncestryGraph& graph, const RefTable& table, const IndexedSplit& unpinned,
												const std::vector<bool>& removed, std::int64_t now)
		{
			const std::vector<std::size_t> timesNamed = CountTimesNamed(graph, table, unpinned, removed, now);
			const std::size_t most = MostTimesNamed(timesNamed, removed);

			if (most == 0)
			{
				return std::nullopt;
			}

			const std::vector<std::size_t> tied = RefsNamedTimes(timesNamed, removed, most);

			if (tied.size() == 1)
			{
				return tied.front();
			}

			const std::vector<std::size_t> authors = AuthorsOnLinesOf(graph, table, unpinned, tied, now);
			std::size_t best = 0;

			for (std::size_t position = 1; position < tied.size(); ++position)
			{
				if (IsBetterGuess(graph, table, tied[position], authors[position], tied[best], authors[best]))
				{
					best = position;
				}
			}

			return tied[best];
		}

		// With nothing named, the guess is the top of the line from a root that has the most tips above it.
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
				if (split.Lines[line].Join != AncestryGraph::kNoCommit)
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

			return FirstRefNotRemovedAt(table, split.Lines[*busiest].Top, removed);
		}

		// The line most active refs would be compared with, with nothing pinned.
		std::optional<std::size_t> GuessMainLine(const AncestryGraph& graph, const RefTable& table, const std::vector<bool>& removed, std::int64_t now)
		{
			const IndexedSplit unpinned = SplitAndIndex(graph, table, {});
			const std::optional<std::size_t> mostNamed = MostNamedRef(graph, table, unpinned, removed, now);

			if (mostNamed.has_value())
			{
				return mostNamed;
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

		bool IsStrictAncestor(const AncestryGraph& graph, Index ancestor, Index descendant)
		{
			return ancestor != descendant && IsAncestor(graph, ancestor, descendant);
		}

		// The first base, other than the one at `position` and those marked skipped, whose tip descends from its tip.
		std::optional<std::size_t> FirstBaseAhead(const AncestryGraph& graph, const RefTable& table, const std::vector<LineBase>& bases,
												  std::size_t position, const std::vector<bool>& skipped)
		{
			const Index tip = table.Tips[bases[position].Ref];

			for (std::size_t other = 0; other < bases.size(); ++other)
			{
				if (other != position && !skipped[other] && IsStrictAncestor(graph, tip, table.Tips[bases[other].Ref]))
				{
					return other;
				}
			}

			return std::nullopt;
		}

		// A server default whose tip is a strict ancestor of another base's tip is an old copy of that base (a fork's
		// stale default) and stops being a base. True when any did.
		bool SetAsideOldCopies(const AncestryGraph& graph, const RefTable& table, BaseList& list, std::vector<OldCopy>& oldCopies)
		{
			const std::vector<LineBase>& bases = list.Bases;
			const std::vector<bool> noneSkipped(bases.size(), false);
			std::vector<bool> isOldCopy(bases.size(), false);

			for (std::size_t position = 0; position < bases.size(); ++position)
			{
				isOldCopy[position] = bases[position].Reason == BaseReason::ServerDefault
									  && FirstBaseAhead(graph, table, bases, position, noneSkipped).has_value();
			}

			std::vector<LineBase> remaining;

			for (std::size_t position = 0; position < bases.size(); ++position)
			{
				// Clock skew can stop the ancestry walk short, so the only bases found ahead may be old copies
				// themselves; with none left to name, it stays a base.
				const std::optional<std::size_t> newer = isOldCopy[position] ? FirstBaseAhead(graph, table, bases, position, isOldCopy)
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

		// Each base's line as the final split draws it. A kept base its line still qualifies for reports the rule.
		void DescribeBases(const AncestryGraph& graph, const RefTable& table, const LineSplit& split, std::int64_t now, std::vector<LineBase>& bases)
		{
			const std::vector<Index> baseTips = BaseTipsOf(table, bases);

			for (LineBase& base : bases)
			{
				const Index tip = table.Tips[base.Ref];
				base.Qualities = MeasureLine(graph, split, split.LineOf[tip], baseTips, now);
				base.Active = IsActive(graph, tip, now);

				if (base.Reason == BaseReason::Kept && QualifiesAsBase(base.Qualities))
				{
					base.Reason = RuleThatHolds(base.Qualities);
				}
			}
		}
	}

	LineModel ComputeLines(const LineModelInput& input)
	{
		const AncestryGraph& graph = input.Graph;
		const RefTable table = BuildRefTable(graph, input.Refs);
		const std::vector<bool> removed = MarkRemovedBases(table, input.Answers);
		LineModel model;
		BaseList list = StartingBases(table, input.Answers, removed);

		// No default and no answer, or only ones the graph lacks, the user removed or that name no branch.
		if (!HasMainLineBase(list))
		{
			const std::optional<std::size_t> guess = GuessMainLine(graph, table, removed, input.Now);

			if (guess.has_value())
			{
				MakeGuessedMainLine(list, *guess);
				model.MainLineGuessed = true;
			}
		}

		IndexedSplit indexed = FindBasesByRules(graph, table, removed, input.Now, list);

		if (SetAsideOldCopies(graph, table, list, model.OldCopies))
		{
			indexed = SplitAndIndex(graph, table, list.Bases);
		}

		DescribeBases(graph, table, indexed.Split, input.Now, list.Bases);
		model.Bases = std::move(list.Bases);
		return model;
	}
}
