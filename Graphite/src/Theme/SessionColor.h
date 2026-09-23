#pragma once

#include <cstdint>

namespace Graphite {

	// Colours of the Claude sessions views. A group of their own, like GitColor, so a scheme
	// author finds them together and a scheme that predates them still resolves them from its
	// base.
	enum class SessionColor : std::uint8_t
	{
		// The bar and icon of a tool call, by what the tool does.
		Read,
		Edit,
		Run,
		Agent,
		Web,
		Other,

		// A failed call, whatever its category.
		Failed,

		// Behind a user message, Claude's reply, a fenced code block, and injected text.
		UserBlock,
		ClaudeBlock,
		CodeBlock,
		Injected,

		Count
	};
}
