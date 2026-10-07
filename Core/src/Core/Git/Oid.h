#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace Core {

	// Raw bytes rather than hex text, because the graph compares and hashes ids on every
	// layout pass.
	struct Oid
	{
		static constexpr std::size_t kByteCount = 20;
		static constexpr std::size_t kGitDefaultShortHexDigits = 7;

		std::array<std::uint8_t, kByteCount> Bytes{};

		bool IsZero() const;

		std::string ToHex() const;

		// A count above the full length is clamped rather than asserted.
		std::string ShortHex(std::size_t digits = kGitDefaultShortHexDigits) const;

		friend bool operator==(const Oid&, const Oid&) = default;
	};
}

template<>
struct std::hash<Core::Oid>
{
	std::size_t operator()(const Core::Oid& id) const noexcept;
};
