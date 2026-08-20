#pragma once

#include <chrono>
#include <string>

namespace Core {

	struct Signature
	{
		std::string Name;
		std::string Email;

		// The instant git recorded, in UTC. The offset the author's clock had is
		// deliberately dropped: every display in this application shows local time or a
		// relative age, and neither needs it.
		std::chrono::sys_seconds When;
	};
}
