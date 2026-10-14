#include "Settings/SettingsVersion.h"

#include "Core/Base/JsonFile.h"
#include "Core/Base/Strings.h"

#include <string>

namespace Graphite {

	namespace {
		constexpr const char* kSettingsVersionKey = "settingsVersion";
		constexpr int kUnversionedFile = 0;

		int StoredVersion(const Core::JsonFile& file)
		{
			return Core::ParseNumber<int>(file.Get(kSettingsVersionKey)).value_or(kUnversionedFile);
		}
	}

	bool StampSettingsVersion(Core::JsonFile& file)
	{
		const bool writtenByNewerBuild = StoredVersion(file) > kSettingsVersion;

		if (writtenByNewerBuild)
		{
			return false;
		}

		file.Set(kSettingsVersionKey, std::to_string(kSettingsVersion));
		return true;
	}
}
