#pragma once

#include "Core/Git/Oid.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Core {

	struct StashEntry
	{
		// stash@{Index}. Shifts whenever a stash is pushed or dropped, which is why the
		// writes take Id instead.
		std::size_t Index = 0;

		// As git records it: "On main: message" or "WIP on main: <commit summary>".
		std::string Message;

		Oid Id;

		// Seconds since the epoch, from the stash commit.
		std::int64_t Time = 0;
	};
}
