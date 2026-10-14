#pragma once

#include "Core/Base/Result.h"
#include "Core/Platform/CurrentPlatform.h"

#include <string>
#include <string_view>

namespace Graphite {

	struct TemplateValues
	{
		std::string_view Version;
		Core::PlatformTarget Platform;
	};

	Core::Result<std::string> ExpandPackageTemplate(std::string_view pattern, const TemplateValues& values);

	std::string_view OperatingSystemName(Core::OperatingSystem system);

	std::string_view ArchitectureName(Core::Architecture machine);

	std::string RuntimeIdentifier(Core::PlatformTarget platform);

	std::string_view ExecutableSuffix(Core::OperatingSystem system);

	// WARNING: No version may take one of these names: a release tagged "installed.json" would replace the record.
	inline constexpr std::string_view kRecordFileName = "installed.json";
	inline constexpr std::string_view kLockFileName = "install.lock";

	// WARNING: Core::WriteFileAtomically falls back to ".tmp1", ".tmp2" and on, which the sweep must clear too.
	bool IsTemporaryName(std::string_view name);

	std::string TemporaryNameOf(std::string_view name);

	// WARNING: A temporary name is refused too, because the start-up sweep removes anything so named.
	bool IsSafeDirectoryName(std::string_view name);
}
