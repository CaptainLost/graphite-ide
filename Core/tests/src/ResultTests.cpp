#include <doctest/doctest.h>

#include "Core/Base/Result.h"

#include <memory>
#include <string>

using core::Error;
using core::ErrorCode;
using core::Result;

TEST_CASE("Result przechowuje wartosc")
{
    Result<int> result{42};

    CHECK(result.HasValue());
    CHECK(static_cast<bool>(result));
    CHECK(result.Value() == 42);
}

TEST_CASE("Result przechowuje blad")
{
    Result<int> result{Error{ErrorCode::NotFound, "nie ma pliku"}};

    CHECK_FALSE(result.HasValue());
    CHECK_FALSE(static_cast<bool>(result));
    CHECK(result.GetError().code == ErrorCode::NotFound);
    CHECK(result.GetError().message == "nie ma pliku");
}

TEST_CASE("ValueOr zwraca wartosc zastepcza przy bledzie")
{
    Result<int> ok{7};
    Result<int> failed{Error{ErrorCode::IoFailure, "dysk"}};

    CHECK(ok.ValueOr(-1) == 7);
    CHECK(failed.ValueOr(-1) == -1);
}

TEST_CASE("Result dziala z typem tylko-przenoszalnym")
{
    Result<std::unique_ptr<std::string>> result{std::make_unique<std::string>("tresc")};

    REQUIRE(result.HasValue());
    std::unique_ptr<std::string> taken = std::move(result).Value();

    REQUIRE(taken != nullptr);
    CHECK(*taken == "tresc");
}

TEST_CASE("Result<void> rozroznia sukces od bledu")
{
    Result<void> ok = Result<void>::Ok();
    Result<void> failed{Error{ErrorCode::AccessDenied, "brak dostepu"}};

    CHECK(ok.HasValue());
    CHECK_FALSE(failed.HasValue());
    CHECK(failed.GetError().code == ErrorCode::AccessDenied);
}
