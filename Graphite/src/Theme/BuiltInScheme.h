#pragma once

#include <cstddef>
#include <cstdint>

namespace Graphite {

	enum class BuiltInScheme : std::uint8_t
	{
		Dark,
		Light,
		Graphite,
		GraphiteBright,
		GraphiteWarm,
		GraphiteCards,
		GraphiteLight,
		GruvboxDarkHard,
		GruvboxDarkMedium,
		GruvboxDarkSoft,
		GruvboxLightHard,
		GruvboxLightMedium,
		GruvboxLightSoft,
		Count
	};

	constexpr std::size_t kBuiltInSchemeCount = static_cast<std::size_t>(BuiltInScheme::Count);
}
