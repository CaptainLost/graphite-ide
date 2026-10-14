#include "Git/Changes/ChangeBases.h"

#include "Core/Git/Repository.h"
#include "Core/Text/Codec.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/TextDocument.h"

#include <utility>

namespace Graphite {

	std::optional<std::string> DecodeBaseText(std::string_view bytes)
	{
		if (bytes.size() > Core::MaximumDiffTextSize)
		{
			return std::nullopt;
		}

		Core::Result<std::string> decoded = Core::Decode(bytes, Core::SniffEncoding(bytes));

		if (!decoded.HasValue())
		{
			return std::nullopt;
		}

		std::string text = std::move(decoded).GetValue();
		text.erase(0, text.size() - Core::WithoutUtf8ByteOrderMark(text).size());

		if (!Core::LooksLikeText(text))
		{
			return std::nullopt;
		}

		// WARNING: The buffer holds LF whatever the file uses on disk, so a CRLF blob compared as it
		// is would mark every line changed.
		return Core::NormaliseLineEndings(text).Content;
	}

	ChangeBases MakeChangeBases(std::optional<std::string> headBytes,
								std::optional<std::string> indexBytes)
	{
		ChangeBases bases;
		bases.Tracked = true;

		if (headBytes)
		{
			bases.Head = DecodeBaseText(*headBytes);
			bases.Unusable = bases.Unusable || !bases.Head.has_value();
		}

		if (indexBytes)
		{
			bases.Index = DecodeBaseText(*indexBytes);
			bases.Unusable = bases.Unusable || !bases.Index.has_value();
		}

		return bases;
	}
}
