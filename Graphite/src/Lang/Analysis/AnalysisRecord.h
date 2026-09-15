#pragma once

#include "Editor/AnalysisCache.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	// The format the on-disk store writes. Bumped whenever the layout below changes in any way —
	// a record from another version is discarded rather than reinterpreted, which is the whole
	// benefit of a version field on a cache nobody has to migrate.
	inline constexpr std::uint32_t kAnalysisRecordVersion = 1;

	// One analysis as bytes, and back.
	//
	// Binary rather than JSON: a large file's overlay runs to thousands of spans, and this is read
	// on the UI thread in the frame a document opens. Free functions over a string rather than a
	// stream, and no filesystem anywhere in here, so every way a record can be malformed is a unit
	// test rather than a temporary directory.
	//
	// `path` is written into the record and checked on the way back. Records are named on disk by
	// a hash of the path, and a hash has collisions; this is what makes one file's colours unable
	// to be drawn over another's.
	std::string EncodeAnalysis(const std::filesystem::path& path, const CachedAnalysis& analysis);

	// Empty for anything that is not exactly a record of this version for exactly this path:
	// wrong magic, wrong version, a length that claims more than is there, a truncated tail, or a
	// TokenKind/DeclarationKind value outside what this build knows how to draw. A cache has
	// nothing worth half-trusting, so there is deliberately no partial result and no error to
	// report — the caller re-asks the server, which is what it would have done anyway.
	std::optional<CachedAnalysis> DecodeAnalysis(std::string_view bytes,
												 const std::filesystem::path& expectedPath);
}
