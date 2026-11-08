#pragma once

#include "Core/Git/Diff.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Status.h"
#include "Git/GitModel.h"
#include "Git/GitPort.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// What a test double of the port answers to the network operations: it records the call's name and arguments, and
// answers what a test set, Done by default.
struct NetworkDoubleState
{
	Graphite::NetworkSetup Setup;
	std::optional<std::chrono::sys_seconds> LastFetch;
	Core::NetworkOutcome Outcome;
	std::vector<std::string> Calls;
	int Progress = -1;

	Core::NetworkOutcome Answer(std::string call, const Core::ProgressSink& progress)
	{
		Calls.push_back(std::move(call));

		if (progress && Progress >= 0)
		{
			progress(Progress);
		}

		return Outcome;
	}
};

// The network half of GitPort for a test double: each call is recorded by name and arguments, and answered with what a
// test set in Network, Done by default.
class NetworkDoublePort : public Graphite::GitPort
{
public:
	NetworkDoubleState Network;

	Graphite::NetworkSetup QueryNetworkSetup() override { return Network.Setup; }
	std::optional<std::chrono::sys_seconds> QueryLastFetch() override { return Network.LastFetch; }

	Core::NetworkOutcome FetchAll(const std::atomic<bool>&, const Core::ProgressSink& progress) override
	{
		return Network.Answer("FetchAll", progress);
	}

	Core::NetworkOutcome FetchRemote(const std::atomic<bool>&, const Core::ProgressSink& progress, const std::string& remote) override
	{
		return Network.Answer("FetchRemote " + remote, progress);
	}

	Core::NetworkOutcome FetchFullHistory(const std::atomic<bool>&, const Core::ProgressSink& progress) override
	{
		return Network.Answer("FetchFullHistory", progress);
	}

	Core::NetworkOutcome FastForward(const std::atomic<bool>&, const Core::ProgressSink& progress, const std::string& upstream) override
	{
		return Network.Answer("FastForward " + upstream, progress);
	}

	Core::NetworkOutcome PushTo(const std::atomic<bool>&, const Core::ProgressSink& progress, const std::string& remote,
								const std::optional<std::string>& upstreamBranch) override
	{
		return Network.Answer("PushTo " + remote + " " + upstreamBranch.value_or("(set upstream)"), progress);
	}

	Core::NetworkOutcome SetRemoteHeadAuto(const std::atomic<bool>&, const Core::ProgressSink& progress, const std::string& remote) override
	{
		return Network.Answer("SetRemoteHeadAuto " + remote, progress);
	}
};

// A repository with nothing in it: every query answers empty and every write succeeds, for tests
// that need a GitModel but none of its answers. A diff or a file's texts can be told to fail instead, and a
// test that needs one answer of its own derives and overrides that query.
class EmptyGitPort : public NetworkDoublePort
{
public:
	using Paths = std::vector<std::filesystem::path>;

	Core::Result<Core::WorkingTreeStatus> QueryStatus() override { return Core::WorkingTreeStatus{}; }
	Core::Result<Core::WorkingTreeStatus> QueryUntrackedIn(const std::filesystem::path&) override { return Core::WorkingTreeStatus{}; }
	Core::Result<std::filesystem::path> GetWorkingDirectory() override { return std::filesystem::path{"C:/repo"}; }

	Core::Result<std::vector<Core::Branch>> QueryBranches() override { return std::vector<Core::Branch>{}; }
	Core::Result<std::vector<Core::Tag>> QueryTags(Core::TagCache&) override { return std::vector<Core::Tag>{}; }
	Core::Result<std::vector<Core::TipAuthors>> QueryOwnAuthors(const Core::OwnAuthorsRequest&) override { return std::vector<Core::TipAuthors>{}; }
	Core::Result<std::optional<std::string>> ReadUserEmail() override { return std::optional<std::string>{}; }
	Core::Result<std::vector<Core::Remote>> QueryRemotes() override { return std::vector<Core::Remote>{}; }
	Core::Result<Core::HeadState> QueryHead() override { return Core::HeadState{}; }
	Core::Result<Core::LogPage> QueryLog(const Core::LogRequest&) override { return Core::LogPage{}; }

	Core::Result<Graphite::CommitCount> CountCommits(const std::optional<std::string>&, Core::FollowedParents,
													 const std::optional<Graphite::CommitCount>&) override
	{
		return Graphite::CommitCount{};
	}

	Core::Result<Core::Diff> QueryDiff(const Core::DiffRequest&) override
	{
		if (DiffFailure.has_value())
		{
			return *DiffFailure;
		}

		return DiffAnswer;
	}

	std::optional<Core::Error> DiffFailure;
	Core::Diff DiffAnswer;
	std::optional<Core::Error> TextsFailure;

	Core::Result<Graphite::FileTexts> ReadFileTexts(const Core::Oid&, const std::filesystem::path&, const std::filesystem::path&) override
	{
		if (TextsFailure.has_value())
		{
			return *TextsFailure;
		}

		return Graphite::FileTexts{};
	}

	Core::Result<Graphite::FileTexts> ReadWorkingTreeTexts(Core::DiffTarget, const std::filesystem::path&, const std::filesystem::path&) override
	{
		if (TextsFailure.has_value())
		{
			return *TextsFailure;
		}

		return Graphite::FileTexts{};
	}

	Core::Result<void> Stage(const Paths&) override { return Core::Result<void>::Ok(); }
	Core::Result<void> Unstage(const Paths&) override { return Core::Result<void>::Ok(); }
	Core::Result<void> DiscardWorkTree(const Paths&) override { return Core::Result<void>::Ok(); }
	Core::Result<void> DeleteUntracked(const Paths&) override { return Core::Result<void>::Ok(); }
	Core::Result<Core::Oid> Commit(const Core::CommitRequest&) override { return Core::Oid{}; }

	Core::Result<void> CreateBranch(const std::string&, const Core::Oid&) override { return Core::Result<void>::Ok(); }
	Core::Result<void> RenameBranch(const std::string&, const std::string&) override { return Core::Result<void>::Ok(); }
	Core::Result<std::size_t> CountUnmergedCommits(const std::string&) override { return std::size_t{0}; }
	Core::Result<void> DeleteBranch(const std::string&) override { return Core::Result<void>::Ok(); }

	Core::Result<bool> StashSave(const std::string&, Core::UntrackedFiles) override { return false; }
	Core::Result<std::vector<Core::StashEntry>> QueryStashes() override { return std::vector<Core::StashEntry>{}; }
	Core::Result<void> StashApply(const Core::Oid&) override { return Core::Result<void>::Ok(); }
	Core::Result<void> StashPop(const Core::Oid&) override { return Core::Result<void>::Ok(); }
	Core::Result<void> StashDrop(const Core::Oid&) override { return Core::Result<void>::Ok(); }

	Core::Result<Core::CheckoutPlan> PlanCheckout(const Core::CheckoutTarget&) override { return Core::CheckoutPlan{}; }
	Core::CheckoutOutcome Checkout(const Core::CheckoutTarget&) override { return Core::CheckoutOutcome{}; }
	Core::Result<void> DiscardAll(Core::UntrackedFiles) override { return Core::Result<void>::Ok(); }

	Core::Result<void> Init() override { return Core::Result<void>::Ok(); }
};

inline Core::Oid OidWithFirstByte(std::uint8_t firstByte)
{
	Core::Oid id;
	id.Bytes[0] = firstByte;
	return id;
}

inline Graphite::JobSubmitter RunJobsInline()
{
	return [](std::function<void()> job) { job(); };
}

inline Core::WorkingTreeStatus WorkingTreeStatusOf(std::vector<Core::FileStatus> entries)
{
	Core::WorkingTreeStatus status;
	status.Entries = std::move(entries);
	return status;
}

inline Core::DiffLine MakeDiffLine(Core::DiffLineKind kind, int oldLine, int newLine, std::string text)
{
	Core::DiffLine line;
	line.Kind = kind;
	line.OldLine = oldLine;
	line.NewLine = newLine;
	line.Text = std::move(text);
	return line;
}

inline Core::FileDiff MakeFileDiff(std::vector<Core::DiffLine> lines)
{
	Core::Hunk hunk;
	hunk.OldStart = 1;
	hunk.NewStart = 1;
	hunk.Header = "@@ -1 +1 @@\n";
	hunk.Lines = std::move(lines);

	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.NewPath = "a.txt";
	file.Hunks.push_back(std::move(hunk));
	return file;
}

inline std::chrono::sys_seconds SystemSecondsAt(long long seconds)
{
	return std::chrono::sys_seconds{std::chrono::seconds{seconds}};
}
