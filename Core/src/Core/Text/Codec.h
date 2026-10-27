#pragma once

#include "Core/Base/Result.h"
#include "Core/Text/Encoding.h"

#include <string>
#include <string_view>

namespace Core {

	class CancellationToken;

	Result<std::string> Decode(std::string_view bytes, Encoding encoding);

	Result<std::string> DecodeUnlessCancelled(std::string_view bytes, Encoding encoding, const CancellationToken& cancellation);

	Result<std::string> DecodeWithoutByteOrderMark(std::string bytes, Encoding encoding);

	Result<std::string> DecodeWithoutByteOrderMarkUnlessCancelled(std::string bytes, Encoding encoding,
																  const CancellationToken& cancellation);

	std::string Encode(std::string_view utf8, Encoding encoding);

	// Deliberately hard to convince of UTF-16: a file of small 16-bit integers has the same
	// byte shape, and writing it back out as text destroys it.
	Encoding SniffEncoding(std::string_view bytes);

	bool LooksLikeText(std::string_view utf8);

	bool TransformsContent(Encoding encoding);
}