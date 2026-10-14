#pragma once

#include <cstdint>

namespace Graphite {

	enum class SessionColor : std::uint8_t
	{
		Read,
		Edit,
		Run,
		Agent,
		Web,
		Other,

		Failed,

		UserBlock,
		ClaudeBlock,
		CodeBlock,
		Injected,

		Count
	};
}
