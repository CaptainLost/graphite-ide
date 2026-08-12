#pragma once

#include "Core/Base/Result.h"
#include "Core/Text/Encoding.h"

#include <string>
#include <string_view>

namespace Core {

	Result<std::string> Decode(std::string_view bytes, Encoding encoding);

	std::string Encode(std::string_view utf8, Encoding encoding);

	// UTF-16 is three tests, and each one earns its place: at least 90% of bytes at one
	// parity are NUL (not 100% - U+0100 puts a NUL at the other parity), at least two NUL
	// bytes present, and the decode has to look like text. A file of small 16-bit integers
	// has the same byte shape, and writing it back as text destroys it.
	Encoding SniffEncoding(std::string_view bytes);

	bool LooksLikeText(std::string_view utf8);

	bool TransformsContent(Encoding encoding);
}