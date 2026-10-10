#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	class NuGetVersion
	{
	public:
		static std::optional<NuGetVersion> Parse(std::string_view text);

		bool IsPrerelease() const;

		std::strong_ordering operator<=>(const NuGetVersion& other) const;
		bool operator==(const NuGetVersion& other) const;

	private:
		std::array<std::uint64_t, 4> m_Numbers{};

		// Lower-cased when parsed, since NuGet compares labels without regard to case.
		std::vector<std::string> m_Labels;
	};
}
