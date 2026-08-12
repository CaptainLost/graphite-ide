#pragma once

#include <functional>
#include <string>

namespace Core {

	struct Command
	{
		std::string Id;
		std::string Label;

		std::string Group;

		std::function<void()> Run;

		std::function<bool()> Enabled;

		std::function<bool()> Checked;
	};
}