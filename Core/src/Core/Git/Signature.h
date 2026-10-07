#pragma once

#include <chrono>
#include <string>

namespace Core {

	struct Signature
	{
		std::string Name;
		std::string Email;

		// In UTC. The author's clock offset is dropped, so this cannot show the author's own
		// local time.
		std::chrono::sys_seconds When;
	};
}
