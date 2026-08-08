#include "corepch.h"

#include "Core/Text/FileProbe.h"

#include "Core/Fs/FileSystem.h"

#include <fstream>
#include <string>
#include <system_error>

namespace core::text
{
    FileProbe ClassifySample(std::string_view sample, std::uint64_t size)
    {
        FileProbe probe;
        probe.size = size;
        probe.encoding = DetectEncoding(sample);

        const bool isUtf16 =
            probe.encoding == Encoding::Utf16Le || probe.encoding == Encoding::Utf16Be;

        // UTF-16 text is half NUL bytes, so the mark has to be believed before the NUL
        // check runs or every UTF-16 file would be called binary. Load refuses it by
        // name instead.
        if (!isUtf16 && sample.find('\0') != std::string_view::npos)
        {
            probe.kind = FileKind::Binary;
            return probe;
        }

        if (size > MaximumFileSize)
            probe.kind = FileKind::TooLarge;
        else if (size > LargeFileThreshold)
            probe.kind = FileKind::LargeText;
        else
            probe.kind = FileKind::Text;

        return probe;
    }

    Result<FileProbe> ProbeFile(const std::filesystem::path& path)
    {
        std::error_code code;
        const auto status = std::filesystem::status(path, code);

        if (code || !std::filesystem::exists(status))
            return Error{ErrorCode::NotFound, "No such file: " + fs::ToUtf8(path)};

        if (!std::filesystem::is_regular_file(status))
            return Error{ErrorCode::InvalidArgument, "Not a file: " + fs::ToUtf8(path)};

        const std::uintmax_t size = std::filesystem::file_size(path, code);

        if (code)
            return Error{ErrorCode::IoFailure, "Cannot measure file: " + fs::ToUtf8(path)};

        std::ifstream stream{path, std::ios::binary};

        if (!stream.is_open())
            return Error{ErrorCode::AccessDenied, "Cannot open file: " + fs::ToUtf8(path)};

        std::string sample(ProbeSampleSize, '\0');
        stream.read(sample.data(), static_cast<std::streamsize>(sample.size()));
        sample.resize(static_cast<std::size_t>(stream.gcount()));

        return ClassifySample(sample, static_cast<std::uint64_t>(size));
    }
}
