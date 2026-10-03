#include "corepch.h"

#include "Core/Text/FileProbe.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/Codec.h"

#include <fstream>
#include <string>
#include <system_error>

namespace Core {

	FileProbe ClassifySample(std::string_view sample, std::uint64_t size)
	{
		FileProbe probe;
		probe.Size = size;
		probe.FileEncoding = SniffEncoding(sample);

		const bool isUtf16 =
			probe.FileEncoding == Encoding::Utf16Le || probe.FileEncoding == Encoding::Utf16Be;

		// UTF-16 text is half NUL bytes, so it has to be recognised before this check or
		// every UTF-16 file would be called binary.
		if (!isUtf16 && sample.find('\0') != std::string_view::npos)
		{
			probe.Kind = FileKind::Binary;
			return probe;
		}

		if (size > MaximumFileSize)
		{
			probe.Kind = FileKind::TooLarge;
		}
		else if (size > LargeFileThreshold)
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
		std::error_code code;
		const auto status = std::filesystem::status(path, code);

		if (code || !std::filesystem::exists(status))
		{
			return Error{ErrorCode::NotFound, "No such file: " + ToUtf8(path)};
		}

		if (!std::filesystem::is_regular_file(status))
		{
			return Error{ErrorCode::InvalidArgument, "Not a file: " + ToUtf8(path)};
		}

		const std::uintmax_t size = std::filesystem::file_size(path, code);

		if (code)
		{
			return Error{ErrorCode::IoFailure, "Cannot measure file: " + ToUtf8(path)};
		}

		std::ifstream stream{path, std::ios::binary};

		if (!stream.is_open())
		{
			return Error{ErrorCode::AccessDenied, "Cannot open file: " + ToUtf8(path)};
		}

		std::string sample(ProbeSampleSize, '\0');
		stream.read(sample.data(), static_cast<std::streamsize>(sample.size()));
		sample.resize(static_cast<std::size_t>(stream.gcount()));

		return ClassifySample(sample, static_cast<std::uint64_t>(size));
	}
}