#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Lines/AncestryGraph.h"

#include <filesystem>
#include <optional>

namespace Graphite {

	// Format v1, little-endian: magic "GLG1"; u32 author count; per author u32 byte length and the UTF-8 bytes;
	// u32 commit count; per commit 20 id bytes, i64 time, u32 author index, u8 flags (bit 0 = ParentsCut),
	// u8 parent count, then u32 per parent (an index into this file's commit table, always below the commit's own:
	// commits are written parents first).
	Core::Result<void> SaveGraph(const std::filesystem::path& file, const Core::AncestryGraph& graph);

	// A missing, truncated, foreign-version or inconsistent file (an index out of range or not below its commit's,
	// trailing bytes) gives nothing.
	std::optional<Core::AncestryGraph> LoadGraph(const std::filesystem::path& file);
}
