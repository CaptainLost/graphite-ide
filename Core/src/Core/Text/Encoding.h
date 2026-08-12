#pragma once

#include <cstddef>
#include <string_view>

namespace Core {

	enum class Encoding
	{
		Utf8,
		Utf8Bom,
		Utf16Le,
		Utf16Be,
		Unknown
	};

	std::size_t BomLength(std::string_view bytes);

	std::size_t BomLength(Encoding encoding);

	std::string_view BomBytes(Encoding encoding);

	Encoding DetectEncoding(std::string_view bytes);

	bool IsValidUtf8(std::string_view bytes);

	std::size_t NextCharacter(std::string_view bytes, std::size_t offset);

	std::size_t PreviousCharacter(std::string_view bytes, std::size_t offset);

	std::string_view EncodingName(Encoding encoding);
}