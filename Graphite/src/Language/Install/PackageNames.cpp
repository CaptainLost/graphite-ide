#include "Language/Install/PackageNames.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/RelativePath.h"

#include <format>
#include <optional>

namespace Graphite {

	namespace {

		constexpr std::size_t kLongestDirectoryName = 64;
		constexpr std::string_view kTemporarySuffix = ".tmp";
		constexpr std::string_view kDigits = "0123456789";

		bool IsLetterOrDigit(char character)
		{
			const bool isLowerCase = character >= 'a' && character <= 'z';
			const bool isUpperCase = character >= 'A' && character <= 'Z';
			const bool isDigit = character >= '0' && character <= '9';
			return isLowerCase || isUpperCase || isDigit;
		}

		bool IsDirectoryNameCharacter(char character)
		{
			return IsLetterOrDigit(character) || character == '.' || character == '-' || character == '_' || character == '+';
		}

		bool IsInstallerFileName(std::string_view name)
		{
			return Core::EqualsIgnoringCase(name, kRecordFileName) || Core::EqualsIgnoringCase(name, kLockFileName);
		}

		std::string_view RuntimeSystemName(Core::OperatingSystem system)
		{
			switch (system)
			{
			case Core::OperatingSystem::Windows:
				return "win";
			case Core::OperatingSystem::Linux:
				return "linux";
			case Core::OperatingSystem::MacOS:
				break;
			}

			return "osx";
		}

		std::optional<std::string> PlaceholderValue(std::string_view placeholder, const TemplateValues& values)
		{
			if (placeholder == "version")
			{
				return std::string{values.Version};
			}

			if (placeholder == "os")
			{
				return std::string{OperatingSystemName(values.Platform.System)};
			}

			if (placeholder == "arch")
			{
				return std::string{ArchitectureName(values.Platform.Machine)};
			}

			if (placeholder == "rid")
			{
				return RuntimeIdentifier(values.Platform);
			}

			if (placeholder == "exe")
			{
				return std::string{ExecutableSuffix(values.Platform.System)};
			}

			return std::nullopt;
		}
	}

	Core::Result<std::string> ExpandPackageTemplate(std::string_view pattern, const TemplateValues& values)
	{
		std::string expanded;
		std::size_t position = 0;

		while (position < pattern.size())
		{
			const std::size_t open = pattern.find('{', position);

			if (open == std::string_view::npos)
			{
				expanded.append(pattern.substr(position));
				break;
			}

			expanded.append(pattern.substr(position, open - position));
			const std::size_t close = pattern.find('}', open);

			if (close == std::string_view::npos)
			{
				return Core::Error{.Code = Core::ErrorCode::InvalidArgument,
								   .Message = std::format("'{}' leaves a placeholder unclosed", pattern)};
			}

			const std::string_view placeholder = pattern.substr(open + 1, close - open - 1);
			const std::optional<std::string> value = PlaceholderValue(placeholder, values);

			if (!value)
			{
				return Core::Error{.Code = Core::ErrorCode::InvalidArgument,
								   .Message = std::format("'{}' names an unknown placeholder {{{}}}", pattern, placeholder)};
			}

			expanded.append(*value);
			position = close + 1;
		}

		return expanded;
	}

	std::string_view OperatingSystemName(Core::OperatingSystem system)
	{
		switch (system)
		{
		case Core::OperatingSystem::Windows:
			return "windows";
		case Core::OperatingSystem::Linux:
			return "linux";
		case Core::OperatingSystem::MacOS:
			break;
		}

		return "mac";
	}

	std::string_view ArchitectureName(Core::Architecture machine)
	{
		switch (machine)
		{
		case Core::Architecture::X64:
			return "x64";
		case Core::Architecture::Arm64:
			break;
		}

		return "arm64";
	}

	std::string RuntimeIdentifier(Core::PlatformTarget platform)
	{
		return std::format("{}-{}", RuntimeSystemName(platform.System), ArchitectureName(platform.Machine));
	}

	std::string_view ExecutableSuffix(Core::OperatingSystem system)
	{
		switch (system)
		{
		case Core::OperatingSystem::Windows:
			return ".exe";
		case Core::OperatingSystem::Linux:
		case Core::OperatingSystem::MacOS:
			break;
		}

		return "";
	}

	bool IsTemporaryName(std::string_view name)
	{
		const std::string_view withoutCounter = Core::WithoutTrailing(name, kDigits);

		return withoutCounter.size() >= kTemporarySuffix.size() &&
			   Core::EqualsIgnoringCase(withoutCounter.substr(withoutCounter.size() - kTemporarySuffix.size()),
										kTemporarySuffix);
	}

	std::string TemporaryNameOf(std::string_view name)
	{
		return std::string{name} + std::string{kTemporarySuffix};
	}

	bool IsSafeDirectoryName(std::string_view name)
	{
		if (name.empty() || name.size() > kLongestDirectoryName || !IsLetterOrDigit(name.front()) || name.ends_with('.') ||
			IsTemporaryName(name) || IsInstallerFileName(name) || !Core::IsSafeRelativePath(name))
		{
			return false;
		}

		for (const char character : name)
		{
			if (!IsDirectoryNameCharacter(character))
			{
				return false;
			}
		}

		return true;
	}
}
