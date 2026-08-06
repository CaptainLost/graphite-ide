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

    /// Wynik operacji, ktora moze sie nie udac. Zastepuje wyjatki na granicach modulow.
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
            CORE_ASSERT(HasValue(), "Result::Value() na wyniku bledu");
            return std::get<0>(m_data);
        }

        const T& Value() const&
        {
            CORE_ASSERT(HasValue(), "Result::Value() na wyniku bledu");
            return std::get<0>(m_data);
        }

        T&& Value() &&
        {
            CORE_ASSERT(HasValue(), "Result::Value() na wyniku bledu");
            return std::get<0>(std::move(m_data));
        }

        T ValueOr(T fallback) const&
        {
            return HasValue() ? std::get<0>(m_data) : std::move(fallback);
        }

        const Error& GetError() const
        {
            CORE_ASSERT(!HasValue(), "Result::GetError() na wyniku sukcesu");
            return std::get<1>(m_data);
        }

    private:
        std::variant<T, Error> m_data;
    };

    /// Operacja bez wartosci zwracanej — niesie wylacznie informacje o bledzie.
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
            CORE_ASSERT(!HasValue(), "Result<void>::GetError() na wyniku sukcesu");
            return *m_error;
        }

    private:
        std::optional<Error> m_error;
    };
}
