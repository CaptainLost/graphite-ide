#include "App/Version.h"

#include "App/VersionNumbers.h"

namespace Graphite {

	const char* Version::String()
	{
		return GRAPHITE_VERSION_TEXT;
	}
}
