#include "corepch.h"

#include "Core/Text/Codec.h"

#include "Core/Jobs/CancellationToken.h"
#include "Core/Text/Utf8.h"

#include <cstdint>
#include <string>
#include <utility>

namespace Core {

	namespace {
		constexpr std::uint32_t kHighSurrogateFirst = 0xD800;
		constexpr std::uint32_t kHighSurrogateLast = 0xDBFF;
		constexpr std::uint32_t kLowSurrogateFirst = 0xDC00;
		constexpr std::uint32_t kLowSurrogateLast = 0xDFFF;
		constexpr std::uint32_t kFirstSupplementary = 0x10000;
		constexpr std::uint32_t kSurrogatePayloadBits = 10;
		constexpr std::uint32_t kSurrogatePayloadMask = 0x3FFu;

		constexpr std::uint32_t kBitsPerByte = 8;
		constexpr std::uint32_t kLowByteMask = 0xFFu;

		constexpr std::size_t kUtf16UnitBytes = 2;
		constexpr std::size_t kSurrogatePairBytes = 4;

		constexpr unsigned char kFirstByteAfterC0Controls = 0x20u;
		constexpr unsigned char kDeleteByte = 0x7Fu;
		constexpr unsigned char kC1ControlLeadByte = 0xC2u;
		constexpr unsigned char kFirstC1ControlTrailByte = 0x80u;
		constexpr unsigned char kLastC1ControlTrailByte = 0x9Fu;

		constexpr std::size_t kFewestNulsForUtf16 = 2;

		constexpr std::size_t kBytesBetweenCancellationChecks = 1024 * 1024;

		// Nine tenths rather than all of them: U+0100 and its neighbours put a NUL at the other
		// parity, and a file may legitimately contain a few.
		constexpr std::size_t kDominantNulShareNumerator = 9;
		constexpr std::size_t kDominantNulShareDenominator = 10;

		enum class ByteOrder
		{
			LittleEndian,
			BigEndian
		};

		bool IsHighSurrogate(std::uint32_t unit)
		{
			return unit >= kHighSurrogateFirst && unit <= kHighSurrogateLast;
		}

		bool IsLowSurrogate(std::uint32_t unit)
		{
			return unit >= kLowSurrogateFirst && unit <= kLowSurrogateLast;
		}

		std::uint32_t ReadUnit(std::string_view bytes, std::size_t unitStart,
							   ByteOrder byteOrder)
		{
			const std::uint32_t firstByte = static_cast<std::uint32_t>(
				static_cast<unsigned char>(bytes[unitStart]));
			const std::uint32_t secondByte = static_cast<std::uint32_t>(
				static_cast<unsigned char>(bytes[unitStart + 1]));

			return byteOrder == ByteOrder::LittleEndian ? (secondByte << kBitsPerByte) | firstByte
														: (firstByte << kBitsPerByte) | secondByte;
		}

		void AppendUnit(std::string& utf16, std::uint32_t unit, ByteOrder byteOrder)
		{
			const char lowByte = static_cast<char>(unit & kLowByteMask);
			const char highByte = static_cast<char>((unit >> kBitsPerByte) & kLowByteMask);

			const bool isLittleEndian = byteOrder == ByteOrder::LittleEndian;

			utf16.push_back(isLittleEndian ? lowByte : highByte);
			utf16.push_back(isLittleEndian ? highByte : lowByte);
		}

		Result<std::string> DecodeUtf16(std::string_view bytes, ByteOrder byteOrder, const CancellationToken& cancellation)
		{
			if (bytes.size() % kUtf16UnitBytes != 0)
			{
				return Error{.Code = ErrorCode::InvalidArgument, .Message = "UTF-16 file is damaged: odd number of bytes (" + std::to_string(bytes.size()) + ")"};
			}

			std::string utf8;
			utf8.reserve(bytes.size());

			std::size_t unitStart = 0;
			std::size_t nextCancellationCheck = 0;

			while (unitStart < bytes.size())
			{
				if (unitStart >= nextCancellationCheck)
				{
					if (cancellation.IsCancelled())
					{
						return Error{.Code = ErrorCode::Cancelled, .Message = "Decoding UTF-16 was given up"};
					}

					nextCancellationCheck = unitStart + kBytesBetweenCancellationChecks;
				}

				const std::uint32_t unit = ReadUnit(bytes, unitStart, byteOrder);

				if (IsLowSurrogate(unit))
				{
					return Error{.Code = ErrorCode::InvalidArgument, .Message = "UTF-16 file is damaged: a low surrogate at byte " + std::to_string(unitStart) + " has no high surrogate before it"};
				}

				if (IsHighSurrogate(unit))
				{
					if (unitStart + kSurrogatePairBytes > bytes.size())
					{
						return Error{.Code = ErrorCode::InvalidArgument, .Message = "UTF-16 file is damaged: a high surrogate at byte " + std::to_string(unitStart) + " is the last unit"};
					}

					const std::uint32_t lowSurrogate = ReadUnit(bytes, unitStart + kUtf16UnitBytes, byteOrder);

					if (!IsLowSurrogate(lowSurrogate))
					{
						return Error{.Code = ErrorCode::InvalidArgument, .Message = "UTF-16 file is damaged: a high surrogate at byte " + std::to_string(unitStart) + " is not followed by a low one"};
					}

					const std::uint32_t highPayload = unit - kHighSurrogateFirst;
					const std::uint32_t lowPayload = lowSurrogate - kLowSurrogateFirst;
					const std::uint32_t codePoint = kFirstSupplementary + (highPayload << kSurrogatePayloadBits) + lowPayload;

					AppendUtf8(utf8, static_cast<char32_t>(codePoint));
					unitStart += kSurrogatePairBytes;
					continue;
				}

				AppendUtf8(utf8, static_cast<char32_t>(unit));
				unitStart += kUtf16UnitBytes;
			}

			return utf8;
		}

		std::string EncodeUtf16(std::string_view utf8, ByteOrder byteOrder)
		{
			std::string utf16;
			utf16.reserve(utf8.size() * kUtf16UnitBytes);

			std::size_t characterStart = 0;

			while (characterStart < utf8.size())
			{
				const std::uint32_t codePoint = static_cast<std::uint32_t>(DecodeUtf8(utf8, characterStart).value_or(kReplacementCharacter));

				if (codePoint < kFirstSupplementary)
				{
					AppendUnit(utf16, codePoint, byteOrder);
				}
				else
				{
					const std::uint32_t supplementaryOffset = codePoint - kFirstSupplementary;

					AppendUnit(utf16, kHighSurrogateFirst + (supplementaryOffset >> kSurrogatePayloadBits), byteOrder);
					AppendUnit(utf16, kLowSurrogateFirst + (supplementaryOffset & kSurrogatePayloadMask), byteOrder);
				}
			}

			return utf16;
		}

		enum class Transform
		{
			PassThrough,
			Utf16LittleEndian,
			Utf16BigEndian
		};

		// No default, so a new Encoding enumerator is a build error until its Transform is
		// decided here.
		Transform TransformFor(Encoding encoding)
		{
			switch (encoding)
			{
			case Encoding::Utf16Le:
				return Transform::Utf16LittleEndian;
			case Encoding::Utf16Be:
				return Transform::Utf16BigEndian;
			case Encoding::Utf8:
			case Encoding::Utf8Bom:
			case Encoding::Unknown:
				break;
			}

			return Transform::PassThrough;
		}

		bool IsC1ControlAt(std::string_view utf8, std::size_t byteIndex)
		{
			const bool isLeadByte = static_cast<unsigned char>(utf8[byteIndex]) == kC1ControlLeadByte;
			const bool hasTrailByte = byteIndex + 1 < utf8.size();

			if (!isLeadByte || !hasTrailByte)
			{
				return false;
			}

			const unsigned char trailByte = static_cast<unsigned char>(utf8[byteIndex + 1]);

			return trailByte >= kFirstC1ControlTrailByte && trailByte <= kLastC1ControlTrailByte;
		}

		bool IsC0ControlOtherThanTabOrLineBreak(unsigned char byte)
		{
			const bool isC0Control = byte < kFirstByteAfterC0Controls;
			const bool isTab = byte == '\t';
			const bool isLineFeed = byte == '\n';
			const bool isCarriageReturn = byte == '\r';

			return isC0Control && !isTab && !isLineFeed && !isCarriageReturn;
		}
	}

	// DEL and the C1 controls are not text, yet every byte that encodes them lies past the C0
	// range, so the C0 test alone lets them through.
	bool LooksLikeText(std::string_view utf8)
	{
		for (std::size_t byteIndex = 0; byteIndex < utf8.size(); ++byteIndex)
		{
			const unsigned char byte = static_cast<unsigned char>(utf8[byteIndex]);

			if (byte == kDeleteByte)
			{
				return false;
			}

			if (IsC1ControlAt(utf8, byteIndex))
			{
				return false;
			}

			if (IsC0ControlOtherThanTabOrLineBreak(byte))
			{
				return false;
			}
		}

		return true;
	}

	Result<std::string> Decode(std::string_view bytes, Encoding encoding)
	{
		const CancellationToken neverCancelled;

		return DecodeUnlessCancelled(bytes, encoding, neverCancelled);
	}

	Result<std::string> DecodeUnlessCancelled(std::string_view bytes, Encoding encoding, const CancellationToken& cancellation)
	{
		switch (TransformFor(encoding))
		{
		case Transform::Utf16LittleEndian:
			return DecodeUtf16(bytes, ByteOrder::LittleEndian, cancellation);
		case Transform::Utf16BigEndian:
			return DecodeUtf16(bytes, ByteOrder::BigEndian, cancellation);
		case Transform::PassThrough:
			break;
		}

		return std::string{bytes};
	}

	Result<std::string> DecodeWithoutByteOrderMark(std::string bytes, Encoding encoding)
	{
		const CancellationToken neverCancelled;

		return DecodeWithoutByteOrderMarkUnlessCancelled(std::move(bytes), encoding, neverCancelled);
	}

	// The mark is measured on the bytes, never taken from the encoding: unmarked bytes can be
	// sniffed as UTF-16 and must then lose nothing. A pass-through encoding is erased in place
	// because Decode would return a second whole copy of the file.
	Result<std::string> DecodeWithoutByteOrderMarkUnlessCancelled(std::string bytes, Encoding encoding,
																  const CancellationToken& cancellation)
	{
		const std::size_t markLength = LeadingByteOrderMarkLength(bytes);

		if (!TransformsContent(encoding))
		{
			bytes.erase(0, markLength);
			return bytes;
		}

		return DecodeUnlessCancelled(std::string_view{bytes}.substr(markLength), encoding, cancellation);
	}

	std::string Encode(std::string_view utf8, Encoding encoding)
	{
		switch (TransformFor(encoding))
		{
		case Transform::Utf16LittleEndian:
			return EncodeUtf16(utf8, ByteOrder::LittleEndian);
		case Transform::Utf16BigEndian:
			return EncodeUtf16(utf8, ByteOrder::BigEndian);
		case Transform::PassThrough:
			break;
		}

		return std::string{utf8};
	}

	bool TransformsContent(Encoding encoding)
	{
		return TransformFor(encoding) != Transform::PassThrough;
	}

	Encoding SniffEncoding(std::string_view bytes)
	{
		const Encoding markedEncoding = DetectEncoding(bytes);

		const bool bytesCarryByteOrderMark = markedEncoding != Encoding::Utf8;
		if (bytesCarryByteOrderMark)
		{
			return markedEncoding;
		}

		std::size_t nulsAtEvenOffsets = 0;
		std::size_t nulsAtOddOffsets = 0;

		for (std::size_t byteIndex = 0; byteIndex < bytes.size(); ++byteIndex)
		{
			if (bytes[byteIndex] != '\0')
			{
				continue;
			}

			const bool isEvenOffset = byteIndex % 2 == 0;
			if (isEvenOffset)
			{
				++nulsAtEvenOffsets;
			}
			else
			{
				++nulsAtOddOffsets;
			}
		}

		const std::size_t nulCount = nulsAtEvenOffsets + nulsAtOddOffsets;

		if (nulCount < kFewestNulsForUtf16)
		{
			return Encoding::Utf8;
		}

		const std::size_t nulsAtDominantParity = nulsAtEvenOffsets > nulsAtOddOffsets ? nulsAtEvenOffsets : nulsAtOddOffsets;

		const bool tooFewNulsShareOneParity = nulsAtDominantParity * kDominantNulShareDenominator < nulCount * kDominantNulShareNumerator;
		if (tooFewNulsShareOneParity)
		{
			return Encoding::Utf8;
		}

		const bool lowByteComesFirst = nulsAtOddOffsets > nulsAtEvenOffsets;
		const Encoding candidate = lowByteComesFirst ? Encoding::Utf16Le
													 : Encoding::Utf16Be;

		const std::size_t strayByteCount = bytes.size() % kUtf16UnitBytes;
		std::string_view wholeUnits = bytes;
		wholeUnits.remove_suffix(strayByteCount);

		const Result<std::string> decoded = Decode(wholeUnits, candidate);

		if (!decoded)
		{
			return Encoding::Utf8;
		}

		return LooksLikeText(decoded.GetValue()) ? candidate : Encoding::Utf8;
	}
}
