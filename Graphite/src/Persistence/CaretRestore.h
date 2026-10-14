#pragma once

#include <cstddef>

namespace Core {

	class TextBuffer;

}

namespace Graphite {

	std::size_t RestoredCaretOffset(const Core::TextBuffer& buffer, std::size_t line, std::size_t byteColumn);

	std::size_t RestoredTopLine(const Core::TextBuffer& buffer, std::size_t savedTopLine);
}
