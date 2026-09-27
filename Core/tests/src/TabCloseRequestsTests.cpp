#include <doctest/doctest.h>

#include "Core/ImGui/TabCloseRequests.h"

using Core::TabCloseRequests;

TEST_CASE("A close request is not due in the frame it was asked")
{
	TabCloseRequests requests;
	requests.Request("main.cpp###doc1");

	CHECK_FALSE(requests.Take("main.cpp###doc1"));
}

TEST_CASE("A close request is taken exactly once in the next frame")
{
	TabCloseRequests requests;
	requests.Request("main.cpp###doc1");
	requests.EndFrame();

	CHECK(requests.Take("main.cpp###doc1"));
	CHECK_FALSE(requests.Take("main.cpp###doc1"));
}

TEST_CASE("A close request nobody takes is dropped after its frame")
{
	// The window closed some other way before its turn came; a later window of the same name
	// must not inherit the request.
	TabCloseRequests requests;
	requests.Request("main.cpp###doc1");
	requests.EndFrame();
	requests.EndFrame();

	CHECK_FALSE(requests.Take("main.cpp###doc1"));
}

TEST_CASE("Taking one name leaves the others due")
{
	TabCloseRequests requests;
	requests.Request("a");
	requests.Request("b");
	requests.EndFrame();

	CHECK(requests.Take("b"));
	CHECK(requests.Take("a"));
	CHECK_FALSE(requests.Take("c"));
}
