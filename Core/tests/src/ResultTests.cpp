#include <doctest/doctest.h>

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"

#include <memory>
#include <string>
#include <type_traits>

using Core::Error;
using Core::ErrorCode;
using Core::Result;

TEST_CASE("Result stores a value")
{
    Result<int> result{42};

    CHECK(result.HasValue());
    CHECK(static_cast<bool>(result));
    CHECK(result.Value() == 42);
}

TEST_CASE("Result stores an error")
{
    Result<int> result{Error{ErrorCode::NotFound, "no such file"}};

    CHECK_FALSE(result.HasValue());
    CHECK_FALSE(static_cast<bool>(result));
    CHECK(result.GetError().code == ErrorCode::NotFound);
    CHECK(result.GetError().message == "no such file");
}

TEST_CASE("ValueOr returns the fallback value on error")
{
    Result<int> ok{7};
    Result<int> failed{Error{ErrorCode::IoFailure, "disk"}};

    CHECK(ok.ValueOr(-1) == 7);
    CHECK(failed.ValueOr(-1) == -1);
}

TEST_CASE("Result works with a move-only type")
{
    Result<std::unique_ptr<std::string>> result{std::make_unique<std::string>("content")};

    REQUIRE(result.HasValue());
    std::unique_ptr<std::string> taken = std::move(result).Value();

    REQUIRE(taken != nullptr);
    CHECK(*taken == "content");
}

TEST_CASE("Result<void> distinguishes success from error")
{
    Result<void> ok = Result<void>::Ok();
    Result<void> failed{Error{ErrorCode::AccessDenied, "no access"}};

    CHECK(ok.HasValue());
    CHECK_FALSE(failed.HasValue());
    CHECK(failed.GetError().code == ErrorCode::AccessDenied);
}

TEST_CASE("A type inheriting from NonCopyable remains movable")
{
    struct Movable : Core::NonCopyable
    {
        explicit Movable(int v) : value(v) {}
        Movable(Movable&&) = default;
        Movable& operator=(Movable&&) = default;

        int value = 0;
    };

    static_assert(std::is_move_constructible_v<Movable>);
    static_assert(std::is_move_assignable_v<Movable>);
    static_assert(!std::is_copy_constructible_v<Movable>);

    Movable source{7};
    Movable moved{std::move(source)};

    CHECK(moved.value == 7);
}
