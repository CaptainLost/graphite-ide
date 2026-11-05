#include <doctest/doctest.h>

#include "Git/Lines/LinesModel.h"

#include "DeferredJobs.h"
#include "GitFixtures.h"

#include "Core/Events/EventBus.h"
#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Lines/LineQuestions.h"
#include "Workspace/Workspace.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace Graphite;

namespace {
	constexpr std::int64_t kNow = 1800000000;

	using Snapshot = Core::Ref<const std::vector<Core::Branch>>;

	const Core::Oid kRoot = OidWithFirstByte(1);
	const Core::Oid kFork = OidWithFirstByte(2);
	const Core::Oid kMainTip = OidWithFirstByte(3);
	const Core::Oid kFeatureTip = OidWithFirstByte(4);
	const Core::Oid kNotInGraph = OidWithFirstByte(99);

	// root - fork - main tip, and the feature tip on the fork.
	Core::Ref<const Core::AncestryGraph> SmallGraph()
	{
		Core::AncestryGraph graph;
		graph.Append({Core::GraphCommit{.Id = kRoot, .Time = 100, .AuthorKey = "ann"},
					  Core::GraphCommit{.Id = kFork, .Parents = {kRoot}, .Time = 200, .AuthorKey = "ann"},
					  Core::GraphCommit{.Id = kMainTip, .Parents = {kFork}, .Time = 300, .AuthorKey = "ann"},
					  Core::GraphCommit{.Id = kFeatureTip, .Parents = {kFork}, .Time = 400, .AuthorKey = "bob"}});
		return Core::CreateRef<const Core::AncestryGraph>(std::move(graph));
	}

	Snapshot NewSnapshot()
	{
		return Core::CreateRef<const std::vector<Core::Branch>>();
	}

	Core::LineRef RefAt(std::string fullName, const Core::Oid& tip)
	{
		Core::LineRef ref;
		ref.FullName = std::move(fullName);
		ref.Tip = tip;
		return ref;
	}

	// Every read lands on the same small graph, with origin/main its one base; a change is applied to the answers of
	// the read it was asked on.
	class FakeLinesPort final : public LinesPort
	{
	public:
		std::uint64_t RepositoryNow() override
		{
			return Repository;
		}

		Core::Result<LineRead> ReadLines(std::uint64_t repository, const LineReadInput& input) override
		{
			Asked.push_back("read");
			ReadRepositories.push_back(repository);
			ReadInputs.push_back(input);

			if (FailReads)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "the graph cannot be read"};
			}

			LineRead read;
			read.Branches = input.Branches;
			read.Refs = {RefAt("refs/remotes/origin/main", kMainTip), RefAt("refs/heads/feature", kFeatureTip)};
			read.Graph = input.Graph != nullptr ? input.Graph : Graph;
			read.Generations = input.Generations != nullptr ? input.Generations : Generations;
			read.Model.Bases = {Core::LineBase{.Ref = 0}};
			return read;
		}

		Core::Result<LineRead> RecomputeLines(const LineRead& last, const LineAnswerChange& change, std::int64_t) override
		{
			Asked.push_back("change " + change.Ref);
			ChangedOn.push_back(last.Answers);
			ChangedBranches.push_back(last.Branches);

			if (FailChanges)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "the answers cannot be written"};
			}

			LineRead read = last;
			ApplyLineChange(read.Answers, change);
			return read;
		}

		Core::Ref<const Core::AncestryGraph> Graph = SmallGraph();
		Core::Ref<const std::vector<std::uint32_t>> Generations = Core::CreateRef<const std::vector<std::uint32_t>>(Core::GenerationsOf(*Graph));
		bool FailReads = false;
		bool FailChanges = false;
		std::uint64_t Repository = 1;

		std::vector<std::string> Asked;
		std::vector<std::uint64_t> ReadRepositories;
		std::vector<LineReadInput> ReadInputs;
		std::vector<Core::LineAnswers> ChangedOn;
		std::vector<Snapshot> ChangedBranches;
	};

	std::int64_t FixedNow()
	{
		return kNow;
	}

	// Past the second a failed read waits before it is asked again.
	constexpr float kAfterFirstRetry = 1.1f;

	// Runs every job and lands every result, as frames do while workers finish, until nothing more is asked.
	void RunUntilSettled(DeferredJobs& jobs, LinesModel& model)
	{
		while (jobs.Count() > 0)
		{
			jobs.RunAll();
			model.OnUpdate(Core::Timestep{});
		}
	}

	LineAnswerChange UseAsBase(std::string ref)
	{
		return LineAnswerChange{.What = LineAnswerChange::Kind::UseAsBase, .Ref = std::move(ref)};
	}

	// Reads a new snapshot with a port whose reads fail, then lists it again twice, each time once the back-off ends.
	void FailThreeTimesInARow(DeferredJobs& jobs, LinesModel& model, const Snapshot& snapshot)
	{
		model.Follow(snapshot, 1);
		RunUntilSettled(jobs, model);
		model.Follow(snapshot, 2);
		model.OnUpdate(Core::Timestep{1.0f});
		RunUntilSettled(jobs, model);
		model.Follow(snapshot, 3);
		model.OnUpdate(Core::Timestep{2.0f});
		RunUntilSettled(jobs, model);
	}
}

TEST_CASE("Git window: the lines model asks one read per branch snapshot")
{
	FakeLinesPort port;
	DeferredJobs jobs;
	LinesModel model{port, jobs.Submitter(), FixedNow};

	model.Follow(nullptr);
	CHECK(jobs.Count() == 0);

	const Snapshot first = NewSnapshot();
	model.Follow(first);
	model.Follow(first);
	CHECK(jobs.Count() == 1);
	CHECK(model.IsReading());

	RunUntilSettled(jobs, model);
	model.Follow(first);
	CHECK(jobs.Count() == 0);
	CHECK_FALSE(model.IsReading());
	REQUIRE(model.GetRead() != nullptr);
	CHECK(model.GetRead()->Branches == first);

	const Snapshot second = NewSnapshot();
	model.Follow(second);
	RunUntilSettled(jobs, model);

	REQUIRE(port.ReadInputs.size() == 2);
	CHECK(port.ReadInputs[0].Branches == first);
	CHECK(port.ReadInputs[0].Graph == nullptr);
	CHECK(port.ReadInputs[0].Now == kNow);

	// The graph stays in memory between reads, with its generations.
	CHECK(port.ReadInputs[1].Branches == second);
	CHECK(port.ReadInputs[1].Graph == port.Graph);
	CHECK(port.ReadInputs[1].Generations == port.Generations);
	CHECK(model.GetRead()->Branches == second);
}

TEST_CASE("Git window: only the newest snapshot waits behind a running read")
{
	FakeLinesPort port;
	DeferredJobs jobs;
	LinesModel model{port, jobs.Submitter(), FixedNow};

	const Snapshot running = NewSnapshot();
	const Snapshot passedOver = NewSnapshot();
	const Snapshot newest = NewSnapshot();

	model.Follow(running);
	model.Follow(passedOver);
	model.Follow(newest);
	CHECK(jobs.Count() == 1);

	jobs.RunAll();
	model.OnUpdate(Core::Timestep{});
	REQUIRE(model.GetRead() != nullptr);
	CHECK(model.GetRead()->Branches == running);
	CHECK(model.IsReading());

	RunUntilSettled(jobs, model);

	REQUIRE(port.ReadInputs.size() == 2);
	CHECK(port.ReadInputs[0].Branches == running);
	CHECK(port.ReadInputs[1].Branches == newest);
	CHECK(model.GetRead()->Branches == newest);
	CHECK_FALSE(model.IsReading());
}

TEST_CASE("Git window: a change asked during a read runs after it on the newest read")
{
	FakeLinesPort port;
	DeferredJobs jobs;
	LinesModel model{port, jobs.Submitter(), FixedNow};

	const Snapshot first = NewSnapshot();
	const Snapshot second = NewSnapshot();

	model.Follow(first);
	model.Change(UseAsBase("refs/heads/staging"));
	model.Change(UseAsBase("refs/heads/release"));
	model.Follow(second);
	CHECK(jobs.Count() == 1);

	RunUntilSettled(jobs, model);

	const std::vector<std::string> order = {"read", "change refs/heads/staging", "change refs/heads/release", "read"};
	CHECK(port.Asked == order);

	REQUIRE(port.ChangedBranches.size() == 2);
	CHECK(port.ChangedBranches[0] == first);
	CHECK(port.ChangedOn[0].AddedBases.empty());
	CHECK(port.ChangedOn[1].AddedBases == std::vector<std::string>{"refs/heads/staging"});

	REQUIRE(port.ReadInputs.size() == 2);
	CHECK(port.ReadInputs[1].Branches == second);
	REQUIRE(model.GetRead() != nullptr);
	CHECK(model.GetRead()->Branches == second);
}

TEST_CASE("Git window: a read is run on the repository the port opened when it was asked")
{
	FakeLinesPort port;
	DeferredJobs jobs;
	LinesModel model{port, jobs.Submitter(), FixedNow};

	port.Repository = 7;
	model.Follow(NewSnapshot());
	port.Repository = 8;
	RunUntilSettled(jobs, model);

	CHECK(port.ReadRepositories == std::vector<std::uint64_t>{7});
}

TEST_CASE("Git window: a failed read keeps the previous read")
{
	FakeLinesPort port;
	DeferredJobs jobs;
	LinesModel model{port, jobs.Submitter(), FixedNow};

	model.Follow(NewSnapshot());
	RunUntilSettled(jobs, model);
	const Core::Ref<const LineRead> earlier = model.GetRead();
	REQUIRE(earlier != nullptr);
	CHECK_FALSE(model.GetError().has_value());

	port.FailReads = true;
	model.Follow(NewSnapshot());
	RunUntilSettled(jobs, model);

	CHECK(model.GetRead() == earlier);
	REQUIRE(model.GetError().has_value());
	CHECK(model.GetError()->Message == "the graph cannot be read");
	CHECK_FALSE(model.IsReading());

	port.FailReads = false;
	const Snapshot later = NewSnapshot();
	model.Follow(later);
	RunUntilSettled(jobs, model);

	REQUIRE(model.GetRead() != nullptr);
	CHECK(model.GetRead()->Branches == later);
	CHECK_FALSE(model.GetError().has_value());
}

TEST_CASE("Git window: a failed read is asked again when the branches are listed again as the same snapshot")
{
	FakeLinesPort port;
	DeferredJobs jobs;
	LinesModel model{port, jobs.Submitter(), FixedNow};

	port.FailReads = true;
	const Snapshot snapshot = NewSnapshot();
	model.Follow(snapshot, 1);
	RunUntilSettled(jobs, model);
	REQUIRE(model.GetError().has_value());

	// Every frame follows the snapshot; only a new listing asks again, once a second has passed since the failure.
	model.Follow(snapshot, 1);
	model.OnUpdate(Core::Timestep{kAfterFirstRetry});
	CHECK(jobs.Count() == 0);

	port.FailReads = false;
	model.Follow(snapshot, 2);
	CHECK(model.IsReading());
	RunUntilSettled(jobs, model);

	REQUIRE(model.GetRead() != nullptr);
	CHECK_FALSE(model.GetError().has_value());

	// A read that did not fail is not asked again for the same snapshot.
	model.Follow(snapshot, 3);
	CHECK(jobs.Count() == 0);
	CHECK(port.ReadInputs.size() == 2);
}

TEST_CASE("Git window: a failed read listed again is asked again only once its back-off ends")
{
	FakeLinesPort port;
	DeferredJobs jobs;
	LinesModel model{port, jobs.Submitter(), FixedNow};

	port.FailReads = true;
	const Snapshot snapshot = NewSnapshot();
	model.Follow(snapshot, 1);
	RunUntilSettled(jobs, model);
	REQUIRE(port.ReadInputs.size() == 1);

	// Listed again half a second after the failure: the read is asked again a second after it, with no listing then.
	model.OnUpdate(Core::Timestep{0.5f});
	model.Follow(snapshot, 2);
	CHECK(jobs.Count() == 0);
	model.OnUpdate(Core::Timestep{0.4f});
	CHECK(jobs.Count() == 0);
	model.OnUpdate(Core::Timestep{0.2f});
	CHECK(jobs.Count() == 1);
	RunUntilSettled(jobs, model);
	REQUIRE(port.ReadInputs.size() == 2);

	// The second failure in a row is asked again two seconds after it.
	model.Follow(snapshot, 3);
	model.OnUpdate(Core::Timestep{1.9f});
	CHECK(jobs.Count() == 0);
	model.OnUpdate(Core::Timestep{0.2f});
	CHECK(jobs.Count() == 1);
	RunUntilSettled(jobs, model);

	// Without a listing since the failure nothing is asked, however long it waits.
	model.OnUpdate(Core::Timestep{100.0f});
	CHECK(jobs.Count() == 0);
	CHECK(port.ReadInputs.size() == 3);
}

TEST_CASE("Git window: a new snapshot a folder switch or a line change resets the back-off of a failed read")
{
	FakeLinesPort port;
	DeferredJobs jobs;
	LinesModel model{port, jobs.Submitter(), FixedNow};

	model.Follow(NewSnapshot(), 0);
	RunUntilSettled(jobs, model);
	REQUIRE(model.GetRead() != nullptr);

	port.FailReads = true;
	const Snapshot failing = NewSnapshot();
	FailThreeTimesInARow(jobs, model, failing);
	REQUIRE(port.ReadInputs.size() == 4);

	// A snapshot that differs is read at once.
	const Snapshot changed = NewSnapshot();
	model.Follow(changed, 4);
	CHECK(jobs.Count() == 1);
	RunUntilSettled(jobs, model);

	// And the back-off starts again from a second.
	model.Follow(changed, 5);
	model.OnUpdate(Core::Timestep{1.0f});
	CHECK(jobs.Count() == 1);
	RunUntilSettled(jobs, model);

	// A line change starts it again from a second, even when the change fails too.
	port.FailChanges = true;
	model.Change(UseAsBase("refs/heads/staging"));
	RunUntilSettled(jobs, model);
	model.Follow(changed, 6);
	model.OnUpdate(Core::Timestep{1.0f});
	CHECK(jobs.Count() == 1);
	RunUntilSettled(jobs, model);

	// A folder switch forgets the failures with everything else.
	model.Clear();
	model.Follow(failing, 7);
	CHECK(jobs.Count() == 1);
	RunUntilSettled(jobs, model);
	model.Follow(failing, 8);
	model.OnUpdate(Core::Timestep{1.0f});
	CHECK(jobs.Count() == 1);
	RunUntilSettled(jobs, model);
}

TEST_CASE("Git window: a failed change is handed over once to be said and the read stays")
{
	FakeLinesPort port;
	DeferredJobs jobs;
	LinesModel model{port, jobs.Submitter(), FixedNow};

	model.Follow(NewSnapshot());
	RunUntilSettled(jobs, model);
	const Core::Ref<const LineRead> read = model.GetRead();
	CHECK_FALSE(model.TakeChangeFailure().has_value());

	port.FailChanges = true;
	model.Change(UseAsBase("refs/heads/staging"));
	RunUntilSettled(jobs, model);

	CHECK(model.GetRead() == read);
	const std::optional<Core::Error> failure = model.TakeChangeFailure();
	REQUIRE(failure.has_value());
	CHECK(failure->Message == "the answers cannot be written");
	CHECK_FALSE(model.TakeChangeFailure().has_value());

	// A failed read is said elsewhere, not handed over as a change.
	port.FailReads = true;
	model.Follow(NewSnapshot());
	RunUntilSettled(jobs, model);
	CHECK(model.GetError().has_value());
	CHECK_FALSE(model.TakeChangeFailure().has_value());
}

TEST_CASE("Git window: a line change that succeeds after a newer snapshot's read failed reads that snapshot next")
{
	FakeLinesPort port;
	DeferredJobs jobs;
	LinesModel model{port, jobs.Submitter(), FixedNow};

	const Snapshot first = NewSnapshot();
	model.Follow(first, 1);
	RunUntilSettled(jobs, model);
	REQUIRE(model.GetRead() != nullptr);

	port.FailReads = true;
	const Snapshot newer = NewSnapshot();
	model.Follow(newer, 2);
	RunUntilSettled(jobs, model);
	REQUIRE(model.GetError().has_value());

	SUBCASE("with a retry waiting out its back-off")
	{
		model.Follow(newer, 3);
		CHECK(jobs.Count() == 0);
	}

	SUBCASE("with no listing since the failure")
	{
	}

	// The change is recomputed on the first snapshot's read, which the failure left in hand; its success is no read of
	// the newer snapshot, so that is read right after it, with no time passing and no listing.
	port.FailReads = false;
	model.Change(UseAsBase("refs/heads/staging"));
	RunUntilSettled(jobs, model);

	const std::vector<std::string> order = {"read", "read", "change refs/heads/staging", "read"};
	CHECK(port.Asked == order);
	REQUIRE(port.ChangedBranches.size() == 1);
	CHECK(port.ChangedBranches[0] == first);
	REQUIRE(model.GetRead() != nullptr);
	CHECK(model.GetRead()->Branches == newer);
	CHECK_FALSE(model.GetError().has_value());
}

TEST_CASE("Git window: a read landing after clear is dropped")
{
	FakeLinesPort port;
	DeferredJobs jobs;
	LinesModel model{port, jobs.Submitter(), FixedNow};

	SUBCASE("a read that ran before the clear")
	{
		model.Follow(NewSnapshot());
		jobs.RunAll();
		model.Clear();
		model.OnUpdate(Core::Timestep{});

		CHECK(model.GetRead() == nullptr);
		CHECK_FALSE(model.IsReading());
	}

	SUBCASE("a read still waiting for a worker never reads the next folder")
	{
		model.Follow(NewSnapshot());
		model.Clear();
		jobs.RunAll();
		model.OnUpdate(Core::Timestep{});

		CHECK(port.ReadInputs.empty());
		CHECK(model.GetRead() == nullptr);
	}

	SUBCASE("a change that ran before the folder closed")
	{
		Core::EventBus events;
		model.SubscribeTo(events);
		model.Follow(NewSnapshot());
		RunUntilSettled(jobs, model);

		model.Change(UseAsBase("refs/heads/staging"));
		jobs.RunAll();
		events.Publish(WorkspaceClosedEvent{});
		model.OnUpdate(Core::Timestep{});
		CHECK(model.GetRead() == nullptr);

		// The next folder's read starts at once, not behind the change that was dropped.
		const Snapshot next = NewSnapshot();
		model.Follow(next);
		CHECK(jobs.Count() == 1);
		RunUntilSettled(jobs, model);
		REQUIRE(model.GetRead() != nullptr);
		CHECK(model.GetRead()->Branches == next);
		CHECK(model.GetRead()->Answers.AddedBases.empty());
	}

	SUBCASE("a question asked before another folder opened")
	{
		Core::EventBus events;
		model.SubscribeTo(events);
		model.Follow(NewSnapshot());
		RunUntilSettled(jobs, model);

		CHECK_FALSE(model.DistanceOf(kFeatureTip, kMainTip).has_value());
		jobs.RunAll();
		events.Publish(WorkspaceOpenedEvent{.Root = "C:/other"});
		model.OnUpdate(Core::Timestep{});

		model.Follow(NewSnapshot());
		RunUntilSettled(jobs, model);
		CHECK_FALSE(model.DistanceOf(kFeatureTip, kMainTip).has_value());
		CHECK(jobs.Count() == 1);
	}
}

TEST_CASE("Git window: a distance is asked once and cached")
{
	FakeLinesPort port;
	DeferredJobs jobs;
	LinesModel model{port, jobs.Submitter(), FixedNow};

	CHECK_FALSE(model.DistanceOf(kFeatureTip, kMainTip).has_value());
	CHECK(jobs.Count() == 0);

	model.Follow(NewSnapshot());
	RunUntilSettled(jobs, model);

	CHECK_FALSE(model.DistanceOf(kFeatureTip, kMainTip).has_value());
	CHECK_FALSE(model.DistanceOf(kFeatureTip, kMainTip).has_value());
	CHECK(jobs.Count() == 1);

	// A tip the graph lacks is not asked.
	CHECK_FALSE(model.DistanceOf(kNotInGraph, kMainTip).has_value());
	CHECK(jobs.Count() == 1);

	jobs.RunAll();
	model.OnUpdate(Core::Timestep{});

	const std::optional<Core::LineDistance> distance = model.DistanceOf(kFeatureTip, kMainTip);
	REQUIRE(distance.has_value());
	CHECK(distance->Ahead == 1);
	CHECK(distance->Behind == 1);
	CHECK(distance->ForkTime == std::optional<std::int64_t>{200});
	CHECK(distance->ForkOnLine);
	CHECK(jobs.Count() == 0);

	// A question does not wait for a read, and an answer is kept across reads.
	model.Follow(NewSnapshot());
	CHECK(model.IsReading());
	CHECK_FALSE(model.DistanceOf(kMainTip, kFeatureTip).has_value());
	CHECK(jobs.Count() == 2);
	RunUntilSettled(jobs, model);

	CHECK(model.DistanceOf(kFeatureTip, kMainTip) == distance);
	const std::optional<Core::LineDistance> reverse = model.DistanceOf(kMainTip, kFeatureTip);
	REQUIRE(reverse.has_value());
	CHECK(reverse->Ahead == 1);
	CHECK(reverse->Behind == 1);
	CHECK(jobs.Count() == 0);
}

TEST_CASE("Git window: the lines model says which commits its read's graph holds")
{
	FakeLinesPort port;
	DeferredJobs jobs;
	LinesModel model{port, jobs.Submitter(), FixedNow};

	CHECK_FALSE(model.HoldsCommit(kFeatureTip));

	model.Follow(NewSnapshot());
	RunUntilSettled(jobs, model);

	CHECK(model.HoldsCommit(kFeatureTip));
	CHECK(model.HoldsCommit(kMainTip));
	CHECK_FALSE(model.HoldsCommit(kNotInGraph));
}

TEST_CASE("Git window: which bases hold a commit is asked once and cached")
{
	FakeLinesPort port;
	DeferredJobs jobs;
	LinesModel model{port, jobs.Submitter(), FixedNow};

	model.Follow(NewSnapshot());
	RunUntilSettled(jobs, model);

	CHECK_FALSE(model.BasesHolding(kFork).has_value());
	CHECK_FALSE(model.BasesHolding(kFork).has_value());
	CHECK_FALSE(model.BasesHolding(kFeatureTip).has_value());
	CHECK_FALSE(model.BasesHolding(kNotInGraph).has_value());
	CHECK(jobs.Count() == 2);

	jobs.RunAll();
	model.OnUpdate(Core::Timestep{});

	CHECK(model.BasesHolding(kFork) == std::optional<std::vector<bool>>{std::vector<bool>{true}});
	CHECK(model.BasesHolding(kFeatureTip) == std::optional<std::vector<bool>>{std::vector<bool>{false}});
	CHECK(jobs.Count() == 0);
}
