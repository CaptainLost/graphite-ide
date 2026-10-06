#include <doctest/doctest.h>

#include "Core/Base/Result.h"

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>

using Core::Error;
using Core::ErrorCode;
using Core::Result;

namespace {
	constexpr std::size_t kLengthBeyondSmallStringBuffer = 64;

	std::string StringLongerThanSmallStringBuffer()
	{
		return std::string(kLengthBeyondSmallStringBuffer, 'x');
	}

	Result<std::string> MakeResultHoldingLongString()
	{
		return Result<std::string>{StringLongerThanSmallStringBuffer()};
	}

	template<typename Candidate>
	concept NamesAResult = requires { typename Result<Candidate>; };
}

TEST_CASE("Result stores a value")
{
	Result<int> result{42};

	CHECK(result.HasValue());
	CHECK(static_cast<bool>(result));
	CHECK(result.GetValue() == 42);
}

TEST_CASE("Result stores an error")
{
	Result<int> result{Error{.Code = ErrorCode::NotFound, .Message = "no such file"}};

	CHECK_FALSE(result.HasValue());
	CHECK_FALSE(static_cast<bool>(result));
	CHECK(result.GetError().Code == ErrorCode::NotFound);
	CHECK(result.GetError().Message == "no such file");
}

TEST_CASE("ValueOr returns the fallback value on error")
{
	Result<int> succeeded{7};
	Result<int> failed{Error{.Code = ErrorCode::IoFailure, .Message = "disk"}};

	CHECK(succeeded.ValueOr(-1) == 7);
	CHECK(failed.ValueOr(-1) == -1);
}

TEST_CASE("Result works with a move-only type")
{
	Result<std::unique_ptr<std::string>> result{std::make_unique<std::string>("content")};

	REQUIRE(result.HasValue());
	std::unique_ptr<std::string> taken = std::move(result).GetValue();

	REQUIRE(taken != nullptr);
	CHECK(*taken == "content");
}

TEST_CASE("Result<void> distinguishes success from error")
{
	Result<void> succeeded = Result<void>::Ok();
	Result<void> failed{Error{.Code = ErrorCode::AccessDenied, .Message = "no access"}};

	CHECK(succeeded.HasValue());
	CHECK_FALSE(failed.HasValue());
	CHECK(failed.GetError().Code == ErrorCode::AccessDenied);
}

TEST_CASE("GetValue on a temporary result returns the value rather than a reference into it so a const reference keeps it alive")
{
	static_assert(std::is_same_v<decltype(std::declval<Result<std::string>>().GetValue()), std::string>);
	static_assert(std::is_same_v<decltype(std::declval<Result<std::unique_ptr<int>>>().GetValue()),
								 std::unique_ptr<int>>);

	const std::string& held = MakeResultHoldingLongString().GetValue();

	CHECK(held == StringLongerThanSmallStringBuffer());
}

TEST_CASE("Result refuses Error and references as its value type where the type is named rather than as an ambiguous constructor or a variant error inside the class")
{
	static_assert(NamesAResult<int>);
	static_assert(NamesAResult<void>);
	static_assert(!NamesAResult<Error>);
	static_assert(!NamesAResult<int&>);

	constexpr bool kStaticAssertionsCompiled = true;
	CHECK(kStaticAssertionsCompiled);
}
