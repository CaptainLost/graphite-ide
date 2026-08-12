#pragma once

#include "Core/Base/Assert.h"

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace Core
{
    enum class ErrorCode
    {
        None,
        NotFound,
        AccessDenied,
        InvalidArgument,
        IoFailure,
        Unsupported
    };

    struct Error
    {
        ErrorCode code = ErrorCode::None;
        std::string message;
    };

    /// Result of an operation that may fail. Replaces exceptions at module boundaries.
    template<typename T>
    class [[nodiscard]] Result
    {
    public:
        Result(T value) : m_Data(std::move(value)) {}
        Result(Error error) : m_Data(std::move(error)) {}

        [[nodiscard]] bool HasValue() const noexcept { return m_Data.index() == 0; }
        explicit operator bool() const noexcept { return HasValue(); }

        T& GetValue() &
        {
            CORE_ASSERT(HasValue(), "Result::GetValue() called on an error result");
            return std::get<0>(m_Data);
        }

        const T& GetValue() const&
        {
            CORE_ASSERT(HasValue(), "Result::GetValue() called on an error result");
            return std::get<0>(m_Data);
        }

        T&& GetValue() &&
        {
            CORE_ASSERT(HasValue(), "Result::GetValue() called on an error result");
            return std::get<0>(std::move(m_Data));
        }

        T ValueOr(T fallback) const&
        {
            return HasValue() ? std::get<0>(m_Data) : std::move(fallback);
        }

        const Error& GetError() const
        {
            CORE_ASSERT(!HasValue(), "Result::GetError() called on a success result");
            return std::get<1>(m_Data);
        }

    private:
        std::variant<T, Error> m_Data;
    };

    /// Operation with no return value — carries only error information.
    template<>
    class [[nodiscard]] Result<void>
    {
    public:
        Result() = default;
        Result(Error error) : m_Error(std::move(error)) {}

        static Result Ok() { return Result{}; }

        [[nodiscard]] bool HasValue() const noexcept { return !m_Error.has_value(); }
        explicit operator bool() const noexcept { return HasValue(); }

        const Error& GetError() const
        {
            CORE_ASSERT(!HasValue(), "Result<void>::GetError() called on a success result");
            return *m_Error;
        }

    private:
        std::optional<Error> m_Error;
    };
}
