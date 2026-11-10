#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Head.h"
#include "Core/Git/Requests.h"
#include "Core/Git/Status.h"

#include <nlohmann/json.hpp>

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

	Result<WorkingTreeStatus> WorkingTreeStatusFromJson(const nlohmann::json& json);
	Result<HeadState> HeadStateFromJson(const nlohmann::json& json);
	Result<DiffRequest> DiffRequestFromJson(const nlohmann::json& json);
	Result<Diff> DiffFromJson(const nlohmann::json& json);
	Result<CommitRequest> CommitRequestFromJson(const nlohmann::json& json);
}
