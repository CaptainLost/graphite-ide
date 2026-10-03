#include "Git/Changes/LineSpans.h"

#include <algorithm>

namespace Graphite {

	namespace {

		// A UTF-8 continuation byte, 10xxxxxx: never the first byte of a character.
		bool IsContinuation(char byte)
		{
			return (static_cast<unsigned char>(byte) & 0xC0u) == 0x80u;
		}
	}

	ChangedSpan RemovedSpan(std::string_view oldLine, std::string_view newLine)
	{
		const std::size_t shared = std::min(oldLine.size(), newLine.size());

		std::size_t prefix = 0;

		while (prefix < shared && oldLine[prefix] == newLine[prefix])
		{
			++prefix;
		}

		// Back to the start of the character the first difference falls in: two characters that
		// share a lead byte differ only in a later one, and a span starting there would cut the
		// old character in half.
		while (prefix > 0 && prefix < oldLine.size() && IsContinuation(oldLine[prefix]))
		{
			--prefix;
		}

		// The suffix may not reach into the prefix on either side, or a repeated character would
		// be counted as shared twice and the span would come out negative.
		const std::size_t room = shared - std::min(prefix, shared);
		std::size_t suffix = 0;

		while (suffix < room && oldLine[oldLine.size() - 1 - suffix] == newLine[newLine.size() - 1 - suffix])
		{
			++suffix;
		}

		std::size_t to = oldLine.size() - suffix;

		// Forward to the end of the character the last difference falls in, for the same reason.
		while (to < oldLine.size() && IsContinuation(oldLine[to]))
		{
			++to;
		}

		return {prefix, std::max(prefix, to)};
	}
}
