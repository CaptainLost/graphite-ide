#include <doctest/doctest.h>

#include "Core/Base/NonCopyable.h"

#include <type_traits>
#include <utility>

TEST_CASE("A type inheriting from NonCopyable remains movable")
{
	struct Movable : Core::NonCopyable
	{
		explicit Movable(int initialValue) : Value(initialValue) {}
		Movable(Movable&&) = default;
		Movable& operator=(Movable&&) = default;

		int Value = 0;
	};

	static_assert(std::is_move_constructible_v<Movable>);
	static_assert(std::is_move_assignable_v<Movable>);
	static_assert(!std::is_copy_constructible_v<Movable>);

	Movable source{7};
	Movable moved{std::move(source)};

	CHECK(moved.Value == 7);
}
