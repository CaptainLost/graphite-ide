#pragma once

#include "Sessions/TranscriptRecord.h"

#include "Core/Base/Result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace Graphite {

	// One line, or nothing when it is not a JSON object.
	std::optional<TranscriptRecord> ParseRecord(std::string_view line);

	struct TranscriptChunk
	{
		std::vector<TranscriptRecord> Records;

		// Bytes of complete lines. A last line with no newline is not consumed: Claude may be
		// half way through writing it.
		std::size_t Consumed = 0;

		// Complete lines that did not parse. Blank lines are not counted.
		std::size_t Skipped = 0;
	};

	TranscriptChunk ParseTranscript(std::string_view bytes);

	struct TranscriptRead
	{
		TranscriptChunk Chunk;
		std::uint64_t NextOffset = 0;

		// The file was shorter than the offset asked for, so it was read from zero and the
		// caller must drop what it built from the earlier contents.
		bool Restarted = false;
	};

	// Reads from `offset` to the end of the file.
	Core::Result<TranscriptRead> ReadTranscript(const std::filesystem::path& file,
												std::uint64_t offset);
}
