#include "corepch.h"

#include "Core/Fs/FileOperations.h"

#include "Core/Fs/FileSystem.h"

#include <array>
#include <cctype>
#include <string>

namespace core::fs
{
    namespace
    {
        bool EqualsIgnoringCase(std::string_view left, std::string_view right)
        {
            if (left.size() != right.size())
                return false;

            for (std::size_t i = 0; i < left.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(left[i])) !=
                    std::tolower(static_cast<unsigned char>(right[i])))
                {
                    return false;
                }
            }

            return true;
        }
    }

    std::filesystem::path MakeUniquePath(const std::filesystem::path& path)
    {
        std::error_code code;
        if (!std::filesystem::exists(path, code))
            return path;

        const std::filesystem::path parent = path.parent_path();
        const std::string stem = ToUtf8(path.stem());
        const std::string extension = ToUtf8(path.extension());

        std::filesystem::path candidate = parent / FromUtf8(stem + " copy" + extension);
        int attempt = 2;

        while (std::filesystem::exists(candidate, code))
        {
            candidate = parent / FromUtf8(stem + " copy " + std::to_string(attempt) + extension);
            ++attempt;
        }

        return candidate;
    }

    bool IsValidFileName(std::string_view name)
    {
        if (name.empty())
            return false;

        if (name.find_first_not_of(" \t") == std::string_view::npos)
            return false;

        if (name.find_first_of("\\/:*?\"<>|") != std::string_view::npos)
            return false;

        if (name.back() == '.' || name.back() == ' ')
            return false;

        // A reserved device name is reserved with any extension too: NUL.txt cannot be
        // created either. Compare against the part before the first dot.
        const std::size_t dot = name.find('.');
        const std::string_view base = dot == std::string_view::npos ? name : name.substr(0, dot);

        constexpr std::array<std::string_view, 22> kReserved{
            "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
            "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

        for (const std::string_view reserved : kReserved)
        {
            if (EqualsIgnoringCase(base, reserved))
                return false;
        }

        return true;
    }
}
