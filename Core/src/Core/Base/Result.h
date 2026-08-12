#pragma once

#include "Core/Base/Assert.h"

#include <optional>
#include <string>
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
		Unsupported
	};

	struct Error
	{
		ErrorCode Code = ErrorCode::None;
		std::string Message;
	};

	template<typename T>
	class Result
	{
	public:
		Result(T value) : m_Data(std::move(value)) {}
		Result(Error error) : m_Data(std::move(error)) {}

		bool HasValue() const { return m_Data.index() == 0; }
		explicit operator bool() const { return HasValue(); }

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

	template<>
	class Result<void>
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