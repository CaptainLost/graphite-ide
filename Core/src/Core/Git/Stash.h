#pragma once

#include "Core/Git/Oid.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Core {

	struct StashEntry
	{
		// The n in stash@{n}. It shifts whenever a stash is pushed or dropped, so the write
		// operations take Id instead.
		std::size_t Index = 0;

		// Git's own text, prefix included: "On main: message" or "WIP on main: <commit summary>".
		std::string Message;

		Oid Id;

		std::int64_t SecondsSinceEpoch = 0;
	};
}
