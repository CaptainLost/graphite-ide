#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	std::string BytesToHex(std::span<const std::uint8_t> bytes);

	std::optional<std::vector<std::uint8_t>> BytesFromHex(std::string_view text);

	// The standard alphabet with '=' padding; the URL-safe alphabet reads as nothing.
	std::optional<std::vector<std::uint8_t>> BytesFromBase64(std::string_view text);
}
