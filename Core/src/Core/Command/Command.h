#pragma once

#include <functional>
#include <string>

namespace Core {

	struct Command
	{
		std::string id;
		std::string label;

		std::string group;

		std::function<void()> run;

		std::function<bool()> enabled;

		std::function<bool()> checked;
	};
}