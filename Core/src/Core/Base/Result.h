#pragma once

#include "Core/Base/Assert.h"

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace core
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
        Result(T value) : m_data(std::move(value)) {}
        Result(Error error) : m_data(std::move(error)) {}

        [[nodiscard]] bool HasValue() const noexcept { return m_data.index() == 0; }
        explicit operator bool() const noexcept { return HasValue(); }

        T& Value() &
        {
            CORE_ASSERT(HasValue(), "Result::Value() called on an error result");
            return std::get<0>(m_data);
        }

        const T& Value() const&
        {
            CORE_ASSERT(HasValue(), "Result::Value() called on an error result");
            return std::get<0>(m_data);
        }

        T&& Value() &&
        {
            CORE_ASSERT(HasValue(), "Result::Value() called on an error result");
            return std::get<0>(std::move(m_data));
        }

        T ValueOr(T fallback) const&
        {
            return HasValue() ? std::get<0>(m_data) : std::move(fallback);
        }

        const Error& GetError() const
        {
            CORE_ASSERT(!HasValue(), "Result::GetError() called on a success result");
            return std::get<1>(m_data);
        }

    private:
        std::variant<T, Error> m_data;
    };

    /// Operation with no return value — carries only error information.
    template<>
    class [[nodiscard]] Result<void>
    {
    public:
        Result() = default;
        Result(Error error) : m_error(std::move(error)) {}

        static Result Ok() { return Result{}; }

        [[nodiscard]] bool HasValue() const noexcept { return !m_error.has_value(); }
        explicit operator bool() const noexcept { return HasValue(); }

        const Error& GetError() const
        {
            CORE_ASSERT(!HasValue(), "Result<void>::GetError() called on a success result");
            return *m_error;
        }

    private:
        std::optional<Error> m_error;
    };
}
