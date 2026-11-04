#pragma once

#include <string>

namespace Core {

	struct Remote
	{
		std::string Name;

		// Empty when the remote's config names none.
		std::string Url;
	};
}
