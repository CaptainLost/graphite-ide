#include "Settings/SettingsVersion.h"

#include "Core/Base/JsonFile.h"

#include <charconv>
#include <string>
#include <system_error>

namespace Graphite {

	namespace {
		constexpr const char* kVersionKey = "settingsVersion";

		int StoredVersion(const Core::JsonFile& file)
		{
			const std::string stored = file.Get(kVersionKey);
			int version = 0;
			const auto [end, error] = std::from_chars(stored.data(), stored.data() + stored.size(), version);

			if (error != std::errc{} || end != stored.data() + stored.size())
			{
				return 0;
			}

			return version;
		}
	}

	bool StampSettingsVersion(Core::JsonFile& file)
	{
		if (StoredVersion(file) > kSettingsVersion)
		{
			return false;
		}

		file.Set(kVersionKey, std::to_string(kSettingsVersion));
		return true;
	}
}
