#pragma once

#include "Core/Base/Result.h"

#include <nlohmann/json.hpp>

#include <string_view>

// The agent's answers to Graphite's file requests. Each takes the request's arguments (and its
// payload, for a write) and returns the reply's result, or the error to reply with. Paths are
// absolute Linux paths.
namespace Agent {

	// {"path"}: writes the payload atomically, keeping the permissions of a file it replaces.
	Core::Result<nlohmann::json> WriteFile(const nlohmann::json& arguments, std::string_view payload);

	// {"from","to"}: InvalidArgument when "to" already exists, so nothing is overwritten.
	Core::Result<nlohmann::json> Rename(const nlohmann::json& arguments);

	// {"path"}: the folder and any missing folders above it.
	Core::Result<nlohmann::json> MakeDirectory(const nlohmann::json& arguments);

	// {"path"}: an empty file; InvalidArgument when something is already there.
	Core::Result<nlohmann::json> CreateFile(const nlohmann::json& arguments);

	// {"path"} -> {"trashedTo"}: into the user's freedesktop trash.
	Core::Result<nlohmann::json> MoveToTrash(const nlohmann::json& arguments);
}
