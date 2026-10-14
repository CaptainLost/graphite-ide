#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	struct ChangeBases
	{
		bool Tracked = false;

		// WARNING: Unusable (a binary or oversized side) draws nothing, rather than marking every
		// line changed.
		bool Unusable = false;

		std::optional<std::string> Head;
		std::optional<std::string> Index;
	};

	class ChangeBasePort
	{
	public:
		virtual ~ChangeBasePort() = default;

		virtual Core::Result<ChangeBases> ReadBases(const std::filesystem::path& absolutePath) = 0;
	};

	std::optional<std::string> DecodeBaseText(std::string_view bytes);

	ChangeBases MakeChangeBases(std::optional<std::string> headBytes,
								std::optional<std::string> indexBytes);
}
