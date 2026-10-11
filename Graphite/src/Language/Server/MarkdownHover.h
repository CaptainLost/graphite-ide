#pragma once

#include "Language/Api/HoverInfo.h"

#include <string_view>

namespace Graphite {

	// A server that names no symbol kind in its hover: its first fenced block is the declaration, the rest is the
	// documentation as plain text. Section headings arrive in the server's own language, so none are looked for.
	HoverInfo ParseMarkdownHover(std::string_view markdown);
}
