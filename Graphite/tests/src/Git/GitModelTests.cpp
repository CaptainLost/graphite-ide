#include <doctest/doctest.h>

#include "DeferredJobs.h"
#include "GitFixtures.h"
#include "Git/GitModel.h"

#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"
#include "Git/Diff/SyntaxRuns.h"
#include "Workspace/Workspace.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

	constexpr float kFrameSeconds = 0.016f;
	constexpr float kNoSeconds = 0.0f;
	constexpr float kPastDebounceSeconds = 0.2f;
	constexpr float kPastMaxDebounceSeconds = 1.0f;
	constexpr float kBurstStepSeconds = 0.02f;
	constexpr float kPartialDebounceStepSeconds = 0.05f;
	constexpr float kRestOfDebounceSeconds = 0.1f;
	constexpr float kStreamStepSeconds = 0.1f;
	constexpr int kEventsInBurst = 5;
	constexpr int kStreamEventsBeforeCap = 9;
	constexpr std::size_t kRequestsPastBound = 4;
	constexpr std::uint8_t kOldestCommitFirstByte = 0;
	constexpr const char* kAbsentPath = "";

	class FakePort final : public NetworkDoublePort
	{
	public:
		using TextsByPathsMap = std::map<std::string, Graphite::FileTexts>;
		using UntrackedByDirectoryMap = std::map<std::filesystem::path, Core::WorkingTreeStatus>;

		Core::Result<Core::WorkingTreeStatus> QueryStatus() override
		{
			++StatusCalls;

			if (FailStatus)
			{
				return PortFailure();
			}

			return Status;
		}

		Core::Result<Core::WorkingTreeStatus> QueryUntrackedIn(
			const std::filesystem::path& directory) override
		{
			++UntrackedCalls;
			LastUntrackedDirectory = directory;

			if (FailUntracked)
			{
				return PortFailure();
			}

			if (UntrackedByDirectory.contains(directory))
			{
				return UntrackedByDirectory.at(directory);
			}

			return Core::WorkingTreeStatus{};
		}

		Core::Result<std::filesystem::path> GetWorkingDirectory() override
		{
			return WorkingDirectory;
		}

		Core::Result<Core::HeadState> QueryHead() override
		{
			++HeadCalls;

			if (FailHead)
			{
				return PortFailure();
			}

			return Head;
		}

		Core::Result<void> Init() override
		{
			const std::vector<std::filesystem::path> noPaths;
			return Record("init", noPaths);
		}

		Core::Result<std::vector<Core::Branch>> QueryBranches() override
		{
			++BranchCalls;
			return Branches;
		}

		Core::Result<std::vector<Core::Tag>> QueryTags(Core::TagCache& cache) override
		{
			TagCacheSizes.push_back(cache.GetSize());
			BranchCallsBeforeTags.push_back(BranchCalls);

			if (FailTags)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "the tags cannot be read"};
			}

			// One entry more per listing, as each listing after a fetch reads a new tag object: what is in the cache tells
			// whether two listings shared it.
			cache.Store(OidWithFirstByte(static_cast<std::uint8_t>(TagCacheSizes.size())), Core::TagCacheEntry{});

			return Tags;
		}

		Core::Result<std::vector<Core::TipAuthors>> QueryOwnAuthors(const Core::OwnAuthorsRequest& request) override
		{
			++OwnAuthorsCalls;
			LastOwnAuthorsRequest = request;

			if (FailOwnAuthors)
			{
				return PortFailure();
			}

			return OwnAuthors;
		}

		Core::Result<std::optional<std::string>> ReadUserEmail() override { return UserEmail; }
		Core::Result<std::vector<Core::Remote>> QueryRemotes() override { return Remotes; }

		Core::Result<Core::LogPage> QueryLog(const Core::LogRequest& request) override
		{
			++LogCalls;

			LastLogRequest = request;

			if (FailLog)
			{
				return PortFailure();
			}

			return Log;
		}

		Core::Result<Graphite::CommitCount> CountCommits(const std::optional<std::string>& ref, Core::FollowedParents parents,
														 const std::optional<Graphite::CommitCount>& known) override
		{
			++CountCalls;
			LastCountRef = ref;
			LastCountParents = parents;
			LastKnownCount = known;

			if (FailCount)
			{
				return PortFailure();
			}

			if (known.has_value() && known->Tip == CountTip)
			{
				return *known;
			}

			++CountWalks;
			return Graphite::CommitCount{.Tip = CountTip,
										 .Count = parents == Core::FollowedParents::FirstOnly ? FirstParentCountTotal : CountTotal};
		}

		Core::Result<Core::Diff> QueryDiff(const Core::DiffRequest& request) override
		{
			++DiffCalls;
			DiffTargets.push_back(request.Target);

			if (FailDiff)
			{
				return PortFailure();
			}

			return Diff;
		}

		Core::Result<Graphite::FileTexts> ReadFileTexts(
			const Core::Oid&, const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath) override
		{
			++TextCalls;

			if (FailTexts)
			{
				return PortFailure();
			}

			const std::string pathsKey = PathsKey(oldPath, newPath);

			if (TextsByPaths.contains(pathsKey))
			{
				return TextsByPaths.at(pathsKey);
			}

			return Texts;
		}

		static std::string PathsKey(const std::filesystem::path& oldPath,
									const std::filesystem::path& newPath)
		{
			return Core::ToUtf8(oldPath) + ">" + Core::ToUtf8(newPath);
		}

		Core::Result<Graphite::FileTexts> ReadWorkingTreeTexts(
			Core::DiffTarget, const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath) override
		{
			++WorkingTreeTextCalls;

			LastTextPaths = Core::ToUtf8(oldPath) + "|" + Core::ToUtf8(newPath);

			return WorkingTreeTexts;
		}

		Graphite::FileTexts WorkingTreeTexts;
		int WorkingTreeTextCalls = 0;
		std::string LastTextPaths;

		Core::Result<void> Stage(const std::vector<std::filesystem::path>& paths) override
		{
			return Record("stage", paths);
		}

		Core::Result<void> Unstage(const std::vector<std::filesystem::path>& paths) override
		{
			return Record("unstage", paths);
		}

		Core::Result<void> DiscardWorkTree(const std::vector<std::filesystem::path>& paths) override
		{
			return Record("discard", paths);
		}

		Core::Result<void> DeleteUntracked(const std::vector<std::filesystem::path>& paths) override
		{
			return Record("delete", paths);
		}

		Core::Result<Core::Oid> Commit(const Core::CommitRequest& request) override
		{
			WriteCalls.push_back("commit:" + request.Message);

			if (FailWrites)
			{
				return PortFailure();
			}

			return CommittedOid;
		}

		Core::Result<void> CreateBranch(const std::string& name, const Core::Oid&) override
		{
			WriteCalls.push_back("branch:" + name);

			return WriteResult();
		}

		Core::Result<void> RenameBranch(const std::string& from, const std::string& to) override
		{
			WriteCalls.push_back("rename:" + from + ">" + to);

			return WriteResult();
		}

		Core::Result<void> DeleteBranch(const std::string& name) override
		{
			WriteCalls.push_back("delete-branch:" + name);

			return WriteResult();
		}

		Core::Result<void> StashApply(const Core::Oid& id) override
		{
			WriteCalls.push_back("apply:" + id.ShortHex());

			return WriteResult();
		}

		Core::Result<void> StashPop(const Core::Oid& id) override
		{
			WriteCalls.push_back("pop:" + id.ShortHex());

			return WriteResult();
		}

		Core::Result<void> StashDrop(const Core::Oid& id) override
		{
			WriteCalls.push_back("drop:" + id.ShortHex());

			return WriteResult();
		}

		Core::Result<void> DiscardAll(Core::UntrackedFiles untrackedFiles) override
		{
			WriteCalls.push_back(std::string{"discard-all:"} + UntrackedFlag(untrackedFiles));

			return WriteResult();
		}

		Core::Result<std::size_t> CountUnmergedCommits(const std::string&) override
		{
			return Unmerged;
		}

		Core::Result<bool> StashSave(const std::string& message, Core::UntrackedFiles untrackedFiles) override
		{
			WriteCalls.push_back("stash:" + message + "|" + UntrackedFlag(untrackedFiles));

			if (FailWrites)
			{
				return PortFailure();
			}

			return StashSaves;
		}

		Core::Result<std::vector<Core::StashEntry>> QueryStashes() override
		{
			++StashCalls;
			return Stashes;
		}

		Core::Result<Core::CheckoutPlan> PlanCheckout(const Core::CheckoutTarget& target) override
		{
			WriteCalls.push_back("plan:" + TargetLabel(target));

			if (FailWrites)
			{
				return PortFailure();
			}

			return Plan;
		}

		Core::CheckoutOutcome Checkout(const Core::CheckoutTarget& target) override
		{
			WriteCalls.push_back("checkout:" + TargetLabel(target));

			if (FailWrites)
			{
				return Core::CheckoutOutcome{{}, PortFailure()};
			}

			return CheckoutResult;
		}

		std::vector<Core::StashEntry> Stashes;
		std::vector<Core::DiffTarget> DiffTargets;
		int StashCalls = 0;
		Core::CheckoutPlan Plan;
		Core::CheckoutOutcome CheckoutResult;
		std::size_t Unmerged = 0;
		bool StashSaves = true;

		std::vector<std::string> WriteCalls;
		bool FailWrites = false;
		Core::Oid CommittedOid{};

		Core::WorkingTreeStatus Status;
		std::filesystem::path WorkingDirectory = "C:/repo";
		std::vector<Core::Branch> Branches;
		std::vector<Core::Tag> Tags;
		bool FailTags = false;
		std::vector<std::size_t> TagCacheSizes; // what the cache held as each tag listing began
		std::vector<int> BranchCallsBeforeTags; // the branch listings read before each tag listing
		std::vector<Core::TipAuthors> OwnAuthors;
		std::optional<std::string> UserEmail;
		std::vector<Core::Remote> Remotes;
		bool FailOwnAuthors = false;
		int OwnAuthorsCalls = 0;
		std::optional<Core::OwnAuthorsRequest> LastOwnAuthorsRequest;
		Core::HeadState Head;
		bool FailHead = false;
		Core::LogPage Log;
		Core::Diff Diff;
		Graphite::FileTexts Texts;

		TextsByPathsMap TextsByPaths;

		bool FailStatus = false;
		bool FailLog = false;
		bool FailDiff = false;
		bool FailTexts = false;

		int StatusCalls = 0;
		int BranchCalls = 0;
		int HeadCalls = 0;
		int LogCalls = 0;
		std::optional<Core::LogRequest> LastLogRequest;
		int CountCalls = 0;
		int CountWalks = 0;
		bool FailCount = false;
		Core::Oid CountTip = OidWithFirstByte(1);
		std::size_t CountTotal = 1739;
		std::size_t FirstParentCountTotal = 812;
		Core::FollowedParents LastCountParents = Core::FollowedParents::All;
		std::optional<std::string> LastCountRef;
		std::optional<Graphite::CommitCount> LastKnownCount;
		int DiffCalls = 0;
		int TextCalls = 0;

		int UntrackedCalls = 0;
		bool FailUntracked = false;
		std::filesystem::path LastUntrackedDirectory;
		UntrackedByDirectoryMap UntrackedByDirectory;

	private:
		static Core::Error PortFailure()
		{
			return Core::Error{Core::ErrorCode::IoFailure, "no"};
		}

		static const char* UntrackedFlag(Core::UntrackedFiles untrackedFiles)
		{
			return untrackedFiles == Core::UntrackedFiles::Included ? "1" : "0";
		}

		Core::Result<void> WriteResult() const
		{
			return FailWrites ? Core::Result<void>{PortFailure()} : Core::Result<void>::Ok();
		}

		static std::string TargetLabel(const Core::CheckoutTarget& target)
		{
			return target.Kind == Core::CheckoutKind::Commit ? target.Commit.ShortHex() : target.Name;
		}

		Core::Result<void> Record(const std::string& kind,
								  const std::vector<std::filesystem::path>& paths)
		{
			std::string line = kind + ":";

			for (std::size_t pathIndex = 0; pathIndex < paths.size(); ++pathIndex)
			{
				if (pathIndex != 0)
				{
					line += ",";
				}

				line += Core::ToUtf8(paths[pathIndex]);
			}

			WriteCalls.push_back(line);

			if (FailWrites)
			{
				return PortFailure();
			}

			return Core::Result<void>::Ok();
		}
	};

	Core::FileStatus ModifiedStatusEntry(const char* path)
	{
		Core::FileStatus status;
		status.Path = path;
		status.WorkTree = Core::StatusFlags::Modified;
		return status;
	}

	void RequestTextsOfUnrenamedFile(
		Graphite::GitModel& model, const Core::Oid& commit, const char* path)
	{
		model.RequestTextsFor(commit, path, path, path);
	}

	constexpr std::uint8_t kMainTipByte = 1;
	constexpr std::uint8_t kOriginMainTipByte = 2;
	constexpr std::uint8_t kFeatureTipByte = 3;
	constexpr std::uint8_t kMovedFeatureTipByte = 4;

	Core::Branch BranchWithReadableTip(const std::string& name, const std::string& fullName, std::uint8_t tipByte)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = fullName;
		branch.Tip = OidWithFirstByte(tipByte);

		Core::CommitSummary tip;
		tip.Id = branch.Tip;
		branch.TipCommit = tip;

		return branch;
	}

	std::vector<Core::Branch> TrunkAndFeature(std::uint8_t featureTipByte)
	{
		Core::Branch main = BranchWithReadableTip("main", "refs/heads/main", kMainTipByte);
		main.IsHead = true;
		main.Upstream = "origin/main";

		Core::Branch originMain = BranchWithReadableTip("origin/main", "refs/remotes/origin/main", kOriginMainTipByte);
		originMain.Kind = Core::RefKind::Remote;
		originMain.IsRemoteDefault = true;

		const Core::Branch feature = BranchWithReadableTip("feature", "refs/heads/feature", featureTipByte);

		return {main, originMain, feature};
	}

	Core::Tag TagOn(const std::string& name, std::uint8_t commitByte, std::int64_t when)
	{
		return Core::Tag{.Name = name,
						 .FullName = "refs/tags/" + name,
						 .Target = OidWithFirstByte(commitByte),
						 .When = std::chrono::sys_seconds{std::chrono::seconds{when}}};
	}

	constexpr std::int64_t kOlderTagTime = 1700000000;
	constexpr std::int64_t kNewerTagTime = 1750000000;

	// What GraphiteLayer passes once the line read lands: the tips of the bases TrunkAndFeature stands for.
	void HideTrunkTips(Graphite::GitModel& model)
	{
		model.SetAuthorsHidden({OidWithFirstByte(kMainTipByte), OidWithFirstByte(kOriginMainTipByte)});
	}

	void ListBranchesAndLandAuthors(Graphite::GitModel& model)
	{
		model.RequestBranches();
		model.OnUpdate(Core::Timestep{kFrameSeconds});
		model.OnUpdate(Core::Timestep{kFrameSeconds});
	}
}

TEST_CASE("Before anything is requested the model has no snapshot")
{
	FakePort port;
	Graphite::GitModel model{port, RunJobsInline()};

	CHECK(model.GetStatus() == nullptr);
	CHECK(model.GetBranches() == nullptr);
}

TEST_CASE("A requested status becomes readable after the next update")
{
	FakePort port;
	port.Status.Entries.push_back(ModifiedStatusEntry("a.txt"));

	Graphite::GitModel model{port, RunJobsInline()};

	model.RequestStatus();

	CHECK(model.GetStatus() == nullptr);

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetStatus() != nullptr);
	REQUIRE(model.GetStatus()->Entries.size() == 1);
	CHECK(model.GetStatus()->Entries.front().Path == std::filesystem::path("a.txt"));
}

TEST_CASE("A snapshot the view already holds survives the next refresh")
{
	FakePort port;
	port.Status.Entries.push_back(ModifiedStatusEntry("a.txt"));

	Graphite::GitModel model{port, RunJobsInline()};

	model.RequestStatus();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	const std::shared_ptr<const Core::WorkingTreeStatus> held = model.GetStatus();

	port.Status.Entries.clear();
	model.RequestStatus();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(held != nullptr);
	CHECK(held->Entries.size() == 1);
	CHECK(model.GetStatus()->Entries.empty());
}

TEST_CASE("A superseded result is discarded rather than shown")
{
	FakePort port;
	port.Status.Entries.push_back(ModifiedStatusEntry("stale.txt"));

	Graphite::GitModel model{port, RunJobsInline()};

	model.RequestStatus();

	port.Status.Entries.clear();
	port.Status.Entries.push_back(ModifiedStatusEntry("fresh.txt"));
	model.RequestStatus();

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetStatus() != nullptr);
	REQUIRE(model.GetStatus()->Entries.size() == 1);

	CHECK(model.GetStatus()->Entries.front().Path == std::filesystem::path("fresh.txt"));
}

TEST_CASE("A failed query leaves the last good snapshot in place")
{
	FakePort port;
	port.Status.Entries.push_back(ModifiedStatusEntry("a.txt"));

	Graphite::GitModel model{port, RunJobsInline()};

	model.RequestStatus();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	port.FailStatus = true;
	model.RequestStatus();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetStatus() != nullptr);
	CHECK(model.GetStatus()->Entries.size() == 1);
	CHECK(model.GetStatusError().has_value());
}

TEST_CASE("An update with nothing pending asks nothing")
{
	FakePort port;
	Graphite::GitModel model{port, RunJobsInline()};

	model.OnUpdate(Core::Timestep{kFrameSeconds});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.StatusCalls == 0);
	CHECK(port.BranchCalls == 0);
	CHECK(port.LogCalls == 0);
}

TEST_CASE("A file-change event does not refresh until the debounce has elapsed")
{
	FakePort port;
	Core::EventBus events;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{});

	model.OnUpdate(Core::Timestep{kPartialDebounceStepSeconds});
	CHECK(port.StatusCalls == 0);

	model.OnUpdate(Core::Timestep{kPartialDebounceStepSeconds});
	CHECK(port.StatusCalls == 0);

	model.OnUpdate(Core::Timestep{kRestOfDebounceSeconds});
	CHECK(port.StatusCalls == 1);
}

TEST_CASE("A burst of file-change events causes one refresh")
{
	FakePort port;
	Core::EventBus events;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	for (int eventNumber = 0; eventNumber < kEventsInBurst; ++eventNumber)
	{
		events.Publish(Graphite::WorkspaceFilesChangedEvent{});
		model.OnUpdate(Core::Timestep{kBurstStepSeconds});
	}

	CHECK(port.StatusCalls == 0);

	model.OnUpdate(Core::Timestep{kPastDebounceSeconds});
	CHECK(port.StatusCalls == 1);
	CHECK(port.BranchCalls == 1);
}

TEST_CASE("The timer does not keep firing once it has fired")
{
	FakePort port;
	Core::EventBus events;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastDebounceSeconds});
	REQUIRE(port.StatusCalls == 1);

	model.OnUpdate(Core::Timestep{kPastMaxDebounceSeconds});
	model.OnUpdate(Core::Timestep{kPastMaxDebounceSeconds});

	CHECK(port.StatusCalls == 1);
}

TEST_CASE("Closing the workspace drops the snapshots")
{
	FakePort port;
	Core::EventBus events;
	port.Status.Entries.push_back(ModifiedStatusEntry("a.txt"));

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestStatus();
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	REQUIRE(model.GetStatus() != nullptr);

	events.Publish(Graphite::WorkspaceClosedEvent{});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetStatus() == nullptr);
	CHECK(model.GetBranches() == nullptr);
}

TEST_CASE("A file-change event outside a workspace refreshes nothing")
{
	FakePort port;
	Core::EventBus events;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SubscribeTo(events);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastMaxDebounceSeconds});

	CHECK(port.StatusCalls == 0);
}

TEST_CASE("Opening a different folder drops the previous folder's snapshot")
{
	FakePort port;
	Core::EventBus events;
	port.Status.Entries.push_back(ModifiedStatusEntry("a.txt"));

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestStatus();
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	REQUIRE(model.GetStatus() != nullptr);

	events.Publish(Graphite::WorkspaceOpenedEvent{"C:/other"});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetStatus() == nullptr);
	CHECK(model.GetBranches() == nullptr);
}

TEST_CASE("A sustained stream of change events still refreshes once the cap is reached")
{
	FakePort port;
	Core::EventBus events;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	for (int eventNumber = 0; eventNumber < kStreamEventsBeforeCap; ++eventNumber)
	{
		events.Publish(Graphite::WorkspaceFilesChangedEvent{});
		model.OnUpdate(Core::Timestep{kStreamStepSeconds});
	}

	CHECK(port.StatusCalls == 0);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{});
	model.OnUpdate(Core::Timestep{kStreamStepSeconds});

	CHECK(port.StatusCalls == 1);
}

TEST_CASE("A log is not requested without an open workspace")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};

	model.RequestLog(Core::LogRequest{});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.LogCalls == 0);
	CHECK(model.GetLog() == nullptr);
}

TEST_CASE("Git window: marked commits land with the log they were asked with")
{
	FakePort port;
	port.Log.Commits.resize(2);

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Graphite::GitModel::MarkedCommitsRequest newer;
	newer.Query.Ref = "refs/heads/develop";
	newer.Query.Exclude = OidWithFirstByte(7);
	newer.Label = "develop";
	newer.Kind = Graphite::GitModel::MarkedKind::Newer;

	model.RequestLog(Core::LogRequest{}, newer);

	CHECK(model.GetLog() == nullptr);
	CHECK(model.GetMarkedCommits() == nullptr);
	CHECK_FALSE(model.GetMarkedKind().has_value());

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetLog() != nullptr);
	REQUIRE(model.GetMarkedCommits() != nullptr);
	CHECK(model.GetMarkedCommits()->Commits.size() == 2);
	CHECK(model.GetMarkedLabel() == "develop");
	CHECK(model.GetMarkedKind() == std::optional<Graphite::GitModel::MarkedKind>{Graphite::GitModel::MarkedKind::Newer});

	CHECK(port.LogCalls == 2);
	REQUIRE(port.LastLogRequest.has_value());
	CHECK(port.LastLogRequest->Exclude == newer.Query.Exclude);

	// The next log asks for the branch's own commits instead; until it lands the newer ones stay with their page.
	Graphite::GitModel::MarkedCommitsRequest own;
	own.Label = "develop";
	own.Kind = Graphite::GitModel::MarkedKind::Own;

	model.RequestLog(Core::LogRequest{}, own);
	CHECK(model.GetMarkedKind() == std::optional<Graphite::GitModel::MarkedKind>{Graphite::GitModel::MarkedKind::Newer});

	model.OnUpdate(Core::Timestep{kFrameSeconds});
	CHECK(model.GetMarkedKind() == std::optional<Graphite::GitModel::MarkedKind>{Graphite::GitModel::MarkedKind::Own});
}

TEST_CASE("A log asked without marked commits asks for none")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestLog(Core::LogRequest{});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.LogCalls == 1);
	CHECK(model.GetMarkedCommits() == nullptr);
	CHECK_FALSE(model.GetMarkedKind().has_value());
}

TEST_CASE("The log on screen names the request it answers")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Core::LogRequest request;
	request.Ref = "refs/heads/topic";
	request.Filter.Text = "encoding";

	model.RequestLog(request);

	CHECK_FALSE(model.GetShownLogRequest().has_value());

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetShownLogRequest().has_value());
	CHECK(model.GetShownLogRequest()->Ref == std::optional<std::string>{"refs/heads/topic"});
	CHECK(model.GetShownLogRequest()->Filter.Text == "encoding");
}

TEST_CASE("A failed log keeps the request of the page still on screen")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Core::LogRequest first;
	first.Ref = "refs/heads/first";
	model.RequestLog(first);
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	port.FailLog = true;

	Core::LogRequest second;
	second.Ref = "refs/heads/second";
	model.RequestLog(second);
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetShownLogRequest().has_value());
	CHECK(model.GetShownLogRequest()->Ref == std::optional<std::string>{"refs/heads/first"});
}

TEST_CASE("A log ask is answered by its page or by its failure")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestLog(Core::LogRequest{});
	const std::uint64_t answeredByPage = model.GetLogGeneration();

	CHECK_FALSE(model.IsLogAnswered(answeredByPage));
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	CHECK(model.IsLogAnswered(answeredByPage));

	port.FailLog = true;

	model.RequestLog(Core::LogRequest{});
	const std::uint64_t answeredByFailure = model.GetLogGeneration();

	CHECK_FALSE(model.IsLogAnswered(answeredByFailure));
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	CHECK(model.IsLogAnswered(answeredByFailure));
}

TEST_CASE("Marked commits are not requested without an open workspace")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};

	model.RequestLog(Core::LogRequest{}, Graphite::GitModel::MarkedCommitsRequest{});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.LogCalls == 0);
	CHECK(model.GetMarkedCommits() == nullptr);
}

TEST_CASE("Marked commits stay until the log asked without them lands, so the rows they gave do not change early")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Graphite::GitModel::MarkedCommitsRequest own;
	own.Label = "main";

	model.RequestLog(Core::LogRequest{}, own);
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	REQUIRE(model.GetMarkedCommits() != nullptr);

	model.RequestLog(Core::LogRequest{});
	CHECK(model.GetMarkedCommits() != nullptr);

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetMarkedCommits() == nullptr);
	CHECK(model.GetMarkedLabel().empty());
	CHECK_FALSE(model.GetMarkedKind().has_value());
}

TEST_CASE("A refresh asks the marked commits again with the log")
{
	Core::EventBus events;
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	Graphite::GitModel::MarkedCommitsRequest own;
	own.Query.Exclude = OidWithFirstByte(7);

	model.RequestLog(Core::LogRequest{}, own);
	model.OnUpdate(Core::Timestep{kNoSeconds});
	REQUIRE(port.LogCalls == 2);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastDebounceSeconds});

	CHECK(port.LogCalls == 4);
	REQUIRE(port.LastLogRequest.has_value());
	CHECK(port.LastLogRequest->Exclude == own.Query.Exclude);
}

TEST_CASE("Closing the workspace forgets the marked commits")
{
	Core::EventBus events;
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestLog(Core::LogRequest{}, Graphite::GitModel::MarkedCommitsRequest{});
	model.OnUpdate(Core::Timestep{kNoSeconds});
	REQUIRE(model.GetMarkedCommits() != nullptr);

	events.Publish(Graphite::WorkspaceClosedEvent{});

	CHECK(model.GetMarkedCommits() == nullptr);
}

TEST_CASE("A requested diff becomes readable after the next update")
{
	FakePort port;

	Core::FileDiff file;
	file.NewPath = "a.txt";
	file.Change = Core::FileChange::Modified;
	port.Diff.Files.push_back(file);

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Core::DiffRequest request;
	request.Target = Core::DiffTarget::WorkTreeToHead;
	model.RequestWorkingTreeDiff(request);

	CHECK(model.GetWorkingTreeDiff(Core::DiffRequest{}) == nullptr);

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetWorkingTreeDiff(Core::DiffRequest{}) != nullptr);
	REQUIRE(model.GetWorkingTreeDiff(Core::DiffRequest{})->Files.size() == 1);
	CHECK(model.GetWorkingTreeDiff(Core::DiffRequest{})->Files.front().NewPath == std::filesystem::path("a.txt"));
}

TEST_CASE("A request made while one is already in flight is not resubmitted")
{
	FakePort port;

	Core::FileDiff stale;
	stale.NewPath = "stale.txt";
	port.Diff.Files.push_back(stale);

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.RequestWorkingTreeDiff(Core::DiffRequest{});

	port.Diff.Files.clear();
	Core::FileDiff fresh;
	fresh.NewPath = "fresh.txt";
	port.Diff.Files.push_back(fresh);
	model.RequestWorkingTreeDiff(Core::DiffRequest{});

	CHECK(port.DiffCalls == 1);

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetWorkingTreeDiff(Core::DiffRequest{}) != nullptr);
	REQUIRE(model.GetWorkingTreeDiff(Core::DiffRequest{})->Files.size() == 1);
	CHECK(model.GetWorkingTreeDiff(Core::DiffRequest{})->Files.front().NewPath == std::filesystem::path("stale.txt"));
}

TEST_CASE("A failed diff keeps the last good one")
{
	FakePort port;

	Core::FileDiff file;
	file.NewPath = "a.txt";
	port.Diff.Files.push_back(file);

	Core::EventBus events;
	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);
	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	port.FailDiff = true;
	events.Publish(Graphite::WorkspaceFilesChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastDebounceSeconds});
	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	REQUIRE(port.DiffCalls == 2);
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetWorkingTreeDiff(Core::DiffRequest{}) != nullptr);
	CHECK(model.GetWorkingTreeDiff(Core::DiffRequest{})->Files.size() == 1);
}

TEST_CASE("Closing the workspace drops the diff too")
{
	FakePort port;
	Core::EventBus events;

	Core::FileDiff file;
	file.NewPath = "a.txt";
	port.Diff.Files.push_back(file);

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestWorkingTreeDiff(Core::DiffRequest{});
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	REQUIRE(model.GetWorkingTreeDiff(Core::DiffRequest{}) != nullptr);

	events.Publish(Graphite::WorkspaceClosedEvent{});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetWorkingTreeDiff(Core::DiffRequest{}) == nullptr);
}

namespace {

	Core::Diff DiffNaming(const char* path)
	{
		Core::Diff diff;

		Core::FileDiff file;
		file.NewPath = path;
		file.Change = Core::FileChange::Modified;
		diff.Files.push_back(std::move(file));

		return diff;
	}
}

TEST_CASE("A commit diff is not requested without an open workspace")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, RunJobsInline()};

	model.RequestDiffFor(OidWithFirstByte(1));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.DiffCalls == 0);
	CHECK(model.GetDiffFor(OidWithFirstByte(1)) == nullptr);
}

TEST_CASE("A commit's diff is readable after the next update, under that commit's id")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(OidWithFirstByte(1));
	CHECK(model.GetDiffFor(OidWithFirstByte(1)) == nullptr);

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetDiffFor(OidWithFirstByte(1)) != nullptr);
	CHECK(model.GetDiffFor(OidWithFirstByte(1))->Files.size() == 1);
}

TEST_CASE("A commit nobody asked about has no diff")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(OidWithFirstByte(1));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetDiffFor(OidWithFirstByte(2)) == nullptr);
}

TEST_CASE("Two commits' diffs are held at the same time")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	port.Diff = DiffNaming("first.txt");
	model.RequestDiffFor(OidWithFirstByte(1));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	port.Diff = DiffNaming("second.txt");
	model.RequestDiffFor(OidWithFirstByte(2));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetDiffFor(OidWithFirstByte(1)) != nullptr);
	REQUIRE(model.GetDiffFor(OidWithFirstByte(2)) != nullptr);
	CHECK(model.GetDiffFor(OidWithFirstByte(1))->Files.front().NewPath == std::filesystem::path("first.txt"));
	CHECK(model.GetDiffFor(OidWithFirstByte(2))->Files.front().NewPath == std::filesystem::path("second.txt"));
}

TEST_CASE("Asking again for a commit already held does not ask the port again")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(OidWithFirstByte(1));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	const int callsAfterFirstRequest = port.DiffCalls;

	model.RequestDiffFor(OidWithFirstByte(1));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.DiffCalls == callsAfterFirstRequest);
}

TEST_CASE("Asking again while the first answer is still in flight does not ask twice")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(OidWithFirstByte(1));
	model.RequestDiffFor(OidWithFirstByte(1));

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.DiffCalls == 1);
}

TEST_CASE("A failed diff for a commit leaves no snapshot and reports the error")
{
	FakePort port;
	port.FailDiff = true;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(OidWithFirstByte(1));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetDiffFor(OidWithFirstByte(1)) == nullptr);
	CHECK(model.GetDiffFailure(OidWithFirstByte(1)).has_value());
}

TEST_CASE("A failed diff is not asked for again on its own")
{
	FakePort port;
	port.FailDiff = true;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(OidWithFirstByte(1));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	const int callsAfterFirstRequest = port.DiffCalls;

	model.RequestDiffFor(OidWithFirstByte(1));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.DiffCalls == callsAfterFirstRequest);
	CHECK(model.GetDiffFor(OidWithFirstByte(1)) == nullptr);
}

TEST_CASE("A failed diff can be asked for again once forgotten")
{
	FakePort port;
	port.FailDiff = true;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(OidWithFirstByte(1));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	port.FailDiff = false;
	port.Diff = DiffNaming("a.txt");

	model.ForgetFailedDiffFor(OidWithFirstByte(1));
	model.RequestDiffFor(OidWithFirstByte(1));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetDiffFor(OidWithFirstByte(1)) != nullptr);
}

TEST_CASE("A commit's diff reports failed only while a failed answer is held")
{
	FakePort port;
	port.FailDiff = true;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	CHECK_FALSE(model.IsDiffFailed(OidWithFirstByte(1)));
	model.RequestDiffFor(OidWithFirstByte(1));
	CHECK_FALSE(model.IsDiffFailed(OidWithFirstByte(1)));

	model.OnUpdate(Core::Timestep{kFrameSeconds});
	CHECK(model.IsDiffFailed(OidWithFirstByte(1)));

	model.ForgetFailedDiffFor(OidWithFirstByte(1));
	CHECK_FALSE(model.IsDiffFailed(OidWithFirstByte(1)));

	port.FailDiff = false;
	port.Diff = DiffNaming("a.txt");
	model.RequestDiffFor(OidWithFirstByte(1));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK_FALSE(model.IsDiffFailed(OidWithFirstByte(1)));
	CHECK(model.GetDiffFor(OidWithFirstByte(1)) != nullptr);
}

TEST_CASE("Forgetting a failed diff clears the error it left behind")
{
	FakePort port;
	port.FailDiff = true;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(OidWithFirstByte(1));
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	REQUIRE(model.GetDiffFailure(OidWithFirstByte(1)).has_value());

	model.ForgetFailedDiffFor(OidWithFirstByte(1));

	CHECK(!model.GetDiffFailure(OidWithFirstByte(1)).has_value());
}

TEST_CASE("Forgetting a successful diff leaves it cached")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(OidWithFirstByte(1));
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	REQUIRE(model.GetDiffFor(OidWithFirstByte(1)) != nullptr);

	const int callsAfterFirstRequest = port.DiffCalls;

	model.ForgetFailedDiffFor(OidWithFirstByte(1));

	REQUIRE(model.GetDiffFor(OidWithFirstByte(1)) != nullptr);

	model.RequestDiffFor(OidWithFirstByte(1));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.DiffCalls == callsAfterFirstRequest);
}

TEST_CASE("The cache holds a bounded number of commits")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	for (std::size_t commitNumber = 0; commitNumber < Graphite::GitModel::kCachedDiffs + kRequestsPastBound; ++commitNumber)
	{
		model.RequestDiffFor(OidWithFirstByte(static_cast<std::uint8_t>(commitNumber)));
		model.OnUpdate(Core::Timestep{kFrameSeconds});
	}

	CHECK(model.GetDiffFor(OidWithFirstByte(kOldestCommitFirstByte)) == nullptr);
	CHECK(model.GetDiffFor(OidWithFirstByte(1)) == nullptr);
	CHECK(model.GetDiffFor(OidWithFirstByte(2)) == nullptr);
	CHECK(model.GetDiffFor(OidWithFirstByte(3)) == nullptr);
	CHECK(model.GetDiffFor(OidWithFirstByte(4)) != nullptr);
	CHECK(model.GetDiffFor(OidWithFirstByte(static_cast<std::uint8_t>(Graphite::GitModel::kCachedDiffs + 3))) != nullptr);
}

TEST_CASE("Reading a commit's diff protects it from eviction")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	for (std::size_t commitNumber = 0; commitNumber < Graphite::GitModel::kCachedDiffs; ++commitNumber)
	{
		model.RequestDiffFor(OidWithFirstByte(static_cast<std::uint8_t>(commitNumber)));
		model.OnUpdate(Core::Timestep{kFrameSeconds});
	}

	REQUIRE(model.GetDiffFor(OidWithFirstByte(kOldestCommitFirstByte)) != nullptr);

	model.RequestDiffFor(OidWithFirstByte(static_cast<std::uint8_t>(Graphite::GitModel::kCachedDiffs)));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetDiffFor(OidWithFirstByte(kOldestCommitFirstByte)) != nullptr);
	CHECK(model.GetDiffFor(OidWithFirstByte(1)) == nullptr);
}

TEST_CASE("Closing the workspace drops every cached diff")
{
	FakePort port;
	Core::EventBus events;
	port.Diff = DiffNaming("a.txt");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestDiffFor(OidWithFirstByte(1));
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	REQUIRE(model.GetDiffFor(OidWithFirstByte(1)) != nullptr);

	events.Publish(Graphite::WorkspaceClosedEvent{});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetDiffFor(OidWithFirstByte(1)) == nullptr);
}

TEST_CASE("A stale answer from before a clear does not overwrite a fresh one after it")
{
	FakePort port;
	port.Diff = DiffNaming("before.txt");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestDiffFor(OidWithFirstByte(1));

	model.Clear();

	port.Diff = DiffNaming("after.txt");

	model.RequestDiffFor(OidWithFirstByte(1));

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetDiffFor(OidWithFirstByte(1)) != nullptr);
	CHECK(model.GetDiffFor(OidWithFirstByte(1))->Files.front().NewPath == std::filesystem::path("after.txt"));
}

namespace {

	Graphite::FileTexts TextsNaming(const char* text)
	{
		Graphite::FileTexts texts;
		texts.Old = text;
		texts.New = text;
		texts.HasOld = true;
		texts.HasNew = true;
		return texts;
	}
}

TEST_CASE("A requested file's texts become readable after the next update")
{
	FakePort port;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");
	CHECK(model.GetTextsFor(OidWithFirstByte(1), "a.txt") == nullptr);

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetTextsFor(OidWithFirstByte(1), "a.txt") != nullptr);
	CHECK(model.GetTextsFor(OidWithFirstByte(1), "a.txt")->Old == "a");
	CHECK(model.GetTextsFor(OidWithFirstByte(1), "a.txt")->New == "a");
}

TEST_CASE("A commit nobody asked about has no texts")
{
	FakePort port;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetTextsFor(OidWithFirstByte(2), "a.txt") == nullptr);
}

TEST_CASE("Two commits' texts are held at the same time")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	port.Texts = TextsNaming("first");
	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	port.Texts = TextsNaming("second");
	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(2), "a.txt");
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetTextsFor(OidWithFirstByte(1), "a.txt") != nullptr);
	REQUIRE(model.GetTextsFor(OidWithFirstByte(2), "a.txt") != nullptr);
	CHECK(model.GetTextsFor(OidWithFirstByte(1), "a.txt")->Old == "first");
	CHECK(model.GetTextsFor(OidWithFirstByte(2), "a.txt")->Old == "second");
}

TEST_CASE("Asking again for texts already held does not ask the port again")
{
	FakePort port;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	const int callsAfterFirstRequest = port.TextCalls;

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.TextCalls == callsAfterFirstRequest);
}

TEST_CASE("Asking again for texts while the first answer is still in flight does not ask twice")
{
	FakePort port;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");
	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.TextCalls == 1);
}

TEST_CASE("A failed texts request for a file leaves no snapshot and reports the error")
{
	FakePort port;
	port.FailTexts = true;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetTextsFor(OidWithFirstByte(1), "a.txt") == nullptr);
	CHECK(model.GetTextsFailure(OidWithFirstByte(1), "a.txt").has_value());
}

TEST_CASE("A failed texts request is not asked for again on its own")
{
	FakePort port;
	port.FailTexts = true;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	const int callsAfterFirstRequest = port.TextCalls;

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.TextCalls == callsAfterFirstRequest);
	CHECK(model.GetTextsFor(OidWithFirstByte(1), "a.txt") == nullptr);
}

TEST_CASE("The texts cache holds a bounded number of files")
{
	FakePort port;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	for (std::size_t fileNumber = 0; fileNumber < Graphite::GitModel::kCachedTexts + kRequestsPastBound; ++fileNumber)
	{
		RequestTextsOfUnrenamedFile(model, OidWithFirstByte(static_cast<std::uint8_t>(fileNumber)), "a.txt");
		model.OnUpdate(Core::Timestep{kFrameSeconds});
	}

	CHECK(model.GetTextsFor(OidWithFirstByte(kOldestCommitFirstByte), "a.txt") == nullptr);
	CHECK(model.GetTextsFor(OidWithFirstByte(1), "a.txt") == nullptr);
	CHECK(model.GetTextsFor(OidWithFirstByte(2), "a.txt") == nullptr);
	CHECK(model.GetTextsFor(OidWithFirstByte(3), "a.txt") == nullptr);
	CHECK(model.GetTextsFor(OidWithFirstByte(4), "a.txt") != nullptr);
	CHECK(model.GetTextsFor(OidWithFirstByte(static_cast<std::uint8_t>(Graphite::GitModel::kCachedTexts + 3)),
							"a.txt") != nullptr);
}

TEST_CASE("Closing the workspace drops every cached file's texts")
{
	FakePort port;
	Core::EventBus events;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	REQUIRE(model.GetTextsFor(OidWithFirstByte(1), "a.txt") != nullptr);

	events.Publish(Graphite::WorkspaceClosedEvent{});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetTextsFor(OidWithFirstByte(1), "a.txt") == nullptr);
}

TEST_CASE("A file's texts are not requested without an open workspace")
{
	FakePort port;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, RunJobsInline()};

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.TextCalls == 0);
	CHECK(model.GetTextsFor(OidWithFirstByte(1), "a.txt") == nullptr);
}

TEST_CASE("Reading a file's texts protects it from eviction")
{
	FakePort port;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	for (std::size_t fileNumber = 0; fileNumber < Graphite::GitModel::kCachedTexts; ++fileNumber)
	{
		RequestTextsOfUnrenamedFile(model, OidWithFirstByte(static_cast<std::uint8_t>(fileNumber)), "a.txt");
		model.OnUpdate(Core::Timestep{kFrameSeconds});
	}

	REQUIRE(model.GetTextsFor(OidWithFirstByte(kOldestCommitFirstByte), "a.txt") != nullptr);

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(static_cast<std::uint8_t>(Graphite::GitModel::kCachedTexts)), "a.txt");
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetTextsFor(OidWithFirstByte(kOldestCommitFirstByte), "a.txt") != nullptr);
	CHECK(model.GetTextsFor(OidWithFirstByte(1), "a.txt") == nullptr);
}

TEST_CASE("A stale texts answer from before a clear does not overwrite a fresh one after it")
{
	FakePort port;
	port.Texts = TextsNaming("before");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");

	model.Clear();

	port.Texts = TextsNaming("after");

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetTextsFor(OidWithFirstByte(1), "a.txt") != nullptr);
	CHECK(model.GetTextsFor(OidWithFirstByte(1), "a.txt")->Old == "after");
}

TEST_CASE("A failed texts request can be asked for again once forgotten")
{
	FakePort port;
	port.FailTexts = true;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	port.FailTexts = false;
	port.Texts = TextsNaming("a");

	model.ForgetFailedTextsFor(OidWithFirstByte(1), "a.txt");
	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetTextsFor(OidWithFirstByte(1), "a.txt") != nullptr);
}

TEST_CASE("Forgetting a failed texts request clears the error it left behind")
{
	FakePort port;
	port.FailTexts = true;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	REQUIRE(model.GetTextsFailure(OidWithFirstByte(1), "a.txt").has_value());

	model.ForgetFailedTextsFor(OidWithFirstByte(1), "a.txt");

	CHECK(!model.GetTextsFailure(OidWithFirstByte(1), "a.txt").has_value());
}

TEST_CASE("Forgetting a successful texts request leaves it cached")
{
	FakePort port;
	port.Texts = TextsNaming("a");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	REQUIRE(model.GetTextsFor(OidWithFirstByte(1), "a.txt") != nullptr);

	const int callsAfterFirstRequest = port.TextCalls;

	model.ForgetFailedTextsFor(OidWithFirstByte(1), "a.txt");

	REQUIRE(model.GetTextsFor(OidWithFirstByte(1), "a.txt") != nullptr);

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "a.txt");
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.TextCalls == callsAfterFirstRequest);
}

namespace {

	Core::FileDiff DeletedFile(const char* path)
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Deleted;
		file.OldPath = path;
		return file;
	}

	Core::FileDiff AddedFile(const char* path)
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Added;
		file.NewPath = path;
		return file;
	}

	Core::FileDiff ModifiedFile(const char* path)
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Modified;
		file.OldPath = path;
		file.NewPath = path;
		return file;
	}

	Core::FileDiff RenamedFile(const char* oldPath, const char* newPath)
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Renamed;
		file.OldPath = oldPath;
		file.NewPath = newPath;
		return file;
	}

	Graphite::FileTexts OldSideOnly(const char* text)
	{
		Graphite::FileTexts texts;
		texts.Old = text;
		texts.HasOld = true;
		return texts;
	}

	Graphite::FileTexts NewSideOnly(const char* text)
	{
		Graphite::FileTexts texts;
		texts.New = text;
		texts.HasNew = true;
		return texts;
	}

	void UpdateBindingAndApplyDeliveries(Graphite::FileTextsBinding& binding, Graphite::GitModel& model,
										 const Core::Oid& commit, const Core::FileDiff& file)
	{
		binding.Update(model, commit, file);
		model.OnUpdate(Core::Timestep{kFrameSeconds});
		binding.Update(model, commit, file);
	}
}

TEST_CASE("Two files deleted in one commit do not share one texts entry")
{
	FakePort port;
	port.TextsByPaths[FakePort::PathsKey("a.cpp", kAbsentPath)] = OldSideOnly("a");
	port.TextsByPaths[FakePort::PathsKey("b.cpp", kAbsentPath)] = OldSideOnly("b");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;

	UpdateBindingAndApplyDeliveries(binding, model, OidWithFirstByte(1), DeletedFile("a.cpp"));

	REQUIRE(binding.Texts != nullptr);
	CHECK(binding.Texts->Old == "a");

	UpdateBindingAndApplyDeliveries(binding, model, OidWithFirstByte(1), DeletedFile("b.cpp"));

	REQUIRE(binding.Texts != nullptr);
	CHECK(binding.Texts->Old == "b");

	CHECK(model.GetTextsFor(OidWithFirstByte(1), "a.cpp") != nullptr);
	CHECK(model.GetTextsFor(OidWithFirstByte(1), "b.cpp") != nullptr);
	CHECK(model.GetTextsFor(OidWithFirstByte(1), kAbsentPath) == nullptr);
}

TEST_CASE("A deleted file and an added file in one commit do not collide")
{
	FakePort port;
	port.TextsByPaths[FakePort::PathsKey("gone.cpp", kAbsentPath)] = OldSideOnly("gone");
	port.TextsByPaths[FakePort::PathsKey(kAbsentPath, "fresh.cpp")] = NewSideOnly("fresh");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;

	UpdateBindingAndApplyDeliveries(binding, model, OidWithFirstByte(1), DeletedFile("gone.cpp"));

	REQUIRE(binding.Texts != nullptr);
	CHECK(binding.Texts->HasOld);
	CHECK(binding.Texts->Old == "gone");

	UpdateBindingAndApplyDeliveries(binding, model, OidWithFirstByte(1), AddedFile("fresh.cpp"));

	REQUIRE(binding.Texts != nullptr);
	CHECK(binding.Texts->HasNew);
	CHECK(binding.Texts->New == "fresh");
}

TEST_CASE("A renamed file's texts are keyed by its new path")
{
	FakePort port;
	port.TextsByPaths[FakePort::PathsKey("old.cpp", "new.cpp")] = TextsNaming("renamed");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;

	UpdateBindingAndApplyDeliveries(binding, model, OidWithFirstByte(1), RenamedFile("old.cpp", "new.cpp"));

	REQUIRE(binding.Texts != nullptr);
	CHECK(binding.Key == std::filesystem::path{"new.cpp"});
	CHECK(model.GetTextsFor(OidWithFirstByte(1), "new.cpp") != nullptr);
	CHECK(model.GetTextsFor(OidWithFirstByte(1), "old.cpp") == nullptr);
}

TEST_CASE("A binary file's texts are never fetched")
{
	FakePort port;
	port.Texts = TextsNaming("bytes");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Core::FileDiff file = ModifiedFile("image.png");
	file.IsBinary = true;

	Graphite::FileTextsBinding binding;

	UpdateBindingAndApplyDeliveries(binding, model, OidWithFirstByte(1), file);

	CHECK(port.TextCalls == 0);
	CHECK(binding.Texts == nullptr);
	CHECK(binding.Tokens.Old.empty());
	CHECK(binding.Tokens.New.empty());
	CHECK(model.GetTextsFor(OidWithFirstByte(1), "image.png") == nullptr);
}

TEST_CASE("Turning to a binary file drops the texts the previous one left")
{
	FakePort port;
	port.Texts = TextsNaming("text");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;

	UpdateBindingAndApplyDeliveries(binding, model, OidWithFirstByte(1), ModifiedFile("a.cpp"));
	REQUIRE(binding.Texts != nullptr);

	Core::FileDiff binary = ModifiedFile("image.png");
	binary.IsBinary = true;

	UpdateBindingAndApplyDeliveries(binding, model, OidWithFirstByte(1), binary);

	CHECK(binding.Texts == nullptr);
	CHECK(port.TextCalls == 1);
}

TEST_CASE("Moving to another file forgets the one just left, if its read failed")
{
	FakePort port;
	port.Texts = TextsNaming("a");
	port.FailTexts = true;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;

	UpdateBindingAndApplyDeliveries(binding, model, OidWithFirstByte(1), ModifiedFile("a.cpp"));
	REQUIRE(binding.Texts == nullptr);
	REQUIRE(port.TextCalls == 1);

	binding.Update(model, OidWithFirstByte(1), ModifiedFile("a.cpp"));
	CHECK(port.TextCalls == 1);

	UpdateBindingAndApplyDeliveries(binding, model, OidWithFirstByte(1), ModifiedFile("b.cpp"));

	port.FailTexts = false;

	UpdateBindingAndApplyDeliveries(binding, model, OidWithFirstByte(1), ModifiedFile("a.cpp"));

	REQUIRE(binding.Texts != nullptr);
	CHECK(binding.Texts->Old == "a");
}

TEST_CASE("Each side is lexed with its own path's language")
{
	FakePort port;
	port.TextsByPaths[FakePort::PathsKey("foo.txt", "foo.cpp")] = TextsNaming("int x;");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;

	UpdateBindingAndApplyDeliveries(binding, model, OidWithFirstByte(1), RenamedFile("foo.txt", "foo.cpp"));

	REQUIRE(binding.Texts != nullptr);

	CHECK(binding.Tokens.Old.empty());
	CHECK_FALSE(binding.Tokens.New.empty());
}

TEST_CASE("The tokens are not rebuilt while the texts stay the same")
{
	FakePort port;
	port.Texts = TextsNaming("int x;");

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;

	UpdateBindingAndApplyDeliveries(binding, model, OidWithFirstByte(1), ModifiedFile("a.cpp"));

	REQUIRE(binding.Texts != nullptr);
	REQUIRE_FALSE(binding.Tokens.New.empty());

	const Graphite::FileTexts* held = binding.Texts.get();
	const std::vector<Graphite::Token>* heldNewTokens = binding.Tokens.New.data();

	binding.Update(model, OidWithFirstByte(1), ModifiedFile("a.cpp"));
	binding.Update(model, OidWithFirstByte(1), ModifiedFile("a.cpp"));

	CHECK(binding.Texts.get() == held);
	CHECK(binding.Tokens.New.data() == heldNewTokens);
}

namespace {

	constexpr const char* kCSharpWidget = "class Widget { }";

	bool HasTokenOfKind(const std::vector<Graphite::Token>& tokens, Graphite::TokenKind kind)
	{
		for (const Graphite::Token& token : tokens)
		{
			if (token.Kind == kind)
			{
				return true;
			}
		}

		return false;
	}
}

TEST_CASE("A commit's C# texts arrive from the model coloured by the C# grammar")
{
	FakePort port;
	port.Texts = TextsNaming(kCSharpWidget);

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	RequestTextsOfUnrenamedFile(model, OidWithFirstByte(1), "Widget.cs");
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	const Core::Ref<const Graphite::FileTexts> texts = model.GetTextsFor(OidWithFirstByte(1), "Widget.cs");

	REQUIRE(texts != nullptr);
	REQUIRE(texts->Tokens.Old.size() == 1);
	REQUIRE(texts->Tokens.New.size() == 1);

	CHECK(HasTokenOfKind(texts->Tokens.Old.front(), Graphite::TokenKind::SemanticType));
	CHECK(HasTokenOfKind(texts->Tokens.New.front(), Graphite::TokenKind::SemanticType));
}

TEST_CASE("A commit's C++ texts carry exactly the tokens ColourFileText gives them")
{
	FakePort port;

	Graphite::FileTexts texts;
	texts.Old = "/* opened\nclosed */ int x;\n";
	texts.New = "int y = 2;\n#define Z 3\n";
	texts.HasOld = true;
	texts.HasNew = true;
	port.TextsByPaths[FakePort::PathsKey("old.cpp", "new.cpp")] = texts;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestTextsFor(OidWithFirstByte(1), "new.cpp", "old.cpp", "new.cpp");
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	const Core::Ref<const Graphite::FileTexts> delivered = model.GetTextsFor(OidWithFirstByte(1), "new.cpp");

	REQUIRE(delivered != nullptr);
	CHECK_FALSE(delivered->Tokens.Old.empty());
	CHECK_FALSE(delivered->Tokens.New.empty());
	CHECK(delivered->Tokens.Old == Graphite::ColourFileText(texts.Old, "old.cpp"));
	CHECK(delivered->Tokens.New == Graphite::ColourFileText(texts.New, "new.cpp"));
}

TEST_CASE("A side a commit does not have arrives with no tokens")
{
	FakePort port;
	port.TextsByPaths[FakePort::PathsKey(kAbsentPath, "Widget.cs")] = NewSideOnly(kCSharpWidget);

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestTextsFor(OidWithFirstByte(1), "Widget.cs", kAbsentPath, "Widget.cs");
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	const Core::Ref<const Graphite::FileTexts> texts = model.GetTextsFor(OidWithFirstByte(1), "Widget.cs");

	REQUIRE(texts != nullptr);
	CHECK(texts->Tokens.Old.empty());
	CHECK_FALSE(texts->Tokens.New.empty());
}

TEST_CASE("A working-tree diff's C# text arrives from the model coloured by the C# grammar")
{
	FakePort port;

	Core::FileDiff file;
	file.OldPath = "Widget.cs";
	file.NewPath = "Widget.cs";
	port.Diff.Files.push_back(file);

	port.WorkingTreeTexts = TextsNaming(kCSharpWidget);

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	const Core::DiffRequest request{.Target = Core::DiffTarget::WorkTreeToIndex, .File = "Widget.cs"};
	model.RequestWorkingTreeDiff(request);
	model.OnUpdate(Core::Timestep{kNoSeconds});

	const Core::Ref<const Graphite::FileTexts> texts = model.GetWorkingTreeTexts(request);

	REQUIRE(texts != nullptr);
	REQUIRE(texts->Tokens.New.size() == 1);
	CHECK(HasTokenOfKind(texts->Tokens.New.front(), Graphite::TokenKind::SemanticType));
	CHECK(texts->Tokens.Old == Graphite::ColourFileText(kCSharpWidget, "Widget.cs"));
}

TEST_CASE("The binding hands on the tokens its texts arrived with")
{
	FakePort port;
	port.Texts = TextsNaming(kCSharpWidget);

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Graphite::FileTextsBinding binding;

	UpdateBindingAndApplyDeliveries(binding, model, OidWithFirstByte(1), ModifiedFile("Widget.cs"));

	REQUIRE(binding.Texts != nullptr);
	CHECK_FALSE(binding.Tokens.New.empty());
	CHECK(binding.Tokens.Old == binding.Texts->Tokens.Old);
	CHECK(binding.Tokens.New == binding.Texts->Tokens.New);
}

TEST_CASE("A refresh asks about the history again, not only the status")
{
	Core::EventBus events;
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestLog(Core::LogRequest{});
	model.OnUpdate(Core::Timestep{kNoSeconds});
	REQUIRE(port.LogCalls == 1);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastDebounceSeconds});

	CHECK(port.LogCalls == 2);
}

TEST_CASE("A refresh repeats the query the view is showing, not a default one")
{
	Core::EventBus events;
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	Core::LogRequest request;
	request.Ref = "refs/heads/topic";
	request.Filter.Text = "encoding";
	request.MaxCount = 40;

	model.RequestLog(request);
	model.OnUpdate(Core::Timestep{kNoSeconds});

	events.Publish(Graphite::WorkspaceFilesChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastDebounceSeconds});

	REQUIRE(port.LogCalls == 2);

	REQUIRE(port.LastLogRequest.has_value());
	CHECK(port.LastLogRequest->Ref == request.Ref);
	CHECK(port.LastLogRequest->Filter.Text == "encoding");
	CHECK(port.LastLogRequest->MaxCount == 40);
}

TEST_CASE("A refresh before anything asked for a log asks for none")
{
	Core::EventBus events;
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastDebounceSeconds});

	CHECK(port.StatusCalls == 1);
	CHECK(port.LogCalls == 0);
}

TEST_CASE("Closing the workspace forgets which query to repeat")
{
	Core::EventBus events;
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestLog(Core::LogRequest{});
	model.OnUpdate(Core::Timestep{kNoSeconds});
	REQUIRE(port.LogCalls == 1);

	events.Publish(Graphite::WorkspaceClosedEvent{});
	events.Publish(Graphite::WorkspaceOpenedEvent{"C:/other"});
	model.OnUpdate(Core::Timestep{kPastDebounceSeconds});

	CHECK(port.LogCalls == 1);
}

TEST_CASE("A commit made outside the application refreshes what is on screen")
{
	Core::EventBus events;
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestLog(Core::LogRequest{});
	model.OnUpdate(Core::Timestep{kNoSeconds});
	REQUIRE(port.LogCalls == 1);

	events.Publish(Graphite::RepositoryChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastDebounceSeconds});

	CHECK(port.StatusCalls == 1);
	CHECK(port.BranchCalls == 1);
	CHECK(port.LogCalls == 2);
}

TEST_CASE("A repository change outside a workspace refreshes nothing")
{
	Core::EventBus events;
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SubscribeTo(events);

	events.Publish(Graphite::RepositoryChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastDebounceSeconds});

	CHECK(port.StatusCalls == 0);
}

TEST_CASE("A working-tree diff brings its file's text with it")
{
	FakePort port;

	Core::FileDiff file;
	file.OldPath = "old.txt";
	file.NewPath = "new.txt";
	port.Diff.Files.push_back(file);

	port.WorkingTreeTexts.HasNew = true;
	port.WorkingTreeTexts.New = "on disk";

	const Core::DiffRequest request{.Target = Core::DiffTarget::WorkTreeToIndex, .File = "new.txt"};
	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	CHECK(model.GetWorkingTreeTexts(request) == nullptr);

	model.RequestWorkingTreeDiff(request);
	model.OnUpdate(Core::Timestep{kNoSeconds});

	REQUIRE(model.GetWorkingTreeTexts(request) != nullptr);
	CHECK(model.GetWorkingTreeTexts(request)->New == "on disk");
	CHECK(port.WorkingTreeTextCalls == 1);
	CHECK(port.LastTextPaths == "old.txt|new.txt");
}

TEST_CASE("A working-tree diff that names no file reads no text")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	const Core::DiffRequest request{.Target = Core::DiffTarget::WorkTreeToIndex, .File = "gone.txt"};
	model.RequestWorkingTreeDiff(request);
	model.OnUpdate(Core::Timestep{kNoSeconds});

	CHECK(port.WorkingTreeTextCalls == 0);
	CHECK(model.GetWorkingTreeTexts(request) == nullptr);
}

TEST_CASE("An untracked listing is null until it has been asked for and applied")
{
	FakePort port;
	Core::WorkingTreeStatus listing;
	Core::FileStatus file;
	file.Path = "notes/first.md";
	file.IsUntracked = true;
	listing.Entries.push_back(file);
	port.UntrackedByDirectory[std::filesystem::path("notes")] = listing;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	CHECK(model.GetUntrackedIn("notes") == nullptr);

	model.RequestUntrackedIn("notes");
	model.OnUpdate(Core::Timestep{kNoSeconds});

	const Core::Ref<const Core::WorkingTreeStatus> held = model.GetUntrackedIn("notes");
	REQUIRE(held != nullptr);
	CHECK(held->Entries.size() == 1);
	CHECK(port.LastUntrackedDirectory == std::filesystem::path("notes"));
}

TEST_CASE("A listing already held is not asked for again")
{
	FakePort port;
	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestUntrackedIn("notes");
	model.OnUpdate(Core::Timestep{kNoSeconds});
	model.RequestUntrackedIn("notes");
	model.OnUpdate(Core::Timestep{kNoSeconds});

	CHECK(port.UntrackedCalls == 1);
}

TEST_CASE("A listing is dropped when a status no longer lists its folder")
{
	FakePort port;
	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestUntrackedIn("notes");
	model.OnUpdate(Core::Timestep{kNoSeconds});
	REQUIRE(model.GetUntrackedIn("notes") != nullptr);

	model.RequestStatus();
	model.OnUpdate(Core::Timestep{kNoSeconds});

	CHECK(model.GetUntrackedIn("notes") == nullptr);
}

TEST_CASE("A failed listing answers null and is not retried on its own")
{
	FakePort port;
	port.FailUntracked = true;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestUntrackedIn("notes");
	model.OnUpdate(Core::Timestep{kNoSeconds});
	model.RequestUntrackedIn("notes");
	model.OnUpdate(Core::Timestep{kNoSeconds});

	CHECK(model.GetUntrackedIn("notes") == nullptr);
	CHECK(port.UntrackedCalls == 1);
}

TEST_CASE("A listing stays through a status that still lists its folder, and the next ask reads it again")
{
	FakePort port;
	Core::FileStatus folder;
	folder.Path = "notes";
	folder.IsDirectory = true;
	folder.IsUntracked = true;
	port.Status.Entries = {folder};

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestUntrackedIn("notes");
	model.OnUpdate(Core::Timestep{kNoSeconds});
	REQUIRE(model.GetUntrackedIn("notes") != nullptr);

	model.RequestStatus();
	model.OnUpdate(Core::Timestep{kNoSeconds});

	CHECK(model.GetUntrackedIn("notes") != nullptr);
	CHECK(port.UntrackedCalls == 1);

	model.RequestUntrackedIn("notes");
	model.RequestUntrackedIn("notes");

	CHECK(port.UntrackedCalls == 2);
	CHECK(model.GetUntrackedIn("notes") != nullptr);
}

namespace {

	void RunHeldJobsAndDeliver(std::vector<std::function<void()>>& heldJobs, Graphite::GitModel& model)
	{
		std::vector<std::function<void()>> jobs = std::move(heldJobs);
		heldJobs.clear();

		for (const std::function<void()>& job : jobs)
		{
			job();
		}

		model.OnUpdate(Core::Timestep{kNoSeconds});
	}
}

TEST_CASE("A kept listing is not current from the status that kept it until it has been read again")
{
	FakePort port;
	Core::FileStatus folder;
	folder.Path = "notes";
	folder.IsDirectory = true;
	folder.IsUntracked = true;
	port.Status.Entries = {folder};

	std::vector<std::function<void()>> heldJobs;
	Graphite::GitModel model{port, [&heldJobs](std::function<void()> job) { heldJobs.push_back(std::move(job)); }};
	model.SetRoot("C:/repo");

	CHECK_FALSE(model.IsUntrackedListingCurrent("notes"));

	model.RequestUntrackedIn("notes");
	RunHeldJobsAndDeliver(heldJobs, model);
	CHECK(model.IsUntrackedListingCurrent("notes"));

	model.RequestStatus();
	RunHeldJobsAndDeliver(heldJobs, model);
	REQUIRE(model.GetUntrackedIn("notes") != nullptr);
	CHECK_FALSE(model.IsUntrackedListingCurrent("notes"));

	model.RequestUntrackedIn("notes");
	CHECK_FALSE(model.IsUntrackedListingCurrent("notes"));

	RunHeldJobsAndDeliver(heldJobs, model);
	CHECK(model.IsUntrackedListingCurrent("notes"));
}

TEST_CASE("A failed listing gives its reason until a status that still lists the folder lets it be asked again")
{
	FakePort port;
	port.FailUntracked = true;
	Core::FileStatus folder;
	folder.Path = "notes";
	folder.IsDirectory = true;
	folder.IsUntracked = true;
	port.Status.Entries = {folder};

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	CHECK_FALSE(model.GetUntrackedFailureIn("notes").has_value());

	model.RequestUntrackedIn("notes");
	model.OnUpdate(Core::Timestep{kNoSeconds});

	CHECK(model.GetUntrackedIn("notes") == nullptr);
	CHECK(model.GetUntrackedFailureIn("notes").has_value());

	port.FailUntracked = false;
	model.RequestStatus();
	model.OnUpdate(Core::Timestep{kNoSeconds});
	model.RequestUntrackedIn("notes");
	model.OnUpdate(Core::Timestep{kNoSeconds});

	CHECK(port.UntrackedCalls == 2);
	CHECK(model.GetUntrackedIn("notes") != nullptr);
	CHECK_FALSE(model.GetUntrackedFailureIn("notes").has_value());
}

TEST_CASE("Clearing the model forgets every listing")
{
	FakePort port;
	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestUntrackedIn("notes");
	model.OnUpdate(Core::Timestep{kNoSeconds});
	REQUIRE(model.GetUntrackedIn("notes") != nullptr);

	model.Clear();

	CHECK(model.GetUntrackedIn("notes") == nullptr);
}

TEST_CASE("A requested head becomes readable after the next update")
{
	FakePort port;
	port.Head.Kind = Core::HeadKind::Branch;
	port.Head.BranchName = "main";

	Graphite::GitModel model{port, RunJobsInline()};

	model.RequestHead();
	CHECK(model.GetHead() == nullptr);

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetHead() != nullptr);
	CHECK(model.GetHead()->BranchName == "main");
}

TEST_CASE("Git window: the repository's folder is known once HEAD is read and before the status")
{
	FakePort port;
	port.Head.Kind = Core::HeadKind::Branch;
	port.Head.BranchName = "main";
	port.WorkingDirectory = "C:/repo/";

	Graphite::GitModel model{port, RunJobsInline()};

	model.RequestHead();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetHead() != nullptr);
	CHECK(model.GetStatus() == nullptr);
	CHECK(model.GetWorkingDirectory() == std::filesystem::path{"C:/repo/"});
}

TEST_CASE("The head is refreshed whenever the status is")
{
	FakePort port;
	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Core::EventBus events;
	model.SubscribeTo(events);
	events.Publish(Graphite::RepositoryChangedEvent{});

	model.OnUpdate(Core::Timestep{kPastMaxDebounceSeconds});

	CHECK(port.StatusCalls == 1);
	CHECK(port.HeadCalls == 1);
	CHECK(model.GetHead() != nullptr);
}

TEST_CASE("Closing the folder forgets the head")
{
	FakePort port;
	Graphite::GitModel model{port, RunJobsInline()};

	model.RequestHead();
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	REQUIRE(model.GetHead() != nullptr);

	model.Clear();

	CHECK(model.GetHead() == nullptr);
	CHECK_FALSE(model.GetHeadError().has_value());
}

TEST_CASE("A failed head query keeps the last good head and reports itself")
{
	FakePort port;
	port.Head.BranchName = "main";
	Graphite::GitModel model{port, RunJobsInline()};

	model.RequestHead();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	port.FailHead = true;
	model.RequestHead();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetHead() != nullptr);
	CHECK(model.GetHead()->BranchName == "main");
	CHECK(model.GetHeadError().has_value());
}

TEST_CASE("Stashes are refreshed with the branches and forgotten on close")
{
	FakePort port;
	Core::StashEntry entry;
	entry.Message = "On main: wip";
	port.Stashes = {entry};

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Core::EventBus events;
	model.SubscribeTo(events);
	events.Publish(Graphite::RepositoryChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastMaxDebounceSeconds});

	CHECK(port.StashCalls == 1);
	REQUIRE(model.GetStashes() != nullptr);
	CHECK(model.GetStashes()->size() == 1);

	model.Clear();
	CHECK(model.GetStashes() == nullptr);
}

TEST_CASE("A stash's diff is asked for as a stash's, so its untracked files are in it")
{
	FakePort port;
	port.Diff = DiffNaming("a.txt");

	Core::StashEntry entry;
	entry.Message = "On main: wip";
	entry.Id = OidWithFirstByte(7);
	port.Stashes = {entry};

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Core::EventBus events;
	model.SubscribeTo(events);
	events.Publish(Graphite::RepositoryChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastMaxDebounceSeconds});

	REQUIRE(model.GetStashes() != nullptr);

	model.RequestDiffFor(OidWithFirstByte(7));
	model.RequestDiffFor(OidWithFirstByte(1));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.DiffTargets ==
		  std::vector<Core::DiffTarget>{Core::DiffTarget::StashToParent, Core::DiffTarget::CommitToParent});
}

TEST_CASE("Branch authors are asked once a branch list lands and read an update later")
{
	FakePort port;
	port.Branches = TrunkAndFeature(kFeatureTipByte);
	port.UserEmail = "ann@example.com";
	port.OwnAuthors = {Core::TipAuthors{.Tip = OidWithFirstByte(kFeatureTipByte),
										.Authors = {Core::Signature{.Name = "Ann", .Email = "ann@example.com"}},
										.OwnCommits = 1}};

	Graphite::GitModel model{port, RunJobsInline()};
	HideTrunkTips(model);

	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetBranchAuthors() == nullptr);

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetBranchAuthors() != nullptr);
	CHECK(model.GetBranchAuthors()->ByTip.contains(OidWithFirstByte(kFeatureTipByte)));
	CHECK(model.GetBranchAuthors()->UserEmail == std::optional<std::string>{"ann@example.com"});

	REQUIRE(port.LastOwnAuthorsRequest.has_value());
	CHECK(port.LastOwnAuthorsRequest->Hidden ==
		  std::vector<Core::Oid>{OidWithFirstByte(kMainTipByte), OidWithFirstByte(kOriginMainTipByte)});
}

TEST_CASE("Branch authors are in flight from the listing that asks them until they land")
{
	FakePort port;
	port.Branches = TrunkAndFeature(kFeatureTipByte);

	Graphite::GitModel model{port, RunJobsInline()};
	HideTrunkTips(model);

	CHECK_FALSE(model.IsBranchAuthorsInFlight());

	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.IsBranchAuthorsInFlight());

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK_FALSE(model.IsBranchAuthorsInFlight());
}

TEST_CASE("A listing whose tips did not move asks the authors nothing")
{
	FakePort port;
	port.Branches = TrunkAndFeature(kFeatureTipByte);
	port.UserEmail = "ann@example.com";

	Graphite::GitModel model{port, RunJobsInline()};
	HideTrunkTips(model);

	ListBranchesAndLandAuthors(model);
	ListBranchesAndLandAuthors(model);

	CHECK(port.OwnAuthorsCalls == 1);
}

TEST_CASE("A listing asks again while no user e-mail is known")
{
	FakePort port;
	port.Branches = TrunkAndFeature(kFeatureTipByte);

	Graphite::GitModel model{port, RunJobsInline()};
	HideTrunkTips(model);

	ListBranchesAndLandAuthors(model);

	REQUIRE(model.GetBranchAuthors() != nullptr);
	CHECK_FALSE(model.GetBranchAuthors()->UserEmail.has_value());

	port.UserEmail = "ann@example.com";
	ListBranchesAndLandAuthors(model);

	REQUIRE(model.GetBranchAuthors() != nullptr);
	CHECK(model.GetBranchAuthors()->UserEmail == std::optional<std::string>{"ann@example.com"});

	ListBranchesAndLandAuthors(model);

	CHECK(port.OwnAuthorsCalls == 2);
}

TEST_CASE("Git window: the authors wait for the hidden tips and are asked again when they change")
{
	FakePort port;
	port.Branches = TrunkAndFeature(kFeatureTipByte);

	Graphite::GitModel model{port, RunJobsInline()};

	ListBranchesAndLandAuthors(model);

	CHECK(port.OwnAuthorsCalls == 0);
	CHECK(model.GetBranchAuthors() == nullptr);

	HideTrunkTips(model);
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.OwnAuthorsCalls == 1);
	REQUIRE(port.LastOwnAuthorsRequest.has_value());
	CHECK(port.LastOwnAuthorsRequest->Hidden ==
		  std::vector<Core::Oid>{OidWithFirstByte(kMainTipByte), OidWithFirstByte(kOriginMainTipByte)});

	HideTrunkTips(model);
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.OwnAuthorsCalls == 1);

	model.SetAuthorsHidden({OidWithFirstByte(kMainTipByte)});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.OwnAuthorsCalls == 2);
	CHECK(port.LastOwnAuthorsRequest->Hidden == std::vector<Core::Oid>{OidWithFirstByte(kMainTipByte)});
}

TEST_CASE("Git window: the remotes are read with the branches and forgotten with the folder")
{
	FakePort port;
	port.Remotes = {Core::Remote{"origin", "https://example.com/a.git"}};

	Graphite::GitModel model{port, RunJobsInline()};

	CHECK(model.GetRemotes() == nullptr);

	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetRemotes() != nullptr);
	REQUIRE(model.GetRemotes()->size() == 1);
	CHECK(model.GetRemotes()->front().Name == "origin");

	model.Clear();

	CHECK(model.GetRemotes() == nullptr);
}

TEST_CASE("Git window: the last fetch's time is read with the branches and forgotten with the folder")
{
	FakePort port;
	port.Network.LastFetch = SystemSecondsAt(1'700'000'000);

	Graphite::GitModel model{port, RunJobsInline()};

	CHECK_FALSE(model.GetLastFetch().has_value());

	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetLastFetch() == port.Network.LastFetch);

	model.Clear();

	CHECK_FALSE(model.GetLastFetch().has_value());
}

TEST_CASE("Git window: a repository with no remotes has an empty listing once the read is done, not none")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};

	CHECK(model.GetRemotes() == nullptr);

	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetRemotes() != nullptr);
	CHECK(model.GetRemotes()->empty());
	CHECK_FALSE(model.GetRemotesError().has_value());
}

TEST_CASE("Git window: the user's e-mail is read with the branches")
{
	FakePort port;
	port.Branches = TrunkAndFeature(kFeatureTipByte);
	port.UserEmail = "ann@example.com";

	Graphite::GitModel model{port, RunJobsInline()};

	CHECK_FALSE(model.GetUserEmail().has_value());

	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	// Before the authors walk, which the listing only asks for, has landed.
	CHECK(model.GetBranchAuthors() == nullptr);
	CHECK(model.GetUserEmail() == std::optional<std::string>{"ann@example.com"});

	port.UserEmail.reset();
	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK_FALSE(model.GetUserEmail().has_value());

	port.UserEmail = "ann@example.com";
	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	model.Clear();

	CHECK_FALSE(model.GetUserEmail().has_value());
}

TEST_CASE("A moved tip asks the authors again")
{
	FakePort port;
	port.Branches = TrunkAndFeature(kFeatureTipByte);

	Graphite::GitModel model{port, RunJobsInline()};
	HideTrunkTips(model);

	ListBranchesAndLandAuthors(model);

	port.Branches = TrunkAndFeature(kMovedFeatureTipByte);
	ListBranchesAndLandAuthors(model);

	CHECK(port.OwnAuthorsCalls == 2);
}

TEST_CASE("Git window: a listing of the same branches keeps the snapshot it holds")
{
	FakePort port;
	port.Branches = TrunkAndFeature(kFeatureTipByte);

	Graphite::GitModel model{port, RunJobsInline()};

	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	const Core::Ref<const std::vector<Core::Branch>> first = model.GetBranches();
	REQUIRE(first != nullptr);

	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.BranchCalls == 2);
	CHECK(model.GetBranches() == first);
	CHECK_FALSE(model.GetBranchesError().has_value());
}

TEST_CASE("Git window: a listing of the same branches is a new snapshot once the clone's shallow file has changed")
{
	FakePort port;
	port.Branches = TrunkAndFeature(kFeatureTipByte);
	port.Network.Shallow = Graphite::ShallowStamp{.Exists = true, .Size = 41};

	Graphite::GitModel model{port, RunJobsInline()};

	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	const Core::Ref<const std::vector<Core::Branch>> shallow = model.GetBranches();
	REQUIRE(shallow != nullptr);

	// Deepened by a fetch, here or in a terminal: no ref moved, and the shallow file is gone.
	port.Network.Shallow = Graphite::ShallowStamp{};
	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	const Core::Ref<const std::vector<Core::Branch>> deepened = model.GetBranches();
	REQUIRE(deepened != nullptr);
	CHECK(deepened != shallow);
	CHECK(*deepened == *shallow);

	// Listed again as it is now: the snapshot is kept.
	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	CHECK(model.GetBranches() == deepened);
}

TEST_CASE("Git window: a refresh that reads the same remotes and e-mail keeps the snapshots it holds")
{
	FakePort port;
	port.Branches = TrunkAndFeature(kFeatureTipByte);
	port.Remotes = {Core::Remote{.Name = "origin", .Url = "u"}};
	port.UserEmail = std::optional<std::string>{"ann@example.com"};

	Graphite::GitModel model{port, RunJobsInline()};

	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	const Core::Ref<const std::vector<Core::Remote>> remotes = model.GetRemotes();
	const std::optional<std::string>* const email = &model.GetUserEmail();
	REQUIRE(remotes != nullptr);
	REQUIRE(model.GetUserEmail() == std::optional<std::string>{"ann@example.com"});

	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetRemotes() == remotes);
	CHECK(&model.GetUserEmail() == email);

	port.Remotes.push_back(Core::Remote{.Name = "fork", .Url = "v"});
	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetRemotes() != nullptr);
	CHECK(model.GetRemotes() != remotes);
	CHECK(model.GetRemotes()->size() == 2);
}

TEST_CASE("Git window: a listing whose branches changed lands a new snapshot")
{
	FakePort port;
	port.Branches = TrunkAndFeature(kFeatureTipByte);

	Graphite::GitModel model{port, RunJobsInline()};

	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	const Core::Ref<const std::vector<Core::Branch>> first = model.GetBranches();
	REQUIRE(first != nullptr);

	SUBCASE("a tip moved")
	{
		port.Branches = TrunkAndFeature(kMovedFeatureTipByte);
	}

	SUBCASE("another branch was checked out")
	{
		port.Branches[0].IsHead = false;
		port.Branches[2].IsHead = true;
	}

	SUBCASE("the distance to an upstream changed")
	{
		port.Branches[0].DistanceKnown = true;
		port.Branches[0].Ahead = 1;
	}

	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetBranches() != nullptr);
	CHECK(model.GetBranches() != first);
	CHECK(*model.GetBranches() == port.Branches);
}

TEST_CASE("Git model: tags land in the same update as the branches")
{
	FakePort port;
	port.Branches = TrunkAndFeature(kFeatureTipByte);
	port.Tags = {TagOn("v2", kMainTipByte, kNewerTagTime), TagOn("v1", kMainTipByte, kOlderTagTime)};

	DeferredJobs jobs;
	Graphite::GitModel model{port, jobs.Submitter()};

	model.RequestBranches();

	// Each job as a worker takes it, with a frame after it: no frame has the branches without the tags.
	int frames = 0;
	while (jobs.RunOldest())
	{
		model.OnUpdate(Core::Timestep{kNoSeconds});
		++frames;

		CHECK((model.GetBranches() == nullptr) == (model.GetTags() == nullptr));
	}

	CHECK(frames > 1);
	REQUIRE(model.GetBranches() != nullptr);
	REQUIRE(model.GetTags() != nullptr);
	CHECK(*model.GetTags() == port.Tags);
	CHECK_FALSE(model.GetTagsError().has_value());

	// Read after the branches, in their job.
	CHECK(port.BranchCallsBeforeTags == std::vector<int>{1});
}

TEST_CASE("Git model: a failed tag listing lands the branches and keeps the error for the tags")
{
	FakePort port;
	port.Branches = TrunkAndFeature(kFeatureTipByte);
	port.FailTags = true;

	Graphite::GitModel model{port, RunJobsInline()};

	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetBranches() != nullptr);
	CHECK(*model.GetBranches() == port.Branches);
	CHECK_FALSE(model.GetBranchesError().has_value());
	CHECK(model.GetTags() == nullptr);
	REQUIRE(model.GetTagsError().has_value());
	CHECK(model.GetTagsError()->Message == "the tags cannot be read");

	port.FailTags = false;
	port.Tags = {TagOn("v1", kMainTipByte, kOlderTagTime)};
	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	const Core::Ref<const std::vector<Core::Tag>> listed = model.GetTags();
	REQUIRE(listed != nullptr);
	CHECK_FALSE(model.GetTagsError().has_value());

	// A listing that fails again keeps the tags it had, beside its error.
	port.FailTags = true;
	port.Branches = TrunkAndFeature(kMovedFeatureTipByte);
	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(*model.GetBranches() == port.Branches);
	CHECK_FALSE(model.GetBranchesError().has_value());
	CHECK(model.GetTags() == listed);
	CHECK(model.GetTagsError().has_value());
}

TEST_CASE("Git model: a fetch that adds a tag lists it at the next refresh")
{
	FakePort port;
	port.Branches = TrunkAndFeature(kFeatureTipByte);
	port.Tags = {TagOn("v1", kMainTipByte, kOlderTagTime)};

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Core::EventBus events;
	model.SubscribeTo(events);
	events.Publish(Graphite::RepositoryChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastMaxDebounceSeconds});

	const Core::Ref<const std::vector<Core::Tag>> before = model.GetTags();
	REQUIRE(before != nullptr);
	CHECK(before->size() == 1);

	// A refresh that lists the same tags keeps the snapshot, so whatever follows it by pointer has nothing to redo.
	events.Publish(Graphite::RepositoryChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastMaxDebounceSeconds});

	CHECK(model.GetTags() == before);

	port.Tags.insert(port.Tags.begin(), TagOn("v2", kFeatureTipByte, kNewerTagTime));
	events.Publish(Graphite::RepositoryChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastMaxDebounceSeconds});

	REQUIRE(model.GetTags() != nullptr);
	CHECK(model.GetTags() != before);
	CHECK(*model.GetTags() == port.Tags);

	// Every listing of the folder was handed the one cache, so a tag object read once is never read again.
	CHECK(port.TagCacheSizes == std::vector<std::size_t>{0, 1, 2});
}

TEST_CASE("Git model: a tag listing in flight when the folder changes never lands in the next folder")
{
	FakePort port;
	port.Tags = {TagOn("old", kMainTipByte, kOlderTagTime)};

	DeferredJobs jobs;
	Graphite::GitModel model{port, jobs.Submitter()};
	Core::EventBus events;
	model.SubscribeTo(events);

	events.Publish(Graphite::WorkspaceOpenedEvent{"C:/repo"});
	model.OnUpdate(Core::Timestep{kPastMaxDebounceSeconds});
	REQUIRE(jobs.Count() > 0);

	// The folder changes while the first folder's listing waits for a worker; the port answers for the next folder by
	// the time it runs.
	events.Publish(Graphite::WorkspaceOpenedEvent{"C:/other"});
	port.Tags = {TagOn("other", kFeatureTipByte, kNewerTagTime)};
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{kNoSeconds});

	CHECK(port.TagCacheSizes.size() == 1);
	CHECK(model.GetTags() == nullptr);
	CHECK_FALSE(model.GetTagsError().has_value());

	model.OnUpdate(Core::Timestep{kPastMaxDebounceSeconds});
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{kNoSeconds});

	REQUIRE(model.GetTags() != nullptr);
	CHECK(*model.GetTags() == port.Tags);

	// The stale listing filled the first folder's cache; the next folder's listing started from an empty one.
	CHECK(port.TagCacheSizes == std::vector<std::size_t>{0, 0});
}

TEST_CASE("Git model: another folder starts a fresh tag cache and a closed one has no tags")
{
	FakePort port;
	port.Tags = {TagOn("v1", kMainTipByte, kOlderTagTime)};

	Graphite::GitModel model{port, RunJobsInline()};
	Core::EventBus events;
	model.SubscribeTo(events);

	events.Publish(Graphite::WorkspaceOpenedEvent{"C:/repo"});
	model.OnUpdate(Core::Timestep{kPastMaxDebounceSeconds});
	events.Publish(Graphite::RepositoryChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastMaxDebounceSeconds});

	events.Publish(Graphite::WorkspaceOpenedEvent{"C:/other"});
	model.OnUpdate(Core::Timestep{kPastMaxDebounceSeconds});

	CHECK(port.TagCacheSizes == std::vector<std::size_t>{0, 1, 0});
	REQUIRE(model.GetTags() != nullptr);

	events.Publish(Graphite::WorkspaceClosedEvent{});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetTags() == nullptr);
	CHECK_FALSE(model.GetTagsError().has_value());
}

TEST_CASE("Without a base the authors are answered without the own-commit query")
{
	FakePort port;
	port.Branches = {TrunkAndFeature(kFeatureTipByte).back()};

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetAuthorsHidden({});

	ListBranchesAndLandAuthors(model);

	CHECK(port.OwnAuthorsCalls == 0);
	REQUIRE(model.GetBranchAuthors() != nullptr);
	CHECK(model.GetBranchAuthors()->ByTip.empty());
}

TEST_CASE("An empty branch list gets authors with nothing in them")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetAuthorsHidden({});

	ListBranchesAndLandAuthors(model);

	CHECK(port.OwnAuthorsCalls == 0);
	REQUIRE(model.GetBranchAuthors() != nullptr);
	CHECK(model.GetBranchAuthors()->ByTip.empty());
}

TEST_CASE("A failed authors read is asked again by the next listing with the same tips")
{
	FakePort port;
	port.Branches = TrunkAndFeature(kFeatureTipByte);
	port.FailOwnAuthors = true;

	Graphite::GitModel model{port, RunJobsInline()};
	HideTrunkTips(model);

	ListBranchesAndLandAuthors(model);

	CHECK(model.GetBranchAuthors() == nullptr);
	CHECK(model.GetBranchAuthorsError().has_value());

	port.FailOwnAuthors = false;
	ListBranchesAndLandAuthors(model);

	CHECK(port.OwnAuthorsCalls == 2);
	CHECK(model.GetBranchAuthors() != nullptr);
	CHECK_FALSE(model.GetBranchAuthorsError().has_value());
}

TEST_CASE("Closing the workspace drops the branch authors")
{
	FakePort port;
	Core::EventBus events;
	port.Branches = TrunkAndFeature(kFeatureTipByte);

	Graphite::GitModel model{port, RunJobsInline()};
	HideTrunkTips(model);
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	ListBranchesAndLandAuthors(model);
	REQUIRE(model.GetBranchAuthors() != nullptr);

	events.Publish(Graphite::WorkspaceClosedEvent{});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetBranchAuthors() == nullptr);
}

TEST_CASE("Authors asked for a folder that has since closed are dropped")
{
	FakePort port;
	Core::EventBus events;
	port.Branches = TrunkAndFeature(kFeatureTipByte);

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestBranches();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	events.Publish(Graphite::WorkspaceOpenedEvent{"C:/other"});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetBranchAuthors() == nullptr);
}

TEST_CASE("A log is asked together with the length of its history")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Core::LogRequest request;
	request.Ref = "refs/heads/main";

	model.RequestLog(request);
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.CountCalls == 1);
	CHECK(port.LastCountRef == request.Ref);
	CHECK(model.GetCommitCountFor(request.Ref, Core::FollowedParents::All) == std::optional<std::size_t>{1739});
	CHECK_FALSE(model.IsCommitCountInFlight());
}

TEST_CASE("Git window: a first-parent log is counted along first parents and its count is not given for every commit")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Core::LogRequest request;
	request.Ref = "refs/heads/main";
	request.FirstParentOnly = true;

	model.RequestLog(request);
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.LastCountParents == Core::FollowedParents::FirstOnly);
	CHECK(model.GetCommitCountFor(request.Ref, Core::FollowedParents::FirstOnly) == std::optional<std::size_t>{812});
	CHECK_FALSE(model.GetCommitCountFor(request.Ref, Core::FollowedParents::All).has_value());

	request.FirstParentOnly = false;
	model.RequestLog(request);

	CHECK_FALSE(port.LastKnownCount.has_value());
}

TEST_CASE("A stash's log asks for no history length")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Core::LogRequest request;
	request.Stash = OidWithFirstByte(9);

	model.RequestLog(request);
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.CountCalls == 0);
}

TEST_CASE("A refresh hands the port the count it holds so an unmoved tip is not walked again")
{
	Core::EventBus events;
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	Core::LogRequest request;
	request.Ref = "refs/heads/main";

	model.RequestLog(request);
	model.OnUpdate(Core::Timestep{kNoSeconds});

	events.Publish(Graphite::WorkspaceFilesChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastDebounceSeconds});

	CHECK(port.CountCalls == 2);
	CHECK(port.CountWalks == 1);
	REQUIRE(port.LastKnownCount.has_value());
	CHECK(port.LastKnownCount->Count == 1739);
}

TEST_CASE("A count is walked again once the tip has moved")
{
	Core::EventBus events;
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	Core::LogRequest request;
	request.Ref = "refs/heads/main";

	model.RequestLog(request);
	model.OnUpdate(Core::Timestep{kNoSeconds});

	port.CountTip = OidWithFirstByte(2);
	port.CountTotal = 1740;

	events.Publish(Graphite::WorkspaceFilesChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastDebounceSeconds});

	CHECK(port.CountWalks == 2);
	CHECK(model.GetCommitCountFor(request.Ref, Core::FollowedParents::All) == std::optional<std::size_t>{1740});
}

TEST_CASE("The count of one ref is neither reported for another nor handed to the port for it")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	Core::LogRequest mainLog;
	mainLog.Ref = "refs/heads/main";

	model.RequestLog(mainLog);
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	REQUIRE(model.GetCommitCountFor(mainLog.Ref, Core::FollowedParents::All).has_value());

	Core::LogRequest topicLog;
	topicLog.Ref = "refs/heads/topic";

	model.RequestLog(topicLog);

	CHECK_FALSE(port.LastKnownCount.has_value());
	CHECK_FALSE(model.GetCommitCountFor(topicLog.Ref, Core::FollowedParents::All).has_value());
	CHECK_FALSE(model.GetCommitCountFor(std::nullopt, Core::FollowedParents::All).has_value());
}

TEST_CASE("A failed count leaves the history's length unknown and no longer in flight")
{
	FakePort port;
	port.FailCount = true;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	model.RequestLog(Core::LogRequest{});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK_FALSE(model.GetCommitCountFor(std::nullopt, Core::FollowedParents::All).has_value());
	CHECK_FALSE(model.IsCommitCountInFlight());
}

TEST_CASE("A refresh repeats the walk ceiling the view asked for and not the default one")
{
	Core::EventBus events;
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	constexpr std::size_t kLiftedCeiling = 100000;

	Core::LogRequest request;
	request.MaxWalk = kLiftedCeiling;

	model.RequestLog(request);
	model.OnUpdate(Core::Timestep{kNoSeconds});

	events.Publish(Graphite::WorkspaceFilesChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastDebounceSeconds});

	REQUIRE(port.LogCalls == 2);
	REQUIRE(port.LastLogRequest.has_value());
	CHECK(port.LastLogRequest->MaxWalk == kLiftedCeiling);
}

TEST_CASE("A refresh while a log is still being read asks for no second log")
{
	Core::EventBus events;
	FakePort port;
	DeferredJobs jobs;

	Graphite::GitModel model{port, jobs.Submitter()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestLog(Core::LogRequest{});

	events.Publish(Graphite::WorkspaceFilesChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastDebounceSeconds});

	jobs.RunAll();

	CHECK(port.LogCalls == 1);
}

TEST_CASE("A refresh held behind a log in flight is asked once that log lands")
{
	Core::EventBus events;
	FakePort port;
	DeferredJobs jobs;

	Graphite::GitModel model{port, jobs.Submitter()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	constexpr std::size_t kLiftedCeiling = 100000;

	Core::LogRequest request;
	request.Ref = "refs/heads/topic";
	request.MaxWalk = kLiftedCeiling;

	model.RequestLog(request);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastDebounceSeconds});

	jobs.RunAll();
	REQUIRE(port.LogCalls == 1);

	model.OnUpdate(Core::Timestep{kNoSeconds});
	jobs.RunAll();

	CHECK(port.LogCalls == 2);
	REQUIRE(port.LastLogRequest.has_value());
	CHECK(port.LastLogRequest->Ref == request.Ref);
	CHECK(port.LastLogRequest->MaxWalk == kLiftedCeiling);

	model.OnUpdate(Core::Timestep{kNoSeconds});
	jobs.RunAll();

	CHECK(port.LogCalls == 2);
	CHECK(model.IsLogAnswered(model.GetLogGeneration()));
}

TEST_CASE("A reader's ask is never held back by a log in flight")
{
	Core::EventBus events;
	FakePort port;
	DeferredJobs jobs;

	Graphite::GitModel model{port, jobs.Submitter()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	Core::LogRequest first;
	first.Ref = "refs/heads/first";
	model.RequestLog(first);

	events.Publish(Graphite::WorkspaceFilesChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastDebounceSeconds});

	Core::LogRequest other;
	other.Ref = "refs/heads/other";
	model.RequestLog(other);

	// No update runs in between, so a log held back until one lands could not have been read yet.
	jobs.RunAll();

	CHECK(port.LogCalls == 2);
	REQUIRE(port.LastLogRequest.has_value());
	CHECK(port.LastLogRequest->Ref == other.Ref);

	// The reader's ask reads the repository as it is now, so the refresh held before it is not asked too.
	model.OnUpdate(Core::Timestep{kNoSeconds});
	jobs.RunAll();

	CHECK(port.LogCalls == 2);
}

TEST_CASE("Closing the workspace drops a refresh held behind a log in flight")
{
	Core::EventBus events;
	FakePort port;
	DeferredJobs jobs;

	Graphite::GitModel model{port, jobs.Submitter()};
	model.SetRoot("C:/repo");
	model.SubscribeTo(events);

	model.RequestLog(Core::LogRequest{});

	events.Publish(Graphite::WorkspaceFilesChangedEvent{});
	model.OnUpdate(Core::Timestep{kPastDebounceSeconds});

	events.Publish(Graphite::WorkspaceClosedEvent{});
	events.Publish(Graphite::WorkspaceOpenedEvent{"C:/other"});

	jobs.RunAll();
	model.OnUpdate(Core::Timestep{kNoSeconds});
	jobs.RunAll();

	CHECK(port.LogCalls == 1);
}

TEST_CASE("A log is submitted before the length of its history, so its first page does not wait for the count")
{
	FakePort port;
	std::vector<std::function<void()>> submitted;

	Graphite::GitModel model{port, [&submitted](std::function<void()> job) { submitted.push_back(std::move(job)); }};
	model.SetRoot("C:/repo");

	Core::LogRequest request;
	request.Ref = "refs/heads/main";

	model.RequestLog(request);
	REQUIRE(submitted.size() == 2);

	submitted.front()();

	CHECK(port.LogCalls == 1);
	CHECK(port.CountCalls == 0);

	submitted.back()();

	CHECK(port.CountCalls == 1);
	CHECK(port.LastCountRef == request.Ref);
}

TEST_CASE("Git window: opening a merge in merges mode asks what it brought")
{
	FakePort port;
	port.Log.Commits.resize(2);

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	const Core::Oid merge = OidWithFirstByte(10);
	const Core::Oid firstParent = OidWithFirstByte(11);
	const Core::Oid secondParent = OidWithFirstByte(12);

	model.RequestBrought(merge, firstParent, secondParent);

	CHECK(model.GetBrought(merge) == nullptr);

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(port.LastLogRequest.has_value());
	CHECK(port.LastLogRequest->From == std::optional<Core::Oid>{secondParent});
	CHECK(port.LastLogRequest->Exclude == std::optional<Core::Oid>{firstParent});
	CHECK(port.LastLogRequest->FirstParentOnly);
	CHECK(port.LastLogRequest->MaxCount == Core::kLogPageSize);

	REQUIRE(model.GetBrought(merge) != nullptr);
	CHECK(model.GetBrought(merge)->Commits.size() == 2);

	// A page held is not asked again.
	const int callsBefore = port.LogCalls;
	model.RequestBrought(merge, firstParent, secondParent);
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	CHECK(port.LogCalls == callsBefore);
}

TEST_CASE("Git window: brought pages are kept for the last sixteen merges")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	constexpr std::uint8_t kFirstMerge = 20;
	constexpr std::uint8_t kMergeCount = 17;

	for (std::uint8_t offset = 0; offset < kMergeCount; ++offset)
	{
		model.RequestBrought(OidWithFirstByte(kFirstMerge + offset), OidWithFirstByte(1), OidWithFirstByte(2));
		model.OnUpdate(Core::Timestep{kFrameSeconds});
	}

	CHECK(Graphite::GitModel::kCachedBrought == 16);
	CHECK(model.GetBrought(OidWithFirstByte(kFirstMerge)) == nullptr);
	CHECK(model.GetBrought(OidWithFirstByte(kFirstMerge + 1)) != nullptr);
	CHECK(model.GetBrought(OidWithFirstByte(kFirstMerge + kMergeCount - 1)) != nullptr);

	model.Clear();
	CHECK(model.GetBrought(OidWithFirstByte(kFirstMerge + kMergeCount - 1)) == nullptr);
}

TEST_CASE("Git window: the brought pages of the open merges are never let go of however many are open")
{
	FakePort port;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	constexpr std::uint8_t kFirstMerge = 20;
	constexpr std::uint8_t kOpenCount = 20;

	std::unordered_set<Core::Oid> open;

	for (std::uint8_t offset = 0; offset < kOpenCount; ++offset)
	{
		open.insert(OidWithFirstByte(kFirstMerge + offset));
	}

	model.KeepBroughtOf(open);

	for (std::uint8_t offset = 0; offset < kOpenCount; ++offset)
	{
		model.RequestBrought(OidWithFirstByte(kFirstMerge + offset), OidWithFirstByte(1), OidWithFirstByte(2));
	}

	model.OnUpdate(Core::Timestep{kFrameSeconds});

	// A merge selected but not open is asked too, with the cache past its bound.
	model.RequestBrought(OidWithFirstByte(100), OidWithFirstByte(1), OidWithFirstByte(2));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	const int logCallsAfterReading = port.LogCalls;

	// Every open merge is asked each frame, as the history does: none was let go of, so none is read again.
	for (std::uint8_t offset = 0; offset < kOpenCount; ++offset)
	{
		CHECK(model.GetBrought(OidWithFirstByte(kFirstMerge + offset)) != nullptr);
		model.RequestBrought(OidWithFirstByte(kFirstMerge + offset), OidWithFirstByte(1), OidWithFirstByte(2));
	}

	model.OnUpdate(Core::Timestep{kFrameSeconds});
	CHECK(port.LogCalls == logCallsAfterReading);

	// Merges closed again are let go of like any other, the one touched longest ago first.
	model.KeepBroughtOf({});
	model.RequestBrought(OidWithFirstByte(101), OidWithFirstByte(1), OidWithFirstByte(2));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetBrought(OidWithFirstByte(kFirstMerge)) == nullptr);
	CHECK(model.GetBrought(OidWithFirstByte(101)) != nullptr);
}

TEST_CASE("Git window: a failed read of what a merge brought keeps its message until the merge is closed")
{
	FakePort port;
	port.FailLog = true;

	Graphite::GitModel model{port, RunJobsInline()};
	model.SetRoot("C:/repo");

	const Core::Oid merge = OidWithFirstByte(10);

	model.RequestBrought(merge, OidWithFirstByte(11), OidWithFirstByte(12));
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetBroughtFailure(merge).has_value());
	CHECK_FALSE(model.GetBroughtFailure(merge)->Message.empty());

	// Asking again changes nothing while the failure is held.
	const int callsBefore = port.LogCalls;
	model.RequestBrought(merge, OidWithFirstByte(11), OidWithFirstByte(12));
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	CHECK(port.LogCalls == callsBefore);

	port.FailLog = false;
	model.ForgetFailedBrought(merge);
	CHECK_FALSE(model.GetBroughtFailure(merge).has_value());

	model.RequestBrought(merge, OidWithFirstByte(11), OidWithFirstByte(12));
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	CHECK(port.LogCalls == callsBefore + 1);
	CHECK(model.GetBrought(merge) != nullptr);
}

namespace {

	struct HeadWithMessage : EmptyGitPort
	{
		Core::Result<Core::LogPage> QueryLog(const Core::LogRequest& request) override
		{
			Asked.push_back(request);

			Core::CommitSummary head;
			head.Summary = "Fix the parser";
			head.Body = "It dropped the last token.";

			Core::LogPage page;
			page.Commits.push_back(head);
			return page;
		}

		std::vector<Core::LogRequest> Asked;
	};
}

TEST_CASE("The head message is read as one commit of HEAD's history, summary and body together")
{
	HeadWithMessage port;
	Graphite::GitModel model(port, RunJobsInline());
	model.SetRoot("C:/repo");

	CHECK(model.GetHeadMessage() == nullptr);

	model.RequestHeadMessage();
	model.OnUpdate(Core::Timestep{0.0f});

	REQUIRE(model.GetHeadMessage() != nullptr);
	CHECK(*model.GetHeadMessage() == "Fix the parser\n\nIt dropped the last token.");
	REQUIRE(port.Asked.size() == 1);
	CHECK(port.Asked.front().MaxCount == 1);
	CHECK_FALSE(port.Asked.front().Ref.has_value());
}
