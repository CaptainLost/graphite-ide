#include <doctest/doctest.h>

#include "Git/Content/ContentModel.h"
#include "Git/Content/ContentOrder.h"
#include "Git/Content/ContentPort.h"
#include "Git/Content/ContentStore.h"
#include "Git/Content/ContentWriter.h"
#include "Git/Content/PatchIdFile.h"
#include "Git/Content/RememberedVerdicts.h"
#include "Git/Lines/LineReader.h"
#include "Git/Lines/LineStore.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Content/ContentCheck.h"
#include "Core/Git/Lines/LineQuestions.h"
#include "Core/Git/Repository.h"

#include "TempDirectory.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace Graphite;

namespace {
	std::string EnvironmentText(const char* name)
	{
		char* value = nullptr;
		std::size_t length = 0;
		if (::_dupenv_s(&value, &length, name) != 0 || value == nullptr)
		{
			return {};
		}

		std::string text{value};
		std::free(value);
		return text;
	}

	double Milliseconds(std::chrono::steady_clock::duration duration)
	{
		return std::chrono::duration<double, std::milli>(duration).count();
	}

	const char* ReasonText(Core::BaseReason reason)
	{
		switch (reason)
		{
		case Core::BaseReason::ServerDefault: return "ServerDefault";
		case Core::BaseReason::MainLineByUser: return "MainLineByUser";
		case Core::BaseReason::MainLineGuess: return "MainLineGuess";
		case Core::BaseReason::AddedByUser: return "AddedByUser";
		case Core::BaseReason::Kept: return "Kept";
		case Core::BaseReason::MergedLines: return "MergedLines";
		case Core::BaseReason::Authors: return "Authors";
		}

		return "?";
	}

	const char* KindText(Core::ComparedKind kind)
	{
		switch (kind)
		{
		case Core::ComparedKind::None: return "None";
		case Core::ComparedKind::Base: return "Base";
		case Core::ComparedKind::Branch: return "Branch";
		}

		return "?";
	}

	const char* StateText(Core::BranchState state)
	{
		switch (state)
		{
		case Core::BranchState::NoOwnCommits: return "NoOwnCommits";
		case Core::BranchState::MergedByHistory: return "MergedByHistory";
		case Core::BranchState::NotMerged: return "NotMerged";
		case Core::BranchState::NotCompared: return "NotCompared";
		case Core::BranchState::NoCommonHistory: return "NoCommonHistory";
		case Core::BranchState::HistoryCutOff: return "HistoryCutOff";
		case Core::BranchState::TipMissing: return "TipMissing";
		}

		return "?";
	}

	// One candidate's check for a merge by content, as the content run times it.
	struct ContentRun
	{
		std::size_t Branch = 0; // into read.Model.Branches
		ContentKey Key;
		std::optional<Core::ContentVerdict> Verdict; // empty: the check failed
		std::string Failure;
		double Milliseconds = 0.0;
	};

	std::string OutcomeText(const ContentRun& run)
	{
		if (!run.Verdict.has_value())
		{
			return "Failed";
		}

		switch (run.Verdict->Outcome)
		{
		case Core::ContentOutcome::Merged:
			if (run.Verdict->Kind == Core::ContentKind::Rebase)
			{
				return "Merged rebase";
			}

			return run.Verdict->Landing.has_value() ? "Merged squash with its landing" : "Merged squash without a landing";
		case Core::ContentOutcome::NotMerged: return "NotMerged";
		case Core::ContentOutcome::Unknown: return "Unknown";
		}

		return "?";
	}

	bool IsMerged(const ContentRun& run)
	{
		return run.Verdict.has_value() && run.Verdict->Outcome == Core::ContentOutcome::Merged;
	}

	bool IsSlower(const ContentRun* left, const ContentRun* right)
	{
		return left->Milliseconds > right->Milliseconds;
	}

	double Percentile(std::vector<double> values, double fraction)
	{
		if (values.empty())
		{
			return 0.0;
		}

		std::sort(values.begin(), values.end());
		const std::size_t at = std::min(values.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(values.size())));
		return values[at];
	}

	const std::string& BranchNameOf(const LineRead& read, std::size_t branch)
	{
		return read.Refs[read.Model.Branches[branch].Ref].FullName;
	}

	const std::string& ComparedLineOf(const LineRead& read, std::size_t branch)
	{
		return read.Refs[*read.Model.Branches[branch].ComparedWith].FullName;
	}

	// Every candidate, in the order the window checks them with no row in view, sharing one patch id cache as one store
	// does. Each check is written to `out` as it ends, so a long run can be watched.
	std::vector<ContentRun> CheckEveryCandidate(const Core::Repository& repository, const LineRead& read, Core::PatchIdCache& cache,
												std::ofstream* out)
	{
		std::vector<ContentRun> runs;

		for (const std::size_t branch : OrderCandidates(read, {}, std::nullopt))
		{
			const std::optional<ContentKey> key = ContentKeyOf(read, read.Model.Branches[branch]);

			if (!key.has_value())
			{
				continue;
			}

			ContentRun run{.Branch = branch, .Key = *key};

			const auto start = std::chrono::steady_clock::now();
			const Core::Result<Core::ContentVerdict> verdict = Core::CheckMergedByContent(repository, key->BranchTip, key->LineTip, cache);
			run.Milliseconds = Milliseconds(std::chrono::steady_clock::now() - start);

			if (verdict.HasValue())
			{
				run.Verdict = verdict.GetValue();
			}
			else
			{
				run.Failure = verdict.GetError().Message;
			}

			if (out != nullptr)
			{
				const std::string landing = IsMerged(run) && run.Verdict->Landing.has_value() ? run.Verdict->Landing->ToHex() : std::string{};
				const std::size_t twins = run.Verdict.has_value() ? run.Verdict->Twins.size() : 0;

				*out << BranchNameOf(read, branch) << '\t' << ComparedLineOf(read, branch) << '\t' << OutcomeText(run) << '\t' << landing << '\t'
					 << twins << '\t' << run.Milliseconds << '\t' << read.Model.Branches[branch].Ahead << '\t' << run.Failure << std::endl;
			}

			runs.push_back(std::move(run));
		}

		return runs;
	}

	void ReportContentRuns(const LineRead& read, const std::vector<ContentRun>& runs, double wallMilliseconds)
	{
		constexpr double kSlowCheckMilliseconds = 5000.0;
		constexpr std::size_t kSlowestListed = 10;

		std::map<std::string, std::size_t> perOutcome;
		std::vector<const ContentRun*> slowest;
		std::vector<double> times;
		double total = 0.0;
		std::size_t overFiveSeconds = 0;

		for (const ContentRun& run : runs)
		{
			++perOutcome[OutcomeText(run)];
			slowest.push_back(&run);
			times.push_back(run.Milliseconds);
			total += run.Milliseconds;

			if (run.Milliseconds > kSlowCheckMilliseconds)
			{
				++overFiveSeconds;
			}
		}

		MESSAGE("content checks: ", runs.size(), ", total ", total, " ms (wall ", wallMilliseconds, " ms), median ", Percentile(times, 0.5),
				" ms, 90th ", Percentile(times, 0.9), " ms, 99th ", Percentile(times, 0.99), " ms, over 5 s: ", overFiveSeconds);

		for (const auto& [outcome, count] : perOutcome)
		{
			MESSAGE("content ", outcome, ": ", count);
		}

		std::sort(slowest.begin(), slowest.end(), IsSlower);

		for (std::size_t place = 0; place < std::min(kSlowestListed, slowest.size()); ++place)
		{
			const ContentRun& run = *slowest[place];
			MESSAGE("slow check ", place + 1, ": ", BranchNameOf(read, run.Branch), " vs ", ComparedLineOf(read, run.Branch), " ",
					run.Milliseconds, " ms, ", OutcomeText(run), ", ahead ", read.Model.Branches[run.Branch].Ahead);
		}

		for (const ContentRun& run : runs)
		{
			if (IsMerged(run))
			{
				const std::string landing = run.Verdict->Landing.has_value() ? " in " + run.Verdict->Landing->ShortHex() : std::string{};
				MESSAGE("merged by content: ", BranchNameOf(read, run.Branch), " into ", ComparedLineOf(read, run.Branch), ", ",
						OutcomeText(run), landing, ", copies ", run.Verdict->Twins.size());
			}
			else if (!run.Verdict.has_value())
			{
				MESSAGE("check failed: ", BranchNameOf(read, run.Branch), ": ", run.Failure);
			}
		}
	}

	// The job a moved line asks for: whether each line tip still holds the remembered landings and copies, one walk of the
	// graph per distinct line tip.
	void TimeRecheck(const LineRead& read, const std::vector<ContentRun>& runs)
	{
		std::vector<RememberedVerdict> remembered;
		std::set<Core::Oid> lineTips;

		for (const ContentRun& run : runs)
		{
			if (IsMerged(run))
			{
				remembered.push_back(RememberedVerdict{.Key = run.Key, .Verdict = *run.Verdict});
				lineTips.insert(run.Key.LineTip);
			}
		}

		const auto start = std::chrono::steady_clock::now();
		const std::vector<RememberedVerdict> held = VerdictsStillHeld(*read.Graph, remembered);
		const double elapsed = Milliseconds(std::chrono::steady_clock::now() - start);

		MESSAGE("recheck job: ", remembered.size(), " remembered verdicts over ", lineTips.size(), " line tips in ", elapsed, " ms, ", held.size(),
				" held");
	}

	// Never asked: the model it serves is never on screen.
	class PortAskedNothing final : public ContentPort
	{
	public:
		std::uint64_t RepositoryNow() override { return 1; }

		Core::Result<Core::ContentVerdict> CheckContent(std::uint64_t, const Core::Oid&, const Core::Oid&, Core::PatchIdCache&) override
		{
			return Core::Error{Core::ErrorCode::Cancelled, "not asked"};
		}

		Core::Result<Core::PatchTwinSearch> FindTwin(std::uint64_t, const Core::Oid&, const Core::Oid&, std::size_t, Core::PatchIdCache&) override
		{
			return Core::Error{Core::ErrorCode::Cancelled, "not asked"};
		}
	};

	void DropJob(std::function<void()>)
	{
	}

	// The second opening's share of the UI thread: content.json written as the model writes it, then parsed, and the
	// whole first Follow of a new model; with how many candidates that first frame still shows pending.
	void TimeCacheOpening(const LineRead& read, const std::vector<ContentRun>& runs, const Core::PatchIdCache& patchIds)
	{
		ContentCache cache;

		for (const ContentRun& run : runs)
		{
			if (!run.Verdict.has_value())
			{
				continue;
			}

			cache.Verdicts[run.Key] = *run.Verdict;

			if (run.Verdict->Outcome == Core::ContentOutcome::Merged)
			{
				cache.Found[run.Key.BranchTip] = ContentFound{.Line = ComparedLineOf(read, run.Branch), .Verdict = *run.Verdict};
			}
		}

		const auto trimStart = std::chrono::steady_clock::now();
		const ContentCache saved = VerdictsUsedBy(read, cache);
		const double trim = Milliseconds(std::chrono::steady_clock::now() - trimStart);

		REQUIRE(SaveContentCache(read.Store.Content, saved).HasValue());
		REQUIRE(SavePatchIds(read.Store.Patches, patchIds).HasValue());

		constexpr int kLoads = 5;
		std::vector<double> loads;

		for (int load = 0; load < kLoads; ++load)
		{
			const auto start = std::chrono::steady_clock::now();
			const ContentCache loaded = LoadContentCache(read.Store.Content);
			loads.push_back(Milliseconds(std::chrono::steady_clock::now() - start));
			CHECK(loaded.Verdicts.size() == saved.Verdicts.size());
		}

		const auto patchStart = std::chrono::steady_clock::now();
		Core::PatchIdCache loadedIds;
		LoadPatchIds(read.Store.Patches, loadedIds);
		const double patchLoad = Milliseconds(std::chrono::steady_clock::now() - patchStart);

		PortAskedNothing port;
		ContentModel model{port, DropJob};
		const Core::Ref<const LineRead> followed = Core::CreateRef<const LineRead>(read);

		const auto followStart = std::chrono::steady_clock::now();
		model.Follow(followed);
		const double follow = Milliseconds(std::chrono::steady_clock::now() - followStart);

		std::size_t pending = 0;
		std::size_t candidates = 0;

		for (const Core::BranchLine& line : followed->Model.Branches)
		{
			if (!IsContentCandidate(line))
			{
				continue;
			}

			++candidates;

			if (model.KnownMarkOf(line).State == ContentMark::State::Pending)
			{
				++pending;
			}
		}

		MESSAGE("content.json: ", saved.Verdicts.size(), " verdicts, ", saved.Found.size(), " found, ",
				std::filesystem::file_size(read.Store.Content), " bytes; trimmed to the read in ", trim, " ms; parsed in ",
				Percentile(loads, 0.0), " ms best, ", Percentile(loads, 0.5), " ms median of ", kLoads);
		MESSAGE("first Follow of a new model on the UI thread: ", follow, " ms; first frame: ", pending, " of ", candidates,
				" candidates pending (the failed, asked again)");
		MESSAGE("patches.bin: ", loadedIds.GetSize(), " ids, ", std::filesystem::file_size(read.Store.Patches), " bytes, loaded on a worker in ",
				patchLoad, " ms");

		// Every check that answers leaves a verdict, so a file holds about one per candidate: the failed ones are written
		// as not merged to time the most it holds.
		ContentCache full = saved;

		for (const ContentRun& run : runs)
		{
			if (!full.Verdicts.contains(run.Key))
			{
				full.Verdicts[run.Key] = Core::ContentVerdict{.Outcome = Core::ContentOutcome::NotMerged};
			}
		}

		REQUIRE(SaveContentCache(read.Store.Content, full).HasValue());

		std::vector<double> fullLoads;

		for (int load = 0; load < kLoads; ++load)
		{
			const auto start = std::chrono::steady_clock::now();
			const ContentCache loaded = LoadContentCache(read.Store.Content);
			fullLoads.push_back(Milliseconds(std::chrono::steady_clock::now() - start));
			CHECK(loaded.Verdicts.size() == full.Verdicts.size());
		}

		ContentModel fullModel{port, DropJob};
		const auto fullFollowStart = std::chrono::steady_clock::now();
		fullModel.Follow(Core::CreateRef<const LineRead>(read));
		const double fullFollow = Milliseconds(std::chrono::steady_clock::now() - fullFollowStart);

		MESSAGE("content.json as a full clone would hold it: ", full.Verdicts.size(), " verdicts, ",
				std::filesystem::file_size(read.Store.Content), " bytes; parsed in ", Percentile(fullLoads, 0.0), " ms best, ",
				Percentile(fullLoads, 0.5), " ms median of ", kLoads, "; first Follow ", fullFollow, " ms");
	}

	// MatchPatches reads the header of every line commit the branch lacks before its budget applies: a budget of 0 times
	// that walk alone (with git_graph_ahead_behind's), the branch's own patch ids already in the cache.
	void TimeHeaderWalks(const Core::Repository& repository, const LineRead& read, const std::vector<ContentRun>& runs,
						 Core::PatchIdCache& cache)
	{
		constexpr std::size_t kSlowestTimed = 3;

		std::vector<const ContentRun*> chosen;

		if (!runs.empty())
		{
			chosen.push_back(&runs.back()); // the oldest remote tip, last in the order
		}

		std::vector<const ContentRun*> slowest;

		for (const ContentRun& run : runs)
		{
			slowest.push_back(&run);
		}

		std::sort(slowest.begin(), slowest.end(), IsSlower);

		for (std::size_t place = 0; place < std::min(kSlowestTimed, slowest.size()); ++place)
		{
			chosen.push_back(slowest[place]);
		}

		for (const ContentRun* run : chosen)
		{
			const auto start = std::chrono::steady_clock::now();
			const Core::Result<Core::PatchMatch> match = repository.MatchPatches(run->Key.BranchTip, run->Key.LineTip, 0, cache);
			const double elapsed = Milliseconds(std::chrono::steady_clock::now() - start);

			if (!match.HasValue())
			{
				MESSAGE("header walk of ", BranchNameOf(read, run->Branch), " failed: ", match.GetError().Message);
				continue;
			}

			MESSAGE("header walk (budget 0) of ", BranchNameOf(read, run->Branch), " vs ", ComparedLineOf(read, run->Branch), ": ", elapsed,
					" ms, line commits it lacks ", match.GetValue().Behind, ", its own ", match.GetValue().Ahead, "; its check took ",
					run->Milliseconds, " ms");
		}
	}

	// A copy search as the content model runs it: slices of kPatchCommitsPerCopySlice more line commits each, sharing
	// one patch id cache that starts empty, until a copy, the end of the line or the whole budget.
	struct SlicedSearch
	{
		Core::Result<Core::PatchTwinSearch> Answer = Core::PatchTwinSearch{};
		std::size_t Slices = 0;
		double LongestSlice = 0.0;
		double Total = 0.0;
	};

	SlicedSearch SearchInSlices(const Core::Repository& repository, const Core::Oid& commit, const Core::Oid& lineTip)
	{
		SlicedSearch search;
		Core::PatchIdCache cache;
		std::size_t budget = 0;

		while (budget < Core::kPatchCommitsPerCheck)
		{
			budget = std::min(budget + Core::kPatchCommitsPerCopySlice, Core::kPatchCommitsPerCheck);

			const auto start = std::chrono::steady_clock::now();
			search.Answer = repository.FindPatchTwin(commit, lineTip, budget, cache);
			const double elapsed = Milliseconds(std::chrono::steady_clock::now() - start);

			++search.Slices;
			search.LongestSlice = std::max(search.LongestSlice, elapsed);
			search.Total += elapsed;

			if (!search.Answer.HasValue() || !search.Answer.GetValue().MoreToRead)
			{
				break;
			}
		}

		return search;
	}

	// The newest commit with a patch of its own on a base's first-parent chain: what a details pane shows first.
	Core::AncestryGraph::Index NewestChangeOf(const Core::AncestryGraph& graph, const Core::Oid& tip)
	{
		constexpr std::size_t kMostSteps = 1000;

		Core::AncestryGraph::Index commit = graph.Find(tip);

		for (std::size_t step = 0; step < kMostSteps && commit != Core::AncestryGraph::kNoCommit; ++step)
		{
			if (graph.ParentsOf(commit).size() == 1)
			{
				return commit;
			}

			commit = graph.FirstParentOf(commit);
		}

		return Core::AncestryGraph::kNoCommit;
	}

	// What the details ask for a base's newest change: its copy in each base that does not hold it by history. Cold is a
	// first opening with no patch id known; warm has the content run's.
	void TimeTwinSearches(const Core::Repository& repository, const LineRead& read, Core::PatchIdCache& warm)
	{
		constexpr std::size_t kMostColdSearches = 40;

		const Core::AncestryGraph& graph = *read.Graph;
		std::vector<std::vector<bool>> reach;

		for (const Core::LineBase& base : read.Model.Bases)
		{
			const std::array<Core::AncestryGraph::Index, 1> tip{graph.Find(read.Refs[base.Ref].Tip)};
			reach.push_back(tip[0] == Core::AncestryGraph::kNoCommit ? std::vector<bool>(graph.Size(), false) : Core::CommitsReachableFrom(graph, tip));
		}

		double slowestCold = 0.0;
		double slowestSlice = 0.0;
		double slowestWarm = 0.0;
		std::size_t searches = 0;
		std::size_t found = 0;

		for (std::size_t from = 0; from < read.Model.Bases.size(); ++from)
		{
			const Core::AncestryGraph::Index change = NewestChangeOf(graph, read.Refs[read.Model.Bases[from].Ref].Tip);

			if (change == Core::AncestryGraph::kNoCommit)
			{
				continue;
			}

			for (std::size_t in = 0; in < read.Model.Bases.size(); ++in)
			{
				if (in == from || reach[in][change])
				{
					continue;
				}

				const Core::Oid& commit = graph.IdOf(change);
				const Core::Oid& lineTip = read.Refs[read.Model.Bases[in].Ref].Tip;
				std::string cold = "not timed";

				const auto warmStart = std::chrono::steady_clock::now();
				const Core::Result<Core::PatchTwinSearch> twin = repository.FindPatchTwin(commit, lineTip, Core::kPatchCommitsPerCheck, warm);
				const double elapsed = Milliseconds(std::chrono::steady_clock::now() - warmStart);
				slowestWarm = std::max(slowestWarm, elapsed);

				if (searches < kMostColdSearches)
				{
					const SlicedSearch sliced = SearchInSlices(repository, commit, lineTip);
					slowestCold = std::max(slowestCold, sliced.Total);
					slowestSlice = std::max(slowestSlice, sliced.LongestSlice);
					cold = std::to_string(sliced.Total) + " ms in " + std::to_string(sliced.Slices) + " slices, the longest " +
						   std::to_string(sliced.LongestSlice) + " ms";

					// Sliced, the search answers as one search over the whole budget does.
					CHECK(sliced.Answer.HasValue() == twin.HasValue());

					if (sliced.Answer.HasValue() && twin.HasValue())
					{
						CHECK(sliced.Answer.GetValue().Twin == twin.GetValue().Twin);
					}
				}

				++searches;

				std::string answer = "none";

				if (!twin.HasValue())
				{
					answer = twin.GetError().Message;
				}
				else if (twin.GetValue().Twin.has_value())
				{
					answer = twin.GetValue().Twin->ShortHex();
					++found;
				}

				MESSAGE("copy of ", commit.ShortHex(), " (", read.Refs[read.Model.Bases[from].Ref].FullName, ") in ",
						read.Refs[read.Model.Bases[in].Ref].FullName, ": ", answer, ", cold ", cold, ", warm ", elapsed, " ms");
			}
		}

		MESSAGE("copy searches: ", searches, ", copies found ", found, ", slowest cold ", slowestCold, " ms in slices, the longest slice ",
				slowestSlice, " ms (first ", kMostColdSearches, " timed), slowest warm ", slowestWarm, " ms in one search");
	}

	// The steps of one check timed apart, with a cold patch id cache: where a slow check spends its time.
	void ProfileCheck(const Core::Repository& repository, const LineRead& read, std::size_t branch)
	{
		const std::optional<ContentKey> key = ContentKeyOf(read, read.Model.Branches[branch]);

		if (!key.has_value())
		{
			return;
		}

		Core::PatchIdCache cache;

		auto start = std::chrono::steady_clock::now();
		const Core::Result<Core::Oid> mergeBase = repository.MergeBase(key->BranchTip, key->LineTip);
		const double mergeBaseTime = Milliseconds(std::chrono::steady_clock::now() - start);

		if (!mergeBase.HasValue())
		{
			MESSAGE("profile of ", BranchNameOf(read, branch), ": no merge base");
			return;
		}

		start = std::chrono::steady_clock::now();
		const Core::Result<Core::ContentAnswer> merge = repository.MergeChangesNothing(key->LineTip, key->BranchTip);
		const double mergeTime = Milliseconds(std::chrono::steady_clock::now() - start);
		repository.DropObjectsInMemory();

		start = std::chrono::steady_clock::now();
		const Core::Result<Core::PatchMatch> match = repository.MatchPatches(key->BranchTip, key->LineTip, Core::kPatchCommitsPerCheck, cache);
		const double matchTime = Milliseconds(std::chrono::steady_clock::now() - start);

		start = std::chrono::steady_clock::now();
		const Core::Result<std::vector<Core::Oid>> candidates = repository.LineCommitsTouchingBranch(
			key->LineTip, mergeBase.GetValue(), key->BranchTip, Core::kPatchCommitsPerCheck, Core::kLandingTries);
		const double chainTime = Milliseconds(std::chrono::steady_clock::now() - start);

		std::size_t tried = 0;
		start = std::chrono::steady_clock::now();

		if (candidates.HasValue())
		{
			for (const Core::Oid& candidate : candidates.GetValue())
			{
				++tried;
				const Core::Result<Core::ContentAnswer> answer = repository.MergeChangesNothing(candidate, key->BranchTip);

				if (!answer.HasValue() || answer.GetValue() != Core::ContentAnswer::No)
				{
					break;
				}
			}
		}

		const double landingTime = Milliseconds(std::chrono::steady_clock::now() - start);
		repository.DropObjectsInMemory();

		const std::string matchText = match.HasValue() ? std::to_string(match.GetValue().Behind) + " behind, " +
															 std::to_string(match.GetValue().OwnChanges) + " own, " +
															 std::to_string(match.GetValue().InBase) + " twinned" +
															 (match.GetValue().MissingObjects ? ", missing objects" : "") +
															 (match.GetValue().Complete ? "" : ", incomplete")
														   : match.GetError().Message;

		MESSAGE("profile of ", BranchNameOf(read, branch), " vs ", ComparedLineOf(read, branch), ": merge base ", mergeBaseTime, " ms; merge ",
				mergeTime, " ms; patches ", matchTime, " ms (", matchText, "); landing chain ", chainTime, " ms (",
				candidates.HasValue() ? std::to_string(candidates.GetValue().size()) + " candidates" : candidates.GetError().Message,
				"); landing merges ", landingTime, " ms (", tried, " tried)");
	}

	// With GRAPHITE_CONTENT=1: every candidate's check, then what the parked timings of the content model ask.
	// GRAPHITE_CONTENT_PROFILE=<full name>[;<full name>…] times the steps of those branches' checks alone instead.
	void RunContentAcceptance(const std::string& repositoryPath, const LineRead& read)
	{
		const Core::Result<Core::Repository> repository = Core::Repository::Open(Core::FromUtf8(repositoryPath), Core::ObjectWrites::InMemory);
		INFO((repository.HasValue() ? std::string{} : repository.GetError().Message));
		REQUIRE(repository.HasValue());

		if (const std::string profiled = EnvironmentText("GRAPHITE_CONTENT_PROFILE"); !profiled.empty())
		{
			const std::string names = ";" + profiled + ";";

			for (std::size_t branch = 0; branch < read.Model.Branches.size(); ++branch)
			{
				if (names.find(";" + BranchNameOf(read, branch) + ";") != std::string::npos && IsContentCandidate(read.Model.Branches[branch]))
				{
					ProfileCheck(repository.GetValue(), read, branch);
				}
			}

			return;
		}

		std::ofstream out;

		if (const std::string outPath = EnvironmentText("GRAPHITE_CONTENT_OUT"); !outPath.empty())
		{
			out.open(Core::FromUtf8(outPath), std::ios::binary | std::ios::trunc);
			REQUIRE(out.is_open());
		}

		Core::PatchIdCache cache;

		const auto start = std::chrono::steady_clock::now();
		const std::vector<ContentRun> runs = CheckEveryCandidate(repository.GetValue(), read, cache, out.is_open() ? &out : nullptr);
		const double wall = Milliseconds(std::chrono::steady_clock::now() - start);

		ReportContentRuns(read, runs, wall);
		MESSAGE("patch ids known after the run: ", cache.GetSize());

		TimeRecheck(read, runs);
		TimeCacheOpening(read, runs, cache);
		TimeHeaderWalks(repository.GetValue(), read, runs, cache);
		TimeTwinSearches(repository.GetValue(), read, cache);
	}
}

// Skipped by default: it needs a real repository. Run with
//   GRAPHITE_LINES_REPO=<repository> [GRAPHITE_LINES_OUT=<tsv file>] GraphiteTests -tc="Lines: acceptance*" --no-skip
// With GRAPHITE_CONTENT=1 it also checks every candidate for a merge by content and times what the content model asks
// (GRAPHITE_CONTENT_OUT=<tsv file>: one line per check, written as it ends). The repository is opened to keep the
// checks' objects in memory, so nothing is written into it.
TEST_CASE("Lines: acceptance on the repository in GRAPHITE_LINES_REPO" * doctest::skip())
{
	const std::string repositoryPath = EnvironmentText("GRAPHITE_LINES_REPO");
	REQUIRE_MESSAGE(!repositoryPath.empty(), "GRAPHITE_LINES_REPO is not set");

	const Core::Result<Core::Repository> repository = Core::Repository::Open(Core::FromUtf8(repositoryPath));
	INFO((repository.HasValue() ? std::string{} : repository.GetError().Message));
	REQUIRE(repository.HasValue());

	const TempDirectory directory;
	const LineStorePaths store = LineStoreFor(repository.GetValue().CommonDirectory(), directory.GetPath() / "store");
	const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	const auto coldStart = std::chrono::steady_clock::now();
	const Core::Result<LineRead> cold = ReadLines(repository.GetValue(), store, now);
	const double coldMilliseconds = Milliseconds(std::chrono::steady_clock::now() - coldStart);
	INFO((cold.HasValue() ? std::string{} : cold.GetError().Message));
	REQUIRE(cold.HasValue());

	const auto warmStart = std::chrono::steady_clock::now();
	const Core::Result<LineRead> warm = ReadLines(repository.GetValue(), store, now);
	const double warmMilliseconds = Milliseconds(std::chrono::steady_clock::now() - warmStart);
	INFO((warm.HasValue() ? std::string{} : warm.GetError().Message));
	REQUIRE(warm.HasValue());

	const LineRead& read = warm.GetValue();
	const Core::LineModel& model = read.Model;

	MESSAGE("commits read (cold): ", cold.GetValue().CommitsRead, ", refs: ", read.Refs.size());
	MESSAGE("bases after the cold read: ", cold.GetValue().Model.Bases.size(), ", kept after it: ", cold.GetValue().Answers.KeptBases.size(),
			", kept after the warm read: ", read.Answers.KeptBases.size());
	MESSAGE("cold read ", coldMilliseconds, " ms (reads the whole graph), warm read ", warmMilliseconds,
			" ms (cached graph and the model), commits read warm: ", read.CommitsRead);

	for (const Core::LineBase& base : model.Bases)
	{
		MESSAGE("base ", read.Refs[base.Ref].FullName, " (", std::string{ReasonText(base.Reason)}, std::string{base.Active ? ", active)" : ", quiet)"},
				" merged ", base.Qualities.MergedLines, " mergedAuthors ", base.Qualities.MergedAuthors, " authors ", base.Qualities.Authors,
				" days ", base.Qualities.Span / 86400, " commits ", base.Qualities.Commits);
	}

	for (const Core::OldCopy& copy : model.OldCopies)
	{
		MESSAGE("old copy ", read.Refs[copy.Ref].FullName, " of ", read.Refs[copy.Of].FullName);
	}

	MESSAGE("MainLineGuessed: ", std::string{model.MainLineGuessed ? "yes" : "no"});

	std::map<std::string, std::size_t> perKind;
	std::map<std::string, std::size_t> perState;
	for (const Core::BranchLine& line : model.Branches)
	{
		++perKind[KindText(line.Kind)];
		++perState[StateText(line.State)];
	}

	for (const auto& [kind, count] : perKind)
	{
		MESSAGE("kind ", kind, ": ", count);
	}

	for (const auto& [state, count] : perState)
	{
		MESSAGE("state ", state, ": ", count);
	}

	if (const std::string outPath = EnvironmentText("GRAPHITE_LINES_OUT"); !outPath.empty())
	{
		std::ofstream out{Core::FromUtf8(outPath), std::ios::binary | std::ios::trunc};
		REQUIRE(out.is_open());

		for (const Core::BranchLine& line : model.Branches)
		{
			const std::string comparedWith = line.ComparedWith.has_value() ? read.Refs[*line.ComparedWith].FullName : std::string{};
			out << read.Refs[line.Ref].FullName << '\t' << comparedWith << '\t' << KindText(line.Kind) << '\t' << StateText(line.State)
				<< '\t' << line.Ahead << '\n';
		}
	}

	if (EnvironmentText("GRAPHITE_CONTENT") == "1")
	{
		RunContentAcceptance(repositoryPath, read);
	}
}
