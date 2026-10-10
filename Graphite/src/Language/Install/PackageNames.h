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

	// Takes {version}, {os}, {arch}, {rid} and {exe}. Anything else in braces, or a brace left open, is an error and
	// not text, so a mistake in the catalogue fails its test instead of a user's download.
	Core::Result<std::string> ExpandPackageTemplate(std::string_view pattern, const TemplateValues& values);

	std::string_view OperatingSystemName(Core::OperatingSystem system);

	std::string_view ArchitectureName(Core::Architecture machine);

	std::string RuntimeIdentifier(Core::PlatformTarget platform);

	std::string_view ExecutableSuffix(Core::OperatingSystem system);

	// No version may take one of these names: a release tagged "installed.json" would replace the record.
	inline constexpr std::string_view kRecordFileName = "installed.json";
	inline constexpr std::string_view kLockFileName = "install.lock";

	// Core::WriteFileAtomically falls back to ".tmp1", ".tmp2" and on, which the sweep must clear too.
	bool IsTemporaryName(std::string_view name);

	std::string TemporaryNameOf(std::string_view name);

	// A temporary name is refused too, because the start-up sweep removes anything so named.
	bool IsSafeDirectoryName(std::string_view name);
}
