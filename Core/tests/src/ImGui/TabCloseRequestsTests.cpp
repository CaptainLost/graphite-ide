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

TEST_CASE("A close request survives the visible part of the name changing")
{
	TabCloseRequests requests;
	requests.Request("Old title###session-1");
	requests.EndFrame();

	CHECK(requests.Take("New title###session-1"));
}

TEST_CASE("A name without an identity marker matches only itself")
{
	TabCloseRequests requests;
	requests.Request("Settings");
	requests.EndFrame();

	CHECK_FALSE(requests.Take("Settings 2"));
	CHECK_FALSE(requests.Take("x###Settings"));
	CHECK(requests.Take("Settings"));
}

TEST_CASE("Names with different identities do not match")
{
	TabCloseRequests requests;
	requests.Request("a###x");
	requests.EndFrame();

	CHECK_FALSE(requests.Take("a###y"));
	CHECK(requests.Take("a###x"));
}
