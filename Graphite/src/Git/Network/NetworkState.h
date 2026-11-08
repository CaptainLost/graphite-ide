#pragma once

#include <cstdint>
#include <optional>

namespace Graphite {

	enum class NetworkOperation
	{
		Fetch,
		Pull,
		Push,
		FullHistory
	};

	struct NetworkState
	{
		std::optional<NetworkOperation> Running;

		// Which operation runs: each one started takes the next serial. Zero while nothing runs.
		std::uint64_t Serial = 0;

		// git's own, once it counts.
		std::optional<int> Percent;

		// False while nothing runs, and once Pull's fast-forward is asked for, which nothing stops.
		bool Cancellable = false;

		friend bool operator==(const NetworkState&, const NetworkState&) = default;
	};
}
