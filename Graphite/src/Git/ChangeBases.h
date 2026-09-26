#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	// The two texts a buffer's change markers are measured against, already decoded the way
	// the buffer is: UTF-8, no byte order mark, LF line endings.
	struct ChangeBases
	{
		// False for an ignored file, a file outside the repository, and no repository at
		// all. The column is left empty and nothing is diffed.
		bool Tracked = false;

		// Tracked, but a side is binary, past the size limit, or not text. Nothing is drawn,
		// rather than every line marked changed.
		bool Unusable = false;

		// Absent means the file is not there: an added file has no HEAD side, and an untracked
		// one has neither.
		std::optional<std::string> Head;
		std::optional<std::string> Index;
	};

	// What ChangeMarkersModel needs a repository for. Kept apart from GitPort, which answers the
	// git panels: the model has one question, and the tests answer it from memory.
	class ChangeBasePort
	{
	public:
		virtual ~ChangeBasePort() = default;

		// Called on a worker thread. The path is absolute, as the editor holds it.
		virtual Core::Result<ChangeBases> ReadBases(const std::filesystem::path& absolutePath) = 0;
	};

	// A blob's bytes as text comparable with the buffer, or nothing when it cannot be.
	std::optional<std::string> DecodeBaseText(std::string_view bytes);

	// Decides what two raw reads mean. Pulled out of the port so it can be tested without a
	// repository.
	ChangeBases MakeChangeBases(std::optional<std::string> headBytes,
								std::optional<std::string> indexBytes);
}
