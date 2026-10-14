#pragma once

#include "Sessions/Transcript/TranscriptRecord.h"

#include "Core/Base/Result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace Graphite {

	std::optional<TranscriptRecord> ParseRecord(std::string_view line);

	struct TranscriptChunk
	{
		std::vector<TranscriptRecord> Records;
		std::size_t Consumed = 0;
		std::size_t Skipped = 0;
	};

	TranscriptChunk ParseTranscript(std::string_view bytes);

	struct TranscriptRead
	{
		TranscriptChunk Chunk;
		std::uint64_t NextOffset = 0;
		bool Restarted = false;
		std::uint64_t FileSize = 0;
	};

	Core::Result<TranscriptRead> ReadTranscript(const std::filesystem::path& file,
												std::uint64_t offset);

	Core::Result<TranscriptRead> ReadTranscriptChunk(const std::filesystem::path& file,
													 std::uint64_t offset, std::uint64_t maxBytes);
}
