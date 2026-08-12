#pragma once

#include "Core/Base/Result.h"
#include "Core/Text/Encoding.h"

#include <string>
#include <string_view>

namespace Core {

	Result<std::string> Decode(std::string_view bytes, Encoding encoding);

	std::string Encode(std::string_view utf8, Encoding encoding);

	Encoding SniffEncoding(std::string_view bytes);

	bool LooksLikeText(std::string_view utf8);

	bool TransformsContent(Encoding encoding);
}