#include <doctest/doctest.h>

#include "App/AboutDialog.h"
#include "App/Version.h"
#include "App/VersionNumbers.h"

#include <string>

TEST_CASE("The licences text is never empty, whether or not the notices file sits next to the executable")
{
	const std::string notices = Graphite::AboutDialog::LoadNotices();

	CHECK_FALSE(notices.empty());
}

TEST_CASE("The version shown is the one the resource script is built from")
{
	CHECK(std::string(Graphite::Version::String()) ==
		  std::to_string(GRAPHITE_VERSION_MAJOR) + "." + std::to_string(GRAPHITE_VERSION_MINOR) + "." +
			  std::to_string(GRAPHITE_VERSION_PATCH));
}
