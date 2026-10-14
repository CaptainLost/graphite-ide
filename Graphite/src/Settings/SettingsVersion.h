#pragma once

namespace Core {
	class JsonFile;
}

namespace Graphite {

	inline constexpr int kSettingsVersion = 1;

	bool StampSettingsVersion(Core::JsonFile& file);
}
