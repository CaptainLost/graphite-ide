#include <doctest/doctest.h>

#include "Git/Content/ContentModel.h"

#include "Git/Content/ContentStore.h"
#include "Git/Content/PatchIdFile.h"
#include "Git/Lines/LineReadFixtures.h"
#include "Git/Lines/LineStore.h"

#include "DeferredJobs.h"
#include "TempDirectory.h"

#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Lines/LineQuestions.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace Graphite;

namespace {

	Core::Oid IdOf(std::uint8_t first, std::uint8_t second = 0)
	{
		Core::Oid id;
		id.Bytes[0] = first;
		id.Bytes[1] = second;
		return id;
	}

	// A tip of its own for each ref, however many refs there are.
	Core::Oid TipOfRef(std::size_t ref)
	{
		Core::Oid id;
		id.Bytes[0] = 0xA0;
		id.Bytes[1] = static_cast<std::uint8_t>(ref >> 8);
		id.Bytes[2] = static_cast<std::uint8_t>(ref & 0xFF);
		return id;
	}

	const Core::Oid kLanding = IdOf(0xB1);
	const Core::Oid kOwn = IdOf(0xB2);
	const Core::Oid kCopy = IdOf(0xB3);

	struct TwinAsk
	{
		Core::Oid Commit;
		Core::Oid LineTip;

		friend bool operator==(const TwinAsk&, const TwinAsk&) = default;
	};

	// Answers each check from the verdicts and failures it is given, NotMerged otherwise, and reads one patch id of the
	// repository asked about into the cache it is handed, as a check reads the patches it compares. A pair or commit in
	// RefuseOnce is refused the first time, as the port refuses a question for a repository it no longer opens.
	class FakeContentPort final : public ContentPort
	{
	public:
		static Core::Oid PatchedCommitOf(std::uint64_t repository) { return IdOf(0xC0, static_cast<std::uint8_t>(repository)); }
		static Core::Oid PatchIdOf(std::uint64_t repository) { return IdOf(0xD0, static_cast<std::uint8_t>(repository)); }

		std::uint64_t RepositoryNow() override { return Repository; }

		Core::Result<Core::ContentVerdict> CheckContent(std::uint64_t repository, const Core::Oid& branchTip, const Core::Oid& lineTip,
														Core::PatchIdCache& patchIds) override
		{
			const ContentKey key{branchTip, lineTip};
			Checked.push_back(key);

			if (RefuseOnce.erase(key) > 0)
			{
				return Refused();
			}

			patchIds.Store(PatchedCommitOf(repository), PatchIdOf(repository));

			const std::map<ContentKey, Core::Error>::const_iterator failure = Failures.find(key);

			if (failure != Failures.end())
			{
				return failure->second;
			}

			const std::map<ContentKey, Core::ContentVerdict>::const_iterator verdict = Verdicts.find(key);

			if (verdict != Verdicts.end())
			{
				return verdict->second;
			}

			return Core::ContentVerdict{};
		}

		Core::Result<std::optional<Core::Oid>> FindTwin(std::uint64_t repository, const Core::Oid& commit, const Core::Oid& lineTip,
														Core::PatchIdCache& patchIds) override
		{
			TwinsSearched.push_back(TwinAsk{commit, lineTip});

			if (RefuseTwinOnce.erase(commit) > 0)
			{
				return Refused();
			}

			patchIds.Store(PatchedCommitOf(repository), PatchIdOf(repository));

			if (MissingObjectsOf.contains(commit))
			{
				return Core::Error{Core::ErrorCode::NotFound, "an object the patch needs is missing"};
			}

			const std::map<Core::Oid, Core::Oid>::const_iterator twin = TwinOf.find(commit);

			if (twin != TwinOf.end())
			{
				return std::optional<Core::Oid>{twin->second};
			}

			return std::optional<Core::Oid>{};
		}

		static Core::Error Refused()
		{
			return Core::Error{Core::ErrorCode::Cancelled, "the folder changed since this was asked"};
		}

		std::uint64_t Repository = 1;
		std::map<ContentKey, Core::ContentVerdict> Verdicts;
		std::map<ContentKey, Core::Error> Failures;
		std::set<ContentKey> RefuseOnce;
		std::map<Core::Oid, Core::Oid> TwinOf;
		std::set<Core::Oid> MissingObjectsOf;
		std::set<Core::Oid> RefuseTwinOnce;

		std::vector<ContentKey> Checked;
		std::vector<TwinAsk> TwinsSearched;
	};

	// Holds submitted jobs and runs the newest first, as two workers can when the older one is slow to start.
	class NewestFirstJobs
	{
	public:
		Core::JobSubmitter Submitter()
		{
			return [this](std::function<void()> job) { m_Jobs.push_back(std::move(job)); };
		}

		void RunNewest()
		{
			REQUIRE_FALSE(m_Jobs.empty());
			std::function<void()> job = std::move(m_Jobs.back());
			m_Jobs.pop_back();
			job();
		}

		void RunAllNewestFirst()
		{
			while (!m_Jobs.empty())
			{
				RunNewest();
			}
		}

	private:
		std::vector<std::function<void()>> m_Jobs;
	};

	// Collects what the model would write to the log.
	struct ReportedFailures
	{
		void Report(const std::string& message) { Messages.push_back(message); }

		std::vector<std::string> Messages;
	};

	// origin/main, the line, then `count` local branches compared with it, each with a commit of its own, the first the
	// newest.
	LineRead CandidatesRead(std::size_t count, const LineStorePaths& store = {})
	{
		Fixtures::LineReadBuilder builder;
		builder.AddRemote("origin");
		const std::size_t line = builder.AddRemoteBranch("origin", "main", {.AgeDays = 0});
		builder.MakeBase(line);

		for (std::size_t branch = 0; branch < count; ++branch)
		{
			const std::size_t ref = builder.AddLocal("topic-" + std::to_string(branch), {.AgeDays = static_cast<int>(branch) + 1});
			builder.Compare(ref, line);
		}

		LineRead read = builder.Build();

		for (std::size_t ref = 0; ref < read.Refs.size(); ++ref)
		{
			read.Refs[ref].Tip = TipOfRef(ref);
		}

		for (Core::BranchLine& candidate : read.Model.Branches)
		{
			candidate.Ahead = 1;
		}

		read.Store = store;
		return read;
	}

	Core::Ref<const LineRead> Shared(LineRead read)
	{
		return Core::CreateRef<const LineRead>(std::move(read));
	}

	// The pair the check of read.Model.Branches[branch] asks about.
	ContentKey KeyOfBranch(const LineRead& read, std::size_t branch)
	{
		const Core::BranchLine& line = read.Model.Branches[branch];
		return ContentKey{read.Refs[line.Ref].Tip, read.Refs[*line.ComparedWith].Tip};
	}

	LineStorePaths StoreIn(const std::filesystem::path& folder)
	{
		return LineStorePaths{.Answers = folder / "answers.json",
							  .Graph = folder / "graph.bin",
							  .Content = folder / "content.json",
							  .Patches = folder / "patches.bin"};
	}

	Core::ContentVerdict SquashLandingAt(const Core::Oid& landing)
	{
		return Core::ContentVerdict{.Outcome = Core::ContentOutcome::Merged, .Kind = Core::ContentKind::Squash, .Landing = landing};
	}

	Core::ContentVerdict RebaseCopying(const Core::Oid& own, const Core::Oid& copy)
	{
		return Core::ContentVerdict{.Outcome = Core::ContentOutcome::Merged,
									.Kind = Core::ContentKind::Rebase,
									.Twins = {Core::PatchTwin{.Own = own, .Twin = copy}}};
	}

	Core::ContentVerdict UnknownVerdict()
	{
		return Core::ContentVerdict{.Outcome = Core::ContentOutcome::Unknown};
	}

	// Every job run and every answer landed, as frames do while the window stays on screen, until nothing more is asked.
	void Settle(DeferredJobs& jobs, ContentModel& model)
	{
		model.OnUpdate(Core::Timestep{});

		while (jobs.Count() > 0)
		{
			jobs.RunAll();
			model.OnUpdate(Core::Timestep{});
		}
	}

	// Lets the time between saves pass, and runs the save that starts.
	void LetSaveRun(DeferredJobs& jobs, ContentModel& model)
	{
		model.OnUpdate(Core::Timestep{ContentModel::kSecondsBetweenSaves});
		jobs.RunAll();
	}

	// Asked in one frame, searched, and asked again in the frame its answer lands.
	TwinMark TwinAfterSearch(DeferredJobs& jobs, ContentModel& model, const Core::Oid& commit, const Core::Oid& lineTip)
	{
		static_cast<void>(model.TwinIn(commit, lineTip));
		model.OnUpdate(Core::Timestep{});
		jobs.RunAll();
		model.OnUpdate(Core::Timestep{});
		return model.TwinIn(commit, lineTip);
	}

	bool IsKnownAs(const ContentMark& mark, const Core::ContentVerdict& verdict)
	{
		return mark.State == ContentMark::State::Known && mark.Verdict == verdict;
	}

	// root - landing - after the landing: the line moving on past a squash. root - reset: the line reset to before it.
	// root - copy - after the copy: a rebase's copy on the line. root - branch tip: the branch.
	const Core::Oid kRoot = IdOf(0x01);
	const Core::Oid kLandingOnLine = IdOf(0x02);
	const Core::Oid kAfterLanding = IdOf(0x03);
	const Core::Oid kReset = IdOf(0x04);
	const Core::Oid kBranchTip = IdOf(0x05);
	const Core::Oid kCopyOnLine = IdOf(0x06);
	const Core::Oid kAfterCopy = IdOf(0x07);
	const Core::Oid kOtherBranchTip = IdOf(0x08);

	Core::Ref<const Core::AncestryGraph> LandingGraph()
	{
		Core::AncestryGraph graph;
		graph.Append({Core::GraphCommit{.Id = kRoot, .Time = 100},
					  Core::GraphCommit{.Id = kBranchTip, .Parents = {kRoot}, .Time = 150},
					  Core::GraphCommit{.Id = kOtherBranchTip, .Parents = {kRoot}, .Time = 160},
					  Core::GraphCommit{.Id = kLandingOnLine, .Parents = {kRoot}, .Time = 200},
					  Core::GraphCommit{.Id = kCopyOnLine, .Parents = {kRoot}, .Time = 250},
					  Core::GraphCommit{.Id = kAfterLanding, .Parents = {kLandingOnLine}, .Time = 300},
					  Core::GraphCommit{.Id = kAfterCopy, .Parents = {kCopyOnLine}, .Time = 350},
					  Core::GraphCommit{.Id = kReset, .Parents = {kRoot}, .Time = 400}});
		return Core::CreateRef<const Core::AncestryGraph>(std::move(graph));
	}

	// The branch at kBranchTip compared with main at `lineTip`.
	Core::Ref<const LineRead> LandingRead(const Core::Oid& lineTip)
	{
		LineRead read = CandidatesRead(1);
		read.Refs[0].Tip = lineTip;
		read.Refs[1].Tip = kBranchTip;
		read.Graph = LandingGraph();
		read.Generations = Core::CreateRef<const std::vector<std::uint32_t>>(Core::GenerationsOf(*read.Graph));
		return Shared(std::move(read));
	}

	// topic at kBranchTip compared with main, fix at kOtherBranchTip compared with release.
	Core::Ref<const LineRead> TwoLinesRead(const Core::Oid& mainTip, const Core::Oid& releaseTip)
	{
		Fixtures::LineReadBuilder builder;
		builder.AddRemote("origin");
		const std::size_t mainLine = builder.AddRemoteBranch("origin", "main", {.AgeDays = 0});
		const std::size_t releaseLine = builder.AddRemoteBranch("origin", "release", {.AgeDays = 0});
		builder.MakeBase(mainLine);
		builder.MakeBase(releaseLine);
		const std::size_t topic = builder.AddLocal("topic", {.AgeDays = 1});
		const std::size_t fix = builder.AddLocal("fix", {.AgeDays = 2});
		builder.Compare(topic, mainLine);
		builder.Compare(fix, releaseLine);

		LineRead read = builder.Build();
		read.Refs[mainLine].Tip = mainTip;
		read.Refs[releaseLine].Tip = releaseTip;
		read.Refs[topic].Tip = kBranchTip;
		read.Refs[fix].Tip = kOtherBranchTip;

		for (Core::BranchLine& candidate : read.Model.Branches)
		{
			candidate.Ahead = 1;
		}

		read.Graph = LandingGraph();
		read.Generations = Core::CreateRef<const std::vector<std::uint32_t>>(Core::GenerationsOf(*read.Graph));
		return Shared(std::move(read));
	}
}

TEST_CASE("Git window: a branch's mark is pending until its check lands then known")
{
	FakeContentPort port;
	DeferredJobs jobs;
	ContentModel model{port, jobs.Submitter()};

	LineRead built = CandidatesRead(2);
	built.Model.Branches[1].State = Core::BranchState::MergedByHistory;
	const Core::Ref<const LineRead> read = Shared(std::move(built));
	port.Verdicts[KeyOfBranch(*read, 0)] = SquashLandingAt(kLanding);

	model.SetOnScreen(true);
	model.Follow(read);
	const Core::BranchLine& candidate = read->Model.Branches[0];

	CHECK(model.MarkOf(candidate).State == ContentMark::State::Pending);
	CHECK_FALSE(model.MarkOf(candidate).Verdict.has_value());

	model.OnUpdate(Core::Timestep{});
	REQUIRE(jobs.Count() == 1);
	CHECK(model.MarkOf(candidate).State == ContentMark::State::Pending);

	jobs.RunAll();
	CHECK(model.MarkOf(candidate).State == ContentMark::State::Pending);

	model.OnUpdate(Core::Timestep{});
	CHECK(IsKnownAs(model.MarkOf(candidate), SquashLandingAt(kLanding)));
	CHECK(IsKnownAs(model.KnownMarkOf(candidate), SquashLandingAt(kLanding)));

	// A branch history already shows merged is not asked about.
	CHECK(IsKnownAs(model.KnownMarkOf(read->Model.Branches[1]), Core::ContentVerdict{}));
	CHECK(port.Checked.size() == 1);
}

TEST_CASE("Git window: one content check runs at a time")
{
	FakeContentPort port;
	DeferredJobs jobs;
	ContentModel model{port, jobs.Submitter()};

	const Core::Ref<const LineRead> read = Shared(CandidatesRead(5));
	model.SetOnScreen(true);
	model.Follow(read);

	model.OnUpdate(Core::Timestep{});
	model.OnUpdate(Core::Timestep{});
	model.OnUpdate(Core::Timestep{});
	CHECK(jobs.Count() == 1);

	// A copy asked for in every frame, as the details ask while a commit is selected, waits its turn too.
	std::size_t jobsRun = 0;

	while (jobs.Count() > 0)
	{
		CHECK(jobs.Count() == 1);
		jobs.RunAll();
		++jobsRun;

		static_cast<void>(model.TwinIn(kOwn, TipOfRef(0)));
		model.OnUpdate(Core::Timestep{});
	}

	CHECK(jobsRun == 6);
	CHECK(port.Checked.size() == 5);
	CHECK(port.TwinsSearched.size() == 1);
}

TEST_CASE("Git window: nothing is checked while the window is off screen")
{
	FakeContentPort port;
	DeferredJobs jobs;
	ContentModel model{port, jobs.Submitter()};

	const Core::Ref<const LineRead> read = Shared(CandidatesRead(3));
	model.Follow(read);

	for (int frame = 0; frame < 3; ++frame)
	{
		CHECK(model.MarkOf(read->Model.Branches[0]).State == ContentMark::State::Pending);
		CHECK(model.TwinIn(kOwn, TipOfRef(0)).State == ContentMark::State::Pending);
		model.OnUpdate(Core::Timestep{1.0f});
	}

	CHECK(jobs.Count() == 0);
	CHECK(port.Checked.empty());
	CHECK(port.TwinsSearched.empty());

	model.SetOnScreen(true);
	model.OnUpdate(Core::Timestep{});
	CHECK(jobs.Count() == 1);

	// The check running as the window leaves the screen lands; nothing more starts.
	model.SetOnScreen(false);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{});
	model.OnUpdate(Core::Timestep{});

	CHECK(jobs.Count() == 0);
	CHECK(port.Checked.size() == 1);
	CHECK(model.KnownMarkOf(read->Model.Branches[0]).State == ContentMark::State::Known);
}

TEST_CASE("Git window: the shown branch is checked before a thousand waiting branches")
{
	FakeContentPort port;
	DeferredJobs jobs;
	ContentModel model{port, jobs.Submitter()};

	constexpr std::size_t kWaiting = 1000;
	const Core::Ref<const LineRead> read = Shared(CandidatesRead(kWaiting + 1));
	model.SetOnScreen(true);
	model.Follow(read);

	// The oldest branch, last in the order of the rest.
	const std::size_t shown = kWaiting;
	CHECK(model.MarkOfShown(read->Model.Branches[shown]).State == ContentMark::State::Pending);

	model.OnUpdate(Core::Timestep{});
	jobs.RunAll();
	REQUIRE(port.Checked.size() == 1);
	CHECK(port.Checked[0] == KeyOfBranch(*read, shown));

	// The line moves on, so every branch waits again. The shown branch was asked against the read before, in the frame
	// before the new read is followed; it is still the first checked.
	LineRead movedOn = CandidatesRead(kWaiting + 1);
	movedOn.Refs[0].Tip = IdOf(0xEF);
	const Core::Ref<const LineRead> next = Shared(std::move(movedOn));

	static_cast<void>(model.MarkOfShown(read->Model.Branches[shown]));
	model.Follow(next);
	model.OnUpdate(Core::Timestep{});
	jobs.RunAll();

	REQUIRE(port.Checked.size() == 2);
	CHECK(port.Checked[1] == KeyOfBranch(*next, shown));
}

TEST_CASE("Git window: rows in view are checked before the branches out of view")
{
	FakeContentPort port;
	DeferredJobs jobs;
	ContentModel model{port, jobs.Submitter()};

	const Core::Ref<const LineRead> read = Shared(CandidatesRead(100));
	model.SetOnScreen(true);
	model.Follow(read);

	static_cast<void>(model.MarkOf(read->Model.Branches[70]));
	static_cast<void>(model.MarkOf(read->Model.Branches[60]));
	model.OnUpdate(Core::Timestep{});
	jobs.RunAll();

	static_cast<void>(model.MarkOf(read->Model.Branches[70]));
	static_cast<void>(model.MarkOf(read->Model.Branches[60]));
	model.OnUpdate(Core::Timestep{});
	jobs.RunAll();

	// Out of view again, the order of the rest comes back.
	model.OnUpdate(Core::Timestep{});
	jobs.RunAll();

	REQUIRE(port.Checked.size() == 3);
	CHECK(port.Checked[0] == KeyOfBranch(*read, 60));
	CHECK(port.Checked[1] == KeyOfBranch(*read, 70));
	CHECK(port.Checked[2] == KeyOfBranch(*read, 0));

	// A row drawn in the frame before a new read is followed is still checked first.
	LineRead movedOn = CandidatesRead(100);
	movedOn.Refs[0].Tip = IdOf(0xEF);
	const Core::Ref<const LineRead> next = Shared(std::move(movedOn));

	static_cast<void>(model.MarkOf(read->Model.Branches[80]));
	model.Follow(next);
	model.OnUpdate(Core::Timestep{});
	jobs.RunAll();

	REQUIRE(port.Checked.size() == 4);
	CHECK(port.Checked[3] == KeyOfBranch(*next, 80));
}

TEST_CASE("Git window: a verdict found once counts while the line holds its landing")
{
	FakeContentPort port;
	DeferredJobs jobs;
	ContentModel model{port, jobs.Submitter()};
	model.SetOnScreen(true);

	port.Verdicts[ContentKey{kBranchTip, kLandingOnLine}] = SquashLandingAt(kLandingOnLine);
	model.Follow(LandingRead(kLandingOnLine));
	Settle(jobs, model);
	REQUIRE(port.Checked.size() == 1);

	// The line moves on; merging the branch into its new tip would be asked again, and the port would say no.
	const Core::Ref<const LineRead> movedOn = LandingRead(kAfterLanding);
	model.Follow(movedOn);
	Settle(jobs, model);

	CHECK(port.Checked.size() == 1);
	CHECK(IsKnownAs(model.KnownMarkOf(movedOn->Model.Branches[0]), SquashLandingAt(kLandingOnLine)));
}

TEST_CASE("Git window: a verdict found once is checked again when the line no longer holds its landing")
{
	FakeContentPort port;
	DeferredJobs jobs;
	ContentModel model{port, jobs.Submitter()};
	model.SetOnScreen(true);

	port.Verdicts[ContentKey{kBranchTip, kLandingOnLine}] = SquashLandingAt(kLandingOnLine);
	model.Follow(LandingRead(kLandingOnLine));
	Settle(jobs, model);

	const Core::Ref<const LineRead> reset = LandingRead(kReset);
	model.Follow(reset);
	Settle(jobs, model);

	REQUIRE(port.Checked.size() == 2);
	CHECK(port.Checked[1] == ContentKey{kBranchTip, kReset});
	CHECK(IsKnownAs(model.KnownMarkOf(reset->Model.Branches[0]), Core::ContentVerdict{}));
}

TEST_CASE("Git window: a rebase found once counts while the line holds every copy")
{
	FakeContentPort port;
	DeferredJobs jobs;
	ContentModel model{port, jobs.Submitter()};
	model.SetOnScreen(true);

	port.Verdicts[ContentKey{kBranchTip, kCopyOnLine}] = RebaseCopying(kBranchTip, kCopyOnLine);
	model.Follow(LandingRead(kCopyOnLine));
	Settle(jobs, model);

	const Core::Ref<const LineRead> movedOn = LandingRead(kAfterCopy);
	model.Follow(movedOn);
	Settle(jobs, model);

	CHECK(port.Checked.size() == 1);
	CHECK(IsKnownAs(model.KnownMarkOf(movedOn->Model.Branches[0]), RebaseCopying(kBranchTip, kCopyOnLine)));

	const Core::Ref<const LineRead> withoutCopy = LandingRead(kAfterLanding);
	model.Follow(withoutCopy);
	Settle(jobs, model);

	CHECK(port.Checked.size() == 2);
}

TEST_CASE("Git window: a check that lands after a folder switch is dropped and saves nothing")
{
	TempDirectory directory;
	const LineStorePaths before = StoreIn(directory.GetPath() / "before");
	const LineStorePaths after = StoreIn(directory.GetPath() / "after");

	FakeContentPort port;
	DeferredJobs jobs;
	ContentModel model{port, jobs.Submitter()};
	model.SetOnScreen(true);

	const Core::Ref<const LineRead> beforeRead = Shared(CandidatesRead(1, before));
	const ContentKey key = KeyOfBranch(*beforeRead, 0);
	port.Verdicts[key] = SquashLandingAt(kLanding);

	model.Follow(beforeRead);
	model.OnUpdate(Core::Timestep{});
	REQUIRE(jobs.Count() == 1);

	// The check answers while the folder is open, and the folder switches before the answer lands.
	jobs.RunAll();
	model.Clear();
	CHECK(jobs.Count() == 0);

	port.Repository = 2;
	port.Verdicts.clear();

	// The next folder's read names the same commits, so a verdict that leaked would show.
	const Core::Ref<const LineRead> afterRead = Shared(CandidatesRead(1, after));
	model.Follow(afterRead);
	model.OnUpdate(Core::Timestep{});
	CHECK(model.KnownMarkOf(afterRead->Model.Branches[0]).State == ContentMark::State::Pending);

	Settle(jobs, model);
	CHECK(IsKnownAs(model.KnownMarkOf(afterRead->Model.Branches[0]), Core::ContentVerdict{}));
	CHECK(port.Checked.size() == 2);

	LetSaveRun(jobs, model);
	CHECK_FALSE(std::filesystem::exists(before.Content));
	CHECK_FALSE(std::filesystem::exists(before.Patches));

	const ContentCache saved = LoadContentCache(after.Content);
	REQUIRE(saved.Verdicts.contains(key));
	CHECK(saved.Verdicts.at(key).Outcome == Core::ContentOutcome::NotMerged);
	CHECK(saved.Found.empty());

	Core::PatchIdCache savedIds;
	LoadPatchIds(after.Patches, savedIds);
	CHECK(savedIds.Find(FakeContentPort::PatchedCommitOf(2)).has_value());
	CHECK_FALSE(savedIds.Find(FakeContentPort::PatchedCommitOf(1)).has_value());
}

TEST_CASE("Git window: a check asked before a folder switch and run after it asks nothing")
{
	FakeContentPort port;
	DeferredJobs jobs;
	ContentModel model{port, jobs.Submitter()};
	model.SetOnScreen(true);

	model.Follow(Shared(CandidatesRead(1)));
	model.OnUpdate(Core::Timestep{});
	REQUIRE(jobs.Count() == 1);

	model.Clear();
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{});

	CHECK(port.Checked.empty());
}

TEST_CASE("Git window: an unknown answer is saved and known in the next opening while a failed check is asked again")
{
	TempDirectory directory;
	const LineStorePaths store = StoreIn(directory.GetPath());

	FakeContentPort port;
	DeferredJobs jobs;
	ContentModel model{port, jobs.Submitter()};
	model.SetOnScreen(true);

	ReportedFailures reported;
	model.LogFailure = [&reported](const std::string& message) { reported.Report(message); };

	const Core::Ref<const LineRead> read = Shared(CandidatesRead(3, store));
	port.Verdicts[KeyOfBranch(*read, 0)] = UnknownVerdict();
	port.Failures[KeyOfBranch(*read, 1)] = Core::Error{Core::ErrorCode::IoFailure, "the object database cannot be read"};

	model.Follow(read);
	Settle(jobs, model);

	// The failure is logged, once; an unknown answer is not a failure.
	REQUIRE(reported.Messages.size() == 1);
	CHECK(reported.Messages[0].find("the object database cannot be read") != std::string::npos);

	const ContentMark unknown = model.KnownMarkOf(read->Model.Branches[0]);
	CHECK(unknown.State == ContentMark::State::Unknown);
	CHECK_FALSE(unknown.Verdict.has_value());
	CHECK(model.KnownMarkOf(read->Model.Branches[1]).State == ContentMark::State::Unknown);
	CHECK(IsKnownAs(model.KnownMarkOf(read->Model.Branches[2]), Core::ContentVerdict{}));

	// Neither is asked again while the folder stays open.
	Settle(jobs, model);
	CHECK(port.Checked.size() == 3);

	// The unknown answer is saved with the other; the failure is not.
	LetSaveRun(jobs, model);
	const ContentCache saved = LoadContentCache(store.Content);
	CHECK(saved.Verdicts.size() == 2);
	CHECK(saved.Verdicts.contains(KeyOfBranch(*read, 0)));
	CHECK_FALSE(saved.Verdicts.contains(KeyOfBranch(*read, 1)));
	CHECK(saved.Verdicts.contains(KeyOfBranch(*read, 2)));

	// The next session knows the unknown answer in its first frame and asks only the failed check again.
	ContentModel next{port, jobs.Submitter()};
	next.LogFailure = [&reported](const std::string& message) { reported.Report(message); };
	next.SetOnScreen(true);
	next.Follow(read);
	CHECK(next.KnownMarkOf(read->Model.Branches[0]).State == ContentMark::State::Unknown);
	CHECK(next.KnownMarkOf(read->Model.Branches[1]).State == ContentMark::State::Pending);
	Settle(jobs, next);
	CHECK(port.Checked.size() == 4);
	CHECK(reported.Messages.size() == 2);
}

TEST_CASE("Git window: a commit that is a squash's landing names the branch it came from")
{
	FakeContentPort port;
	DeferredJobs jobs;
	ContentModel model{port, jobs.Submitter()};
	model.SetOnScreen(true);

	const Core::Ref<const LineRead> read = Shared(CandidatesRead(2));
	port.Verdicts[KeyOfBranch(*read, 0)] = SquashLandingAt(kLanding);
	port.Verdicts[KeyOfBranch(*read, 1)] = RebaseCopying(kOwn, kCopy);
	model.Follow(read);
	Settle(jobs, model);

	const std::optional<ContentSource> landing = model.SourceOf(kLanding);
	REQUIRE(landing.has_value());
	CHECK(*landing == ContentSource{.Branch = "refs/heads/topic-0", .Kind = Core::ContentKind::Squash, .Own = std::nullopt});

	const std::optional<ContentSource> copy = model.SourceOf(kCopy);
	REQUIRE(copy.has_value());
	CHECK(*copy == ContentSource{.Branch = "refs/heads/topic-1", .Kind = Core::ContentKind::Rebase, .Own = kOwn});

	CHECK_FALSE(model.SourceOf(kOwn).has_value());

	// Once the branch has moved on, the read no longer holds the branch the landing came from.
	LineRead movedOn = CandidatesRead(2);
	movedOn.Refs[read->Model.Branches[0].Ref].Tip = IdOf(0xEE);
	model.Follow(Shared(std::move(movedOn)));
	Settle(jobs, model);

	CHECK_FALSE(model.SourceOf(kLanding).has_value());
	CHECK(model.SourceOf(kCopy).has_value());
}

TEST_CASE("Git window: verdicts are saved at most once every five seconds")
{
	TempDirectory directory;
	const LineStorePaths store = StoreIn(directory.GetPath());

	FakeContentPort port;
	DeferredJobs jobs;
	ContentModel model{port, jobs.Submitter()};

	const Core::Ref<const LineRead> read = Shared(CandidatesRead(2, store));
	model.Follow(read);

	// One check runs, and lands a second later.
	model.SetOnScreen(true);
	model.OnUpdate(Core::Timestep{});
	model.SetOnScreen(false);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{1.0f});
	jobs.RunAll();
	CHECK_FALSE(std::filesystem::exists(store.Content));

	model.OnUpdate(Core::Timestep{4.0f});
	jobs.RunAll();
	CHECK(LoadContentCache(store.Content).Verdicts.size() == 1);

	// The next lands a second after that save; it waits for five seconds to pass since.
	model.SetOnScreen(true);
	model.OnUpdate(Core::Timestep{});
	model.SetOnScreen(false);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{1.0f});
	jobs.RunAll();
	CHECK(LoadContentCache(store.Content).Verdicts.size() == 1);

	model.OnUpdate(Core::Timestep{4.0f});
	jobs.RunAll();
	CHECK(LoadContentCache(store.Content).Verdicts.size() == 2);
}

TEST_CASE("Git window: verdicts saved on a folder switch are known in the next opening's first frame")
{
	TempDirectory directory;
	const LineStorePaths store = StoreIn(directory.GetPath());

	FakeContentPort port;
	DeferredJobs jobs;
	const Core::Ref<const LineRead> read = Shared(CandidatesRead(1, store));
	port.Verdicts[KeyOfBranch(*read, 0)] = SquashLandingAt(kLanding);

	{
		ContentModel model{port, jobs.Submitter()};
		model.SetOnScreen(true);
		model.Follow(read);
		Settle(jobs, model);

		model.Clear();
		jobs.RunAll();
	}

	ContentModel next{port, jobs.Submitter()};
	next.Follow(read);

	CHECK(IsKnownAs(next.KnownMarkOf(read->Model.Branches[0]), SquashLandingAt(kLanding)));

	next.SetOnScreen(true);
	next.OnUpdate(Core::Timestep{});
	CHECK(jobs.Count() == 0);
	CHECK(port.Checked.size() == 1);

	Core::PatchIdCache savedIds;
	LoadPatchIds(store.Patches, savedIds);
	CHECK(savedIds.Find(FakeContentPort::PatchedCommitOf(1)).has_value());
}

TEST_CASE("Git window: a commit's copy on a line is searched once and kept")
{
	FakeContentPort port;
	DeferredJobs jobs;
	ContentModel model{port, jobs.Submitter()};
	model.SetOnScreen(true);
	model.Follow(Shared(CandidatesRead(0)));

	const Core::Oid lineTip = TipOfRef(0);
	const Core::Oid withoutCopy = IdOf(0xB4);
	const Core::Oid missing = IdOf(0xB5);
	port.TwinOf[kOwn] = kCopy;
	port.MissingObjectsOf.insert(missing);

	CHECK(model.TwinIn(kOwn, lineTip).State == ContentMark::State::Pending);

	const TwinMark found = TwinAfterSearch(jobs, model, kOwn, lineTip);
	CHECK(found.State == ContentMark::State::Known);
	CHECK(found.Twin == kCopy);

	const TwinMark none = TwinAfterSearch(jobs, model, withoutCopy, lineTip);
	CHECK(none.State == ContentMark::State::Known);
	CHECK_FALSE(none.Twin.has_value());

	const TwinMark unknown = TwinAfterSearch(jobs, model, missing, lineTip);
	CHECK(unknown.State == ContentMark::State::Unknown);
	CHECK_FALSE(unknown.Twin.has_value());

	static_cast<void>(TwinAfterSearch(jobs, model, kOwn, lineTip));
	CHECK(port.TwinsSearched.size() == 3);
}

TEST_CASE("Git window: a check the port refuses for a repository it no longer opens is asked again")
{
	FakeContentPort port;
	DeferredJobs jobs;
	ContentModel model{port, jobs.Submitter()};
	ReportedFailures reported;
	model.LogFailure = [&reported](const std::string& message) { reported.Report(message); };
	model.SetOnScreen(true);

	const Core::Ref<const LineRead> read = Shared(CandidatesRead(1));
	port.Verdicts[KeyOfBranch(*read, 0)] = SquashLandingAt(kLanding);
	port.RefuseOnce.insert(KeyOfBranch(*read, 0));
	model.Follow(read);

	model.OnUpdate(Core::Timestep{});
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{});
	CHECK(model.KnownMarkOf(read->Model.Branches[0]).State == ContentMark::State::Pending);

	Settle(jobs, model);
	CHECK(port.Checked.size() == 2);
	CHECK(IsKnownAs(model.KnownMarkOf(read->Model.Branches[0]), SquashLandingAt(kLanding)));

	const Core::Oid lineTip = TipOfRef(0);
	port.TwinOf[kOwn] = kCopy;
	port.RefuseTwinOnce.insert(kOwn);

	CHECK(TwinAfterSearch(jobs, model, kOwn, lineTip).State == ContentMark::State::Pending);

	const TwinMark found = TwinAfterSearch(jobs, model, kOwn, lineTip);
	CHECK(found.State == ContentMark::State::Known);
	CHECK(found.Twin == kCopy);
	CHECK(port.TwinsSearched.size() == 2);

	CHECK(reported.Messages.empty());
}

TEST_CASE("Git window: the next folder's first check waits for the folder before's check to finish")
{
	FakeContentPort port;
	DeferredJobs jobs;
	ContentModel model{port, jobs.Submitter()};
	model.SetOnScreen(true);

	model.Follow(Shared(CandidatesRead(1)));
	model.OnUpdate(Core::Timestep{});
	REQUIRE(jobs.Count() == 1);

	model.Clear();
	model.Follow(Shared(CandidatesRead(2)));
	model.OnUpdate(Core::Timestep{});
	model.OnUpdate(Core::Timestep{});
	CHECK(jobs.Count() == 1);

	// The folder before's check finds its folder gone and asks nothing; then the next folder's first one starts.
	jobs.RunAll();
	CHECK(port.Checked.empty());

	model.OnUpdate(Core::Timestep{});
	CHECK(jobs.Count() == 1);
	jobs.RunAll();
	CHECK(port.Checked.size() == 1);
}

TEST_CASE("Git window: a save of one folder is not skipped for a later save of another folder")
{
	TempDirectory directory;
	const LineStorePaths first = StoreIn(directory.GetPath() / "first");
	const LineStorePaths second = StoreIn(directory.GetPath() / "second");

	FakeContentPort port;
	NewestFirstJobs jobs;
	ContentModel model{port, jobs.Submitter()};
	model.SetOnScreen(true);

	const Core::Ref<const LineRead> firstRead = Shared(CandidatesRead(1, first));
	model.Follow(firstRead);
	model.OnUpdate(Core::Timestep{});
	jobs.RunNewest();
	model.OnUpdate(Core::Timestep{});
	model.Clear();

	// The first folder's save waits while the second folder's check runs, lands and is saved.
	const Core::Ref<const LineRead> secondRead = Shared(CandidatesRead(2, second));
	model.Follow(secondRead);
	model.OnUpdate(Core::Timestep{});
	jobs.RunNewest();
	model.OnUpdate(Core::Timestep{});
	model.Clear();

	jobs.RunAllNewestFirst();

	CHECK(LoadContentCache(second.Content).Verdicts.contains(KeyOfBranch(*secondRead, 0)));
	CHECK(LoadContentCache(first.Content).Verdicts.contains(KeyOfBranch(*firstRead, 0)));
}

TEST_CASE("Git window: a save asked earlier never writes over a later one of the same store")
{
	TempDirectory directory;
	const LineStorePaths store = StoreIn(directory.GetPath());

	FakeContentPort port;
	NewestFirstJobs jobs;
	ContentModel model{port, jobs.Submitter()};

	const Core::Ref<const LineRead> read = Shared(CandidatesRead(2, store));
	model.Follow(read);

	for (int checkedBranches = 1; checkedBranches <= 2; ++checkedBranches)
	{
		model.SetOnScreen(true);
		model.OnUpdate(Core::Timestep{});
		model.SetOnScreen(false);
		jobs.RunNewest();
		model.OnUpdate(Core::Timestep{ContentModel::kSecondsBetweenSaves});
	}

	jobs.RunAllNewestFirst();
	CHECK(LoadContentCache(store.Content).Verdicts.size() == 2);
}

TEST_CASE("Git window: remembered verdicts are rechecked against each branch's own line tip")
{
	FakeContentPort port;
	DeferredJobs jobs;
	ContentModel model{port, jobs.Submitter()};
	model.SetOnScreen(true);

	port.Verdicts[ContentKey{kBranchTip, kLandingOnLine}] = SquashLandingAt(kLandingOnLine);
	port.Verdicts[ContentKey{kOtherBranchTip, kCopyOnLine}] = SquashLandingAt(kCopyOnLine);
	model.Follow(TwoLinesRead(kLandingOnLine, kCopyOnLine));
	Settle(jobs, model);
	REQUIRE(port.Checked.size() == 2);

	// Both lines move on past their branch's landing: neither is checked again, though only release holds fix's.
	const Core::Ref<const LineRead> movedOn = TwoLinesRead(kAfterLanding, kAfterCopy);
	model.Follow(movedOn);
	Settle(jobs, model);

	CHECK(port.Checked.size() == 2);
	CHECK(IsKnownAs(model.KnownMarkOf(movedOn->Model.Branches[0]), SquashLandingAt(kLandingOnLine)));
	CHECK(IsKnownAs(model.KnownMarkOf(movedOn->Model.Branches[1]), SquashLandingAt(kCopyOnLine)));

	// release is reset to before fix's landing; main still holds topic's.
	const Core::Ref<const LineRead> reset = TwoLinesRead(kAfterLanding, kReset);
	model.Follow(reset);
	Settle(jobs, model);

	REQUIRE(port.Checked.size() == 3);
	CHECK(port.Checked[2] == ContentKey{kOtherBranchTip, kReset});
	CHECK(IsKnownAs(model.KnownMarkOf(reset->Model.Branches[0]), SquashLandingAt(kLandingOnLine)));
	CHECK(IsKnownAs(model.KnownMarkOf(reset->Model.Branches[1]), Core::ContentVerdict{}));
}
