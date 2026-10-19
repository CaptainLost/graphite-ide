#pragma once

namespace Graphite {

	enum class ServerHealth
	{
		None,
		Starting,
		Ready,
		Limited,
		Failed
	};
}
