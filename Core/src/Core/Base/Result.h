#pragma once

#include "Core/Base/Assert.h"

#include <concepts>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace Core {

	enum class ErrorCode
	{
		None,
		NotFound,
		AccessDenied,
		InvalidArgument,
		IoFailure,
		Unsupported,
		TooLarge
	};

	struct Error
	{
		ErrorCode Code = ErrorCode::None;
		std::string Message;
	};

	// Not constrained to std::movable, though the body needs it: a class may name Result<Self> in
	// its own definition, where it is still incomplete, and the answer would be remembered as false.
	template<typename Value>
		requires std::is_void_v<Value> || (std::is_object_v<Value> && !std::same_as<Value, Error>)
	class [[nodiscard]] Result
	{
	public:
		Result(Value value) : m_ValueOrError(std::move(value)) {}
		Result(Error error) : m_ValueOrError(std::move(error)) {}

		bool HasValue() const { return std::holds_alternative<Value>(m_ValueOrError); }
		explicit operator bool() const { return HasValue(); }

		Value& GetValue() &
		{
			CORE_ASSERT(HasValue(), "Result::GetValue() called on an error result");
			return std::get<Value>(m_ValueOrError);
		}

		const Value& GetValue() const&
		{
			CORE_ASSERT(HasValue(), "Result::GetValue() called on an error result");
			return std::get<Value>(m_ValueOrError);
		}

		// By value, not Value&&: a Value&& would point into the temporary result, which dies at the
		// semicolon, so `const auto& path = Open().GetValue();` would read freed memory.
		Value GetValue() &&
		{
			CORE_ASSERT(HasValue(), "Result::GetValue() called on an error result");
			return std::get<Value>(std::move(m_ValueOrError));
		}

		Value ValueOr(Value fallback) const&
		{
			return HasValue() ? std::get<Value>(m_ValueOrError) : std::move(fallback);
		}

		const Error& GetError() const
		{
			CORE_ASSERT(!HasValue(), "Result::GetError() called on a success result");
			return std::get<Error>(m_ValueOrError);
		}

	private:
		std::variant<Value, Error> m_ValueOrError;
	};

	template<>
	class [[nodiscard]] Result<void>
	{
	public:
		Result() = default;
		Result(Error error) : m_Error(std::move(error)) {}

		static Result Ok() { return Result{}; }

		bool HasValue() const { return !m_Error.has_value(); }
		explicit operator bool() const { return HasValue(); }

		const Error& GetError() const
		{
			CORE_ASSERT(!HasValue(), "Result<void>::GetError() called on a success result");
			return *m_Error;
		}

	private:
		std::optional<Error> m_Error;
	};
}