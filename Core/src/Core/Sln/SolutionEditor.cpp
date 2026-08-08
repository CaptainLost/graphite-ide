#include "corepch.h"

#include "Core/Sln/SolutionEditor.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Sln/SlnxEditor.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <random>
#include <fstream>
#include <sstream>

namespace core::sln
{
    namespace
    {
        /// Compared case-insensitively, because the filesystem this runs on is.
        std::string LowercaseExtension(const std::filesystem::path& path)
        {
            std::string extension = fs::ToUtf8(path.extension());

            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char character)
                           { return static_cast<char>(std::tolower(character)); });

            return extension;
        }
    }

    std::string NewGuid()
    {
        // A GUID is a hundred and twenty-two random bits in a fixed layout, not a service
        // the operating system has to provide. Generating it here rather than asking
        // CoCreateGuid removes a platform branch, the empty result that branch produced
        // everywhere else, and the check every caller had to carry for a case it could do
        // nothing about — a check that would have been the whole of AddProject's behaviour
        // on any other system.
        //
        // random_device seeds and mt19937_64 generates: a conforming random_device is
        // allowed to be deterministic, and drawing every bit from it is slow where it is
        // not. thread_local because the engine is stateful and this may be called from the
        // UI thread and a worker.
        static thread_local std::mt19937_64 engine{std::random_device{}()};

        std::uniform_int_distribution<std::uint64_t> distribution;

        std::uint64_t high = distribution(engine);
        std::uint64_t low = distribution(engine);

        // Version 4 and variant 1. Neither solution format checks, but a value calling
        // itself a UUID should say which kind it is.
        high = (high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        low = (low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

        char buffer[39] = {};

        std::snprintf(buffer, sizeof(buffer), "{%08X-%04X-%04X-%04X-%012llX}",
                      static_cast<unsigned int>(high >> 32),
                      static_cast<unsigned int>((high >> 16) & 0xFFFFULL),
                      static_cast<unsigned int>(high & 0xFFFFULL),
                      static_cast<unsigned int>(low >> 48),
                      static_cast<unsigned long long>(low & 0xFFFFFFFFFFFFULL));

        return std::string{buffer};
    }

    Result<std::unique_ptr<SolutionEditor>> SolutionEditor::Open(
        const std::filesystem::path& solution)
    {
        std::error_code code;

        if (!std::filesystem::is_regular_file(solution, code))
        {
            return Error{ErrorCode::NotFound,
                         std::format("no solution file at '{}'", solution.string())};
        }

        // .sln is readable but not yet writable; a second plan adds it behind this same
        // call. Refusing here is what keeps a caller from discovering that halfway
        // through an edit.
        if (LowercaseExtension(solution) != ".slnx")
        {
            return Error{ErrorCode::Unsupported,
                         std::format("'{}' is not a format this build can edit",
                                     solution.string())};
        }

        std::ifstream stream{solution, std::ios::binary};

        if (!stream.is_open())
            return Error{ErrorCode::IoFailure, std::format("cannot open '{}'", solution.string())};

        std::ostringstream buffer;
        buffer << stream.rdbuf();

        std::unique_ptr<SolutionEditor> editor =
            std::make_unique<SlnxEditor>(solution, buffer.str());

        return editor;
    }
}
