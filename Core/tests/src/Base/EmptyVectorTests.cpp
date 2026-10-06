#include <doctest/doctest.h>

#include "Core/Base/EmptyVector.h"

#include <vector>

TEST_CASE("SharedEmptyVector is one empty vector per element type")
{
	const std::vector<int>& fromFirstCall = Core::SharedEmptyVector<int>();
	const std::vector<int>& fromSecondCall = Core::SharedEmptyVector<int>();

	CHECK(fromFirstCall.empty());
	CHECK(&fromFirstCall == &fromSecondCall);
}
