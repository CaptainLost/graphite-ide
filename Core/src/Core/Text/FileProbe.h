#pragma once

#include "Core/Base/Result.h"
#include "Core/Text/Encoding.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace Core
{
    /// What may be done with a file, decided before any of it is loaded.
    enum class FileKind
    {
        /// Small enough to open without asking.
        Text,
        /// Text, but past the threshold where the caller asks before loading it.
        LargeText,
        /// Holds a NUL byte that no encoding accounts for, so it is not text. Refused.
        /// UTF-16 is recognised before this rule is applied and is exempt from it — half
        /// of a UTF-16 file is NUL bytes, and the rule as it once read called every one of
        /// them binary.
        Binary,
        /// Past the ceiling. Refused even with confirmation.
        TooLarge
    };

    /// Above this the caller asks the user before loading.
    inline constexpr std::uint64_t LargeFileThreshold = 16ull * 1024 * 1024;

    /// Above this nothing is loaded. Half a gigabyte of content plus its line index is
    /// still comfortable for a 64-bit process; past it, even a successful load gives an
    /// editor in which nothing feels responsive.
    inline constexpr std::uint64_t MaximumFileSize = 512ull * 1024 * 1024;

    /// How much of the file the verdict is based on.
    inline constexpr std::size_t ProbeSampleSize = 8192;

    struct FileProbe
    {
        FileKind kind = FileKind::Text;
        Encoding encoding = Encoding::Utf8;
        std::uint64_t size = 0;
    };

    /// The whole decision, over bytes rather than a path, so it can be tested without
    /// laying files out on disk.
    ///
    /// A NUL byte past ProbeSampleSize goes unseen and the file counts as text. That is
    /// the price of never reading a file before deciding whether to read it, and it is
    /// paid deliberately: the alternative is scanning a 2 GB file to find out it should
    /// not be opened.
    [[nodiscard]] FileProbe ClassifySample(std::string_view sample, std::uint64_t size);

    /// Stats the file and reads at most ProbeSampleSize bytes of it. Fails only when the
    /// file cannot be looked at at all.
    [[nodiscard]] Result<FileProbe> ProbeFile(const std::filesystem::path& path);
}
