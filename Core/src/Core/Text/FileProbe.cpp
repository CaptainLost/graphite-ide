#include "corepch.h"

#include "Core/Text/FileProbe.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/Codec.h"
#include "Core/Text/TextDocument.h"

#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace Core {

	FileProbe ClassifySample(std::string_view sample, std::uint64_t fileSize)
	{
		FileProbe probe{.FileEncoding = SniffEncoding(sample), .Size = fileSize};

		const bool isUtf16 =
			probe.FileEncoding == Encoding::Utf16Le || probe.FileEncoding == Encoding::Utf16Be;

		// UTF-16 text is half NUL bytes, so a NUL byte means binary only once UTF-16 has been
		// ruled out; checked the other way round, every UTF-16 file would be called binary.
		const bool looksBinary = !isUtf16 && sample.find('\0') != std::string_view::npos;

		if (looksBinary)
		{
			probe.Kind = FileKind::Binary;
			return probe;
		}

		if (fileSize > MaximumFileSize)
		{
			probe.Kind = FileKind::TooLarge;
		}
		else if (fileSize > LargeFileThreshold)
		{
			probe.Kind = FileKind::LargeText;
		}
		else
		{
			probe.Kind = FileKind::Text;
		}

		return probe;
	}

	Result<FileProbe> ProbeFile(const std::filesystem::path& path)
	{
		std::error_code queryError;
		const std::filesystem::file_status status = std::filesystem::status(path, queryError);

		if (queryError || !std::filesystem::exists(status))
		{
			return Error{.Code = ErrorCode::NotFound, .Message = "No such file: " + ToUtf8(path)};
		}

		if (!std::filesystem::is_regular_file(status))
		{
			return Error{.Code = ErrorCode::InvalidArgument, .Message = "Not a file: " + ToUtf8(path)};
		}

		const std::uintmax_t fileSize = std::filesystem::file_size(path, queryError);

		if (queryError)
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = "Cannot measure file: " + ToUtf8(path)};
		}

		std::ifstream stream{path, std::ios::binary};

		if (!stream.is_open())
		{
			return Error{.Code = ErrorCode::AccessDenied, .Message = "Cannot open file: " + ToUtf8(path)};
		}

		std::string sample(ProbeSampleSize, '\0');
		stream.read(sample.data(), static_cast<std::streamsize>(sample.size()));
		sample.resize(static_cast<std::size_t>(stream.gcount()));

		return ClassifySample(sample, static_cast<std::uint64_t>(fileSize));
	}

	DiskText ReadDiskText(const std::filesystem::path& path)
	{
		const Result<FileProbe> probeResult = ProbeFile(path);

		if (!probeResult)
		{
			return DiskText{.Outcome = DiskTextOutcome::Unreadable};
		}

		const FileProbe& probe = probeResult.GetValue();

		switch (probe.Kind)
		{
		case FileKind::Binary:
			return DiskText{.Outcome = DiskTextOutcome::Binary};
		case FileKind::TooLarge:
			return DiskText{.Outcome = DiskTextOutcome::TooLarge};
		case FileKind::Text:
		case FileKind::LargeText:
			break;
		}

		Result<std::string> bytes = ReadFileBytes(path);

		if (!bytes)
		{
			return DiskText{.Outcome = DiskTextOutcome::Unreadable};
		}

		// Read as opening the file reads it, without the mark and with LF line ends, so a line and a column found
		// here are the same ones in the document the file opens as.
		Result<std::string> text = DecodeWithoutByteOrderMark(std::move(bytes).GetValue(), probe.FileEncoding);

		if (!text)
		{
			return DiskText{.Outcome = DiskTextOutcome::Undecodable};
		}

		return DiskText{.Outcome = DiskTextOutcome::Read, .Text = NormaliseLineEndings(text.GetValue()).Content};
	}
}