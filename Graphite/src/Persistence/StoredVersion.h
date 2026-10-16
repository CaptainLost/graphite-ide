#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string_view>

namespace Graphite {

	enum class StoredVersion
	{
		Missing,
		NotANumber,
		Older,
		Current,
		Newer
	};

	// The version a state file says it was written in, measured against the one this build writes. What each answer
	// means for reading and writing the file is the caller's policy.
	StoredVersion StoredVersionOf(const nlohmann::json& document, std::string_view key, std::int64_t current);
}