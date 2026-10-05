#include <doctest/doctest.h>

#include "Core/Base/EmptyVector.h"

#include <vector>

TEST_CASE("SharedEmptyVector is one empty vector per element type")
{
	const std::vector<int>& first = Core::SharedEmptyVector<int>();
	const std::vector<int>& second = Core::SharedEmptyVector<int>();

	CHECK(first.empty());
	CHECK(&first == &second);
}
