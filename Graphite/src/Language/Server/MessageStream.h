#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	class MessageStream
	{
	public:
		void Append(std::string_view bytes);

		std::optional<std::string> Take();

	private:
		std::string m_Buffer;
	};
}
