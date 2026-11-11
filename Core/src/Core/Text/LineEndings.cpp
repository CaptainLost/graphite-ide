#include "corepch.h"

#include "Core/Text/LineEndings.h"

#include <utility>

namespace Core {

	namespace {
		constexpr std::size_t kBytesBetweenCancellationChecks = 1024 * 1024;
		constexpr std::size_t kCrlfLength = 2;
	}

	// WARNING: Rewritten in place, which only ever shortens the text, so a load holds one copy of the file, not two.
	bool NormaliseLineEndingsInPlaceUnlessCancelled(std::string& text, NormalisedLineEndings& normalised, const CancellationToken& cancellation)
	{
		std::size_t crlfCount = 0;
		std::size_t lfCount = 0;
		std::size_t nextCancellationCheck = 0;
		std::size_t readIndex = 0;
		std::size_t writeIndex = 0;

		while (readIndex < text.size())
		{
			if (readIndex >= nextCancellationCheck)
			{
				if (cancellation.IsCancelled())
				{
					return false;
				}

				nextCancellationCheck = readIndex + kBytesBetweenCancellationChecks;
			}

			const bool startsCrlf =
				text[readIndex] == '\r' && readIndex + 1 < text.size() && text[readIndex + 1] == '\n';

			if (startsCrlf)
			{
				++crlfCount;
				text[writeIndex] = '\n';
				++writeIndex;
				normalised.ContentLineStarts.push_back(writeIndex);
				readIndex += kCrlfLength;
				continue;
			}

			const char byte = text[readIndex];
			text[writeIndex] = byte;
			++writeIndex;

			if (byte == '\n')
			{
				++lfCount;
				normalised.ContentLineStarts.push_back(writeIndex);
			}

			++readIndex;
		}

		text.resize(writeIndex);
		normalised.Content = std::move(text);
		normalised.Mixed = crlfCount > 0 && lfCount > 0;
		normalised.Ending = crlfCount > lfCount ? LineEnding::Crlf : LineEnding::Lf;

		return true;
	}

	NormalisedLineEndings NormaliseLineEndings(std::string_view text)
	{
		const CancellationToken neverCancelled;
		std::string content{text};
		NormalisedLineEndings normalised;

		NormaliseLineEndingsInPlaceUnlessCancelled(content, normalised, neverCancelled);

		return normalised;
	}

	std::string WithLineEnding(std::string_view text, LineEnding ending)
	{
		if (ending == LineEnding::Lf)
		{
			return std::string{text};
		}

		std::string textWithCrlf;
		textWithCrlf.reserve(text.size());

		for (const char byte : text)
		{
			if (byte == '\n')
			{
				textWithCrlf.push_back('\r');
			}

			textWithCrlf.push_back(byte);
		}

		return textWithCrlf;
	}
}
