#pragma once

#include <filesystem>
#include <string>

// Where Graphite installs the agent: ~/.graphite/agent/<version>/graphite-agent, <version> being
// the first 16 hex digits of the binary's SHA-256 (Core::AgentVersionOf on the host).
namespace Agent {

	// The name of the folder the binary sits in.
	std::string VersionOf(const std::filesystem::path& self);

	// Deletes every other version folder beside this one's, so updates do not pile up. Does
	// nothing unless self sits at .graphite/agent/<version>/, so a binary run from a build folder
	// cannot delete its neighbours.
	void RemoveOtherVersions(const std::filesystem::path& self);
}
