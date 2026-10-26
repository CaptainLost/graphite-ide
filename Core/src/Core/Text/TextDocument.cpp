#include "corepch.h"

#include "Core/Text/TextDocument.h"

#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/Codec.h"
#include "Core/Text/FileProbe.h"
#include "Core/Text/SharedFileReader.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Core {

	namespace {
		constexpr std::size_t kReadChunkBytes = 1024 * 1024;
		constexpr std::size_t kBytesBetweenCancellationChecks = 1024 * 1024;
		constexpr std::size_t kUtf8SliceBytes = 4 * 1024 * 1024;
		constexpr std::size_t kLongestRunOfContinuationBytes = 3;
		constexpr unsigned char kContinuationByteMask = 0xC0;
		constexpr unsigned char kContinuationBytePattern = 0x80;
		constexpr std::size_t kCrlfLength = 2;

		Error ReadGivenUp(const std::filesystem::path& path)
		{
			return Error{.Code = ErrorCode::Cancelled, .Message = "Reading was given up: " + ToUtf8(path)};
		}

		std::string NotTextMessage(Encoding encoding, bool hasByteOrderMark, std::string_view content,
								   const std::filesystem::path& path)
		{
			const std::string name{EncodingName(encoding)};

			if (hasByteOrderMark)
			{
				std::string message = "This file is marked " + name;
				message += ", but decoding it gives a NUL "
						   "character at byte ";
				message += std::to_string(content.find('\0'));
				message += ", and no text contains one. It is most likely UTF-32: the "
						   "UTF-32 LE mark FF FE 00 00 begins with the UTF-16 LE mark, so "
						   "the two cannot be told apart from the first two bytes alone. "
						   "Re-save it as UTF-8 or UTF-16 to open it here: ";
				message += ToUtf8(path);

				return message;
			}

			std::string message = "This file carries no byte order mark, so it was taken for " + name;
			message += " from the shape of its first ";
			message += std::to_string(ProbeSampleSize);
			message += " bytes; decoding all of it gives control characters, so it is not "
					   "text in that encoding after all: ";
			message += ToUtf8(path);

			return message;
		}

		// A NUL is the one thing a mark cannot vouch for: the UTF-32 LE mark FF FE 00 00 begins with
		// the UTF-16 LE mark, so a UTF-32 file arrives here decoded as UTF-16 and full of NULs.
		bool DecodedContentIsText(std::string_view content, bool hasByteOrderMark)
		{
			if (hasByteOrderMark)
			{
				const bool containsNul = content.find('\0') != std::string_view::npos;
				return !containsNul;
			}

			return LooksLikeText(content);
		}

		// WARNING: Reads until the file runs dry, not for `reportedSize` bytes: a file still being appended to grows
		// between the stat and this read, and stopping at the earlier size would truncate the document silently.
		Result<std::string> ReadWholeFile(const std::filesystem::path& path, std::uintmax_t reportedSize,
										  const CancellationToken& cancellation)
		{
			Result<SharedFileReader> reader = SharedFileReader::Open(path);

			if (!reader)
			{
				return reader.GetError();
			}

			std::string fileBytes;
			fileBytes.reserve(static_cast<std::size_t>(reportedSize));

			std::vector<char> chunk(kReadChunkBytes);

			while (true)
			{
				if (cancellation.IsCancelled())
				{
					return ReadGivenUp(path);
				}

				const Result<std::size_t> bytesRead = reader.GetValue().ReadInto(chunk);

				if (!bytesRead)
				{
					return bytesRead.GetError();
				}

				const bool nothingLeftToRead = bytesRead.GetValue() == 0;
				if (nothingLeftToRead)
				{
					break;
				}

				fileBytes.append(chunk.data(), bytesRead.GetValue());

				const bool grewPastMaximumWhileBeingRead = fileBytes.size() > MaximumFileSize;
				if (grewPastMaximumWhileBeingRead)
				{
					return Error{.Code = ErrorCode::InvalidArgument,
								 .Message = "File is too large to open (grew past " + std::to_string(MaximumFileSize) + " bytes while being read): " + ToUtf8(path)};
				}
			}

			return fileBytes;
		}

		bool IsContinuationByte(char byte)
		{
			return (static_cast<unsigned char>(byte) & kContinuationByteMask) == kContinuationBytePattern;
		}

		std::size_t SliceEndAtCharacterStart(std::string_view content, std::size_t wantedEnd)
		{
			if (wantedEnd >= content.size())
			{
				return content.size();
			}

			std::size_t sliceEnd = wantedEnd;

			for (std::size_t steppedBack = 0; steppedBack < kLongestRunOfContinuationBytes && IsContinuationByte(content[sliceEnd]); ++steppedBack)
			{
				--sliceEnd;
			}

			return sliceEnd;
		}

		// WARNING: Slices end where a character starts, so a character is never split and the answer is the whole text's.
		std::optional<bool> IsValidUtf8UnlessCancelled(std::string_view content, const CancellationToken& cancellation)
		{
			std::size_t sliceStart = 0;

			while (sliceStart < content.size())
			{
				if (cancellation.IsCancelled())
				{
					return std::nullopt;
				}

				const std::size_t sliceEnd = SliceEndAtCharacterStart(content, sliceStart + kUtf8SliceBytes);

				if (!IsValidUtf8(content.substr(sliceStart, sliceEnd - sliceStart)))
				{
					return false;
				}

				sliceStart = sliceEnd;
			}

			return true;
		}

		// A lone '\r' stays: taken for a classic Mac line end, it would split a line at a stray byte.
		bool NormaliseUnlessCancelled(std::string_view text, NormalisedLineEndings& normalised, const CancellationToken& cancellation)
		{
			normalised.Content.reserve(text.size());

			std::size_t crlfCount = 0;
			std::size_t lfCount = 0;
			std::size_t nextCancellationCheck = 0;
			std::size_t byteIndex = 0;

			while (byteIndex < text.size())
			{
				if (byteIndex >= nextCancellationCheck)
				{
					if (cancellation.IsCancelled())
					{
						return false;
					}

					nextCancellationCheck = byteIndex + kBytesBetweenCancellationChecks;
				}

				const bool startsCrlf =
					text[byteIndex] == '\r' && byteIndex + 1 < text.size() && text[byteIndex + 1] == '\n';

				if (startsCrlf)
				{
					++crlfCount;
					normalised.Content.push_back('\n');
					normalised.ContentLineStarts.push_back(normalised.Content.size());
					byteIndex += kCrlfLength;
					continue;
				}

				normalised.Content.push_back(text[byteIndex]);

				if (text[byteIndex] == '\n')
				{
					++lfCount;
					normalised.ContentLineStarts.push_back(normalised.Content.size());
				}

				++byteIndex;
			}

			normalised.Mixed = crlfCount > 0 && lfCount > 0;
			normalised.Ending = crlfCount > lfCount ? LineEnding::Crlf : LineEnding::Lf;

			return true;
		}
	}

	Result<TextDocument> TextDocument::FromContent(std::string bytes, std::filesystem::path path)
	{
		const CancellationToken neverCancelled;

		return FromContent(std::move(bytes), std::move(path), neverCancelled);
	}

	Result<TextDocument> TextDocument::FromContent(std::string bytes, std::filesystem::path path,
												   const CancellationToken& cancellation)
	{
		const std::string_view sampleProbeFileReads = std::string_view{bytes}.substr(0, ProbeSampleSize);
		const Encoding sniffedEncoding = SniffEncoding(sampleProbeFileReads);

		// Measured on the bytes, never taken from the sniffed encoding: SniffEncoding can name UTF-16
		// for bytes that carry no mark at all.
		const bool hasByteOrderMark = LeadingByteOrderMarkLength(bytes) > 0;

		Result<std::string> decoded = DecodeWithoutByteOrderMark(std::move(bytes), sniffedEncoding);

		if (!decoded)
		{
			return decoded.GetError();
		}

		if (cancellation.IsCancelled())
		{
			return ReadGivenUp(path);
		}

		const std::string content = std::move(decoded).GetValue();
		const bool contentWasDecoded = TransformsContent(sniffedEncoding);

		if (contentWasDecoded && !DecodedContentIsText(content, hasByteOrderMark))
		{
			return Error{.Code = ErrorCode::InvalidArgument,
						 .Message = NotTextMessage(sniffedEncoding, hasByteOrderMark, content, path)};
		}

		const bool decodedContentIsValid = true;
		const std::optional<bool> contentIsValidUtf8 =
			contentWasDecoded ? decodedContentIsValid : IsValidUtf8UnlessCancelled(content, cancellation);

		if (!contentIsValidUtf8)
		{
			return ReadGivenUp(path);
		}

		NormalisedLineEndings normalised;

		if (!NormaliseUnlessCancelled(content, normalised, cancellation))
		{
			return ReadGivenUp(path);
		}

		TextDocument document;
		document.m_Path = std::move(path);
		document.m_Encoding = sniffedEncoding;
		document.m_HadByteOrderMark = hasByteOrderMark;
		document.m_ByteOrderMarkEncoding = sniffedEncoding;

		if (!*contentIsValidUtf8)
		{
			document.m_Encoding = Encoding::Unknown;
			document.m_ReadOnly = true;
		}

		document.m_LineEnding = normalised.Ending;
		document.m_WasMixed = normalised.Mixed;
		document.m_Buffer = TextBuffer::FromIndexedText(std::move(normalised.Content), std::move(normalised.ContentLineStarts));

		return document;
	}

	Result<TextDocument> TextDocument::Load(const std::filesystem::path& path)
	{
		const CancellationToken neverCancelled;

		return Load(path, neverCancelled);
	}

	Result<TextDocument> TextDocument::Load(const std::filesystem::path& path, const CancellationToken& cancellation)
	{
		if (cancellation.IsCancelled())
		{
			return ReadGivenUp(path);
		}

		std::error_code statusError;
		const std::filesystem::file_status status = std::filesystem::status(path, statusError);

		if (statusError || !std::filesystem::exists(status))
		{
			return Error{.Code = ErrorCode::NotFound, .Message = "No such file: " + ToUtf8(path)};
		}

		if (!std::filesystem::is_regular_file(status))
		{
			return Error{.Code = ErrorCode::InvalidArgument, .Message = "Not a file (it is a directory): " + ToUtf8(path)};
		}

		std::error_code sizeError;
		const std::uintmax_t fileSize = std::filesystem::file_size(path, sizeError);

		if (sizeError)
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = "Cannot read file: " + ToUtf8(path)};
		}

		// Refused before the read reserves `fileSize` bytes, so a lying or enormous size cannot throw
		// std::bad_alloc across this Result boundary.
		if (fileSize > MaximumFileSize)
		{
			return Error{.Code = ErrorCode::InvalidArgument, .Message = "File is too large to open (" + std::to_string(fileSize) + " bytes): " + ToUtf8(path)};
		}

		std::error_code writeTimeError;
		const std::filesystem::file_time_type writeTimeBeforeRead = std::filesystem::last_write_time(path, writeTimeError);

		Result<std::string> fileBytes = ReadWholeFile(path, fileSize, cancellation);

		if (!fileBytes)
		{
			return fileBytes.GetError();
		}

		Result<TextDocument> document = FromContent(std::move(fileBytes).GetValue(), path, cancellation);

		// A write time that cannot be read costs the document its stamp, not the load; no test can make last_write_time fail here.
		const bool writeTimeWasRead = !writeTimeError;
		if (document && writeTimeWasRead)
		{
			document.GetValue().m_DiskStamp = DiskStamp{.Time = writeTimeBeforeRead, .Size = fileSize};
		}

		return document;
	}

	NormalisedLineEndings NormaliseLineEndings(std::string_view text)
	{
		const CancellationToken neverCancelled;
		NormalisedLineEndings normalised;

		NormaliseUnlessCancelled(text, normalised, neverCancelled);

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

	std::string TextDocument::EncodeToBytes() const
	{
		std::string encodedBytes;

		if (m_HadByteOrderMark)
		{
			encodedBytes += ByteOrderMarkBytes(m_ByteOrderMarkEncoding);
		}

		encodedBytes += Encode(WithLineEnding(m_Buffer.Materialize(), m_LineEnding), m_Encoding);

		return encodedBytes;
	}

	bool TextDocument::CheckChangedOnDisk() const
	{
		if (!m_DiskStamp)
		{
			return false;
		}

		std::error_code writeTimeError;
		const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(m_Path, writeTimeError);

		if (writeTimeError)
		{
			return false;
		}

		std::error_code sizeError;
		const std::uintmax_t fileSize = std::filesystem::file_size(m_Path, sizeError);

		if (sizeError)
		{
			return false;
		}

		return writeTime != m_DiskStamp->Time || fileSize != m_DiskStamp->Size;
	}

	Result<void> TextDocument::Save()
	{
		const Result<void> writeOutcome = WriteFileAtomically(m_Path, EncodeToBytes());

		if (!writeOutcome)
		{
			return writeOutcome.GetError();
		}

		MarkSaved();
		m_Buffer.BreakUndoGroup();
		AcceptDiskState();

		return {};
	}

	Result<void> TextDocument::ReloadFromDisk()
	{
		Result<TextDocument> fresh = Load(m_Path);

		if (!fresh)
		{
			return fresh.GetError();
		}

		TextDocument freshDocument = std::move(fresh).GetValue();
		const std::string freshText = freshDocument.m_Buffer.Materialize();

		m_Buffer.ReplaceContent(freshText);

		m_Encoding = freshDocument.m_Encoding;
		m_LineEnding = freshDocument.m_LineEnding;
		m_WasMixed = freshDocument.m_WasMixed;
		m_ReadOnly = freshDocument.m_ReadOnly;
		m_HadByteOrderMark = freshDocument.m_HadByteOrderMark;
		m_ByteOrderMarkEncoding = freshDocument.m_ByteOrderMarkEncoding;

		MarkSaved();

		// Copied even when the fresh load took no stamp: the old stamp describes content the buffer
		// no longer holds.
		m_DiskStamp = freshDocument.m_DiskStamp;

		return {};
	}

	void TextDocument::AcceptDiskState()
	{
		std::error_code writeTimeError;
		const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(m_Path, writeTimeError);

		std::error_code sizeError;
		const std::uintmax_t fileSize = std::filesystem::file_size(m_Path, sizeError);

		const bool stampWasReadWhole = !writeTimeError && !sizeError;
		// A failed read keeps the previous stamp instead of clearing it, because a document with no stamp never reports a change on disk.
		if (!stampWasReadWhole)
		{
			return;
		}

		m_DiskStamp = DiskStamp{.Time = writeTime, .Size = fileSize};
	}
}
