#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Blame.h"
#include "Core/Git/Checkout.h"
#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Head.h"
#include "Core/Git/LineHistory.h"
#include "Core/Git/OwnAuthors.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Remote.h"
#include "Core/Git/Requests.h"
#include "Core/Git/Stash.h"
#include "Core/Git/Status.h"
#include "Core/Git/Tag.h"
#include "Core/Git/Worktree.h"

#include <nlohmann/json.hpp>

#include <vector>

// The Git types a WSL repository's requests and answers carry through the agent's protocol
// (ADR 0695), as JSON: paths in generic UTF-8 (relative ones stay relative), enums by name, ids in
// hex. Decoding refuses a missing or mistyped field with InvalidArgument naming it.
namespace Core {

	nlohmann::json ToJson(const FileStatus& status);
	nlohmann::json ToJson(const WorkingTreeStatus& status);
	nlohmann::json ToJson(const HeadState& head);
	nlohmann::json ToJson(const DiffRequest& request);
	nlohmann::json ToJson(const Diff& diff);
	nlohmann::json ToJson(const CommitRequest& request);
	nlohmann::json ToJson(const CommitSummary& commit);
	nlohmann::json ToJson(const Branch& branch);
	nlohmann::json ToJson(const Tag& tag);
	nlohmann::json ToJson(const Remote& remote);
	nlohmann::json ToJson(const LogRequest& request);
	nlohmann::json ToJson(const LogPage& page);
	nlohmann::json ToJson(const OwnAuthorsRequest& request);
	nlohmann::json ToJson(const TipAuthors& authors);
	nlohmann::json ToJson(const StashEntry& stash);
	nlohmann::json ToJson(const CheckoutTarget& target);
	nlohmann::json ToJson(const CheckoutPlan& plan);
	nlohmann::json ToJson(const CheckoutOutcome& outcome);
	nlohmann::json ToJson(const Blame& blame);
	nlohmann::json ToJson(const LineHistoryRequest& request);
	nlohmann::json ToJson(const LineHistory& history);
	nlohmann::json ToJson(const WorktreeEntry& worktree);

	// A list of anything ToJson takes, as a JSON array in the same order.
	template<typename Value>
	nlohmann::json ToJsonList(const std::vector<Value>& values)
	{
		nlohmann::json listed = nlohmann::json::array();

		for (const Value& value : values)
		{
			listed.push_back(ToJson(value));
		}

		return listed;
	}

	Result<WorkingTreeStatus> WorkingTreeStatusFromJson(const nlohmann::json& json);
	Result<HeadState> HeadStateFromJson(const nlohmann::json& json);
	Result<DiffRequest> DiffRequestFromJson(const nlohmann::json& json);
	Result<Diff> DiffFromJson(const nlohmann::json& json);
	Result<CommitRequest> CommitRequestFromJson(const nlohmann::json& json);
	Result<std::vector<Branch>> BranchesFromJson(const nlohmann::json& json);
	Result<std::vector<Tag>> TagsFromJson(const nlohmann::json& json);
	Result<std::vector<Remote>> RemotesFromJson(const nlohmann::json& json);
	Result<LogRequest> LogRequestFromJson(const nlohmann::json& json);
	Result<LogPage> LogPageFromJson(const nlohmann::json& json);
	Result<OwnAuthorsRequest> OwnAuthorsRequestFromJson(const nlohmann::json& json);
	Result<std::vector<TipAuthors>> TipAuthorsFromJson(const nlohmann::json& json);
	Result<std::vector<StashEntry>> StashesFromJson(const nlohmann::json& json);
	Result<CheckoutTarget> CheckoutTargetFromJson(const nlohmann::json& json);
	Result<CheckoutPlan> CheckoutPlanFromJson(const nlohmann::json& json);
	Result<CheckoutOutcome> CheckoutOutcomeFromJson(const nlohmann::json& json);
	Result<Blame> BlameFromJson(const nlohmann::json& json);
	Result<LineHistoryRequest> LineHistoryRequestFromJson(const nlohmann::json& json);
	Result<LineHistory> LineHistoryFromJson(const nlohmann::json& json);
	Result<std::vector<WorktreeEntry>> WorktreesFromJson(const nlohmann::json& json);
}
