#pragma once

#include <functional>
#include <string>

namespace Core {

	struct Command
	{
		std::string Id;
		std::string Label;

		// Drawn before the label in a menu; empty leaves the label alone.
		std::string Icon;

		std::string Group;

		std::function<void()> Run;

		std::function<bool()> Enabled;

		std::function<bool()> Checked;
	};
}