#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

	// Raw bytes rather than hex text, because the graph compares and hashes ids on every
	// layout pass.
	struct Oid
	{
		static constexpr std::size_t kByteCount = 20;
		static constexpr std::size_t kGitDefaultShortHexDigits = 7;

		std::array<std::uint8_t, kByteCount> Bytes{};

		// Nothing unless the text is exactly forty hex digits; either case is read.
		static std::optional<Oid> FromHex(std::string_view hex);

		bool IsZero() const;

		std::string ToHex() const;

		// A count above the full length is clamped rather than asserted.
		std::string ShortHex(std::size_t digits = kGitDefaultShortHexDigits) const;

		friend bool operator==(const Oid&, const Oid&) = default;
		friend auto operator<=>(const Oid&, const Oid&) = default; // byte order, for ordered containers
	};
}

template<>
struct std::hash<Core::Oid>
{
	std::size_t operator()(const Core::Oid& id) const noexcept;
};
