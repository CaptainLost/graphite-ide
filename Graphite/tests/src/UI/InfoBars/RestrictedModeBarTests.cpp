#include <doctest/doctest.h>

#include "UI/InfoBars/RestrictedModeBar.h"

#include <string>

TEST_CASE("The Trust All confirmation names the whole parent path and what trusting it means")
{
	const std::string message = Graphite::TrustAllMessage("D:/Downloads");

	CHECK(message.find("D:/Downloads") != std::string::npos);
	CHECK(message.find("every folder under") != std::string::npos);
	CHECK(message.find("language server") != std::string::npos);
	CHECK(message.find("background analysis") != std::string::npos);
}
