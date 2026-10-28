#pragma once

#include "Editor/EditorCommands.h"
#include "Hex/HexNavigation.h"

#include "Core/Base/Result.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Graphite {

	inline constexpr std::uint64_t kMostCopiedBytes = 1024 * 1024;

	// Copy and Select All, in a file with any bytes; the other editing commands change text, which a hex tab does not
	// hold.
	bool CanRunOnHexTab(EditorAction action, std::uint64_t fileSize);

	HexSelection WholeFile(std::uint64_t fileSize);

	// The selected bytes as hex pairs, read from the file now rather than from what the view has cached, and empty when
	// the file has none of them. A selection of more than kMostCopiedBytes is refused as TooLarge before anything is read.
	Core::Result<std::string> CopySelectedBytes(const std::filesystem::path& path, const HexSelection& selection,
												std::uint64_t fileSize);
}
