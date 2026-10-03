#include "Git/ChangeBases.h"

#include "Core/Git/Repository.h"
#include "Core/Text/Codec.h"

#include <utility>

namespace Graphite {

	std::optional<std::string> DecodeBaseText(std::string_view bytes)
	{
		if (bytes.size() > Core::MaximumDiffTextSize)
		{
			return std::nullopt;
		}

		auto decoded = Core::Decode(bytes, Core::SniffEncoding(bytes));

		if (!decoded.HasValue())
		{
			return std::nullopt;
		}

		std::string text = std::move(decoded).GetValue();

		constexpr std::string_view kByteOrderMark = "\xEF\xBB\xBF";

		if (text.starts_with(kByteOrderMark))
		{
			text.erase(0, kByteOrderMark.size());
		}

		if (!Core::LooksLikeText(text))
		{
			return std::nullopt;
		}

		// The buffer holds LF whatever the file uses on disk; Core::WithLineEnding puts the
		// file's own endings back only when it is saved. A CRLF blob compared as it is would
		// mark every line changed.
		std::string lf;
		lf.reserve(text.size());

		for (std::size_t i = 0; i < text.size(); ++i)
		{
			if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
			{
				continue;
			}

			lf.push_back(text[i]);
		}

		return lf;
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
