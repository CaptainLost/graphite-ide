#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace Core {

	// A git object name. Twenty raw bytes rather than the forty-character text, because
	// these are compared and hashed far more often than they are shown: the graph keys
	// commits by id on every layout pass.
	struct Oid
	{
		std::array<std::uint8_t, 20> Bytes{};

		bool IsZero() const;

		std::string ToHex() const;

		// Seven digits is what git itself abbreviates to by default. Values above forty
		// are clamped rather than rejected: an over-long request is a display choice, not
		// a programming error worth an assert.
		std::string ShortHex(std::size_t digits = 7) const;

		friend bool operator==(const Oid&, const Oid&) = default;
	};
}

template<>
struct std::hash<Core::Oid>
{
	std::size_t operator()(const Core::Oid& id) const noexcept;
};
