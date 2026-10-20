#include "Theme/SchemeStore.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Color.h"
#include "Core/Base/JsonFile.h"
#include "Core/Base/Log.h"
#include "Core/FileSystem/FileName.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/Encoding.h"
#include "Persistence/FileBackup.h"
#include "Theme/BuiltInSchemes.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <system_error>

namespace Graphite {

	namespace {

		constexpr std::string_view kSyntaxPrefix = "syntax.";
		constexpr std::string_view kEditorPrefix = "editor.";
		constexpr std::string_view kChromePrefix = "ui.";
		constexpr std::string_view kGitPrefix = "git.";
		constexpr std::string_view kSessionPrefix = "sessions.";
		constexpr std::string_view kLayoutPrefix = "layout.";

		std::optional<std::size_t> IndexOfSyntax(std::string_view name)
		{
			for (std::size_t kindIndex = 0; kindIndex < static_cast<std::size_t>(TokenKind::Count); ++kindIndex)
			{
				if (NameOf(static_cast<TokenKind>(kindIndex)) == name)
				{
					return kindIndex;
				}
			}

			return std::nullopt;
		}

		std::optional<std::size_t> IndexOfEditor(std::string_view name)
		{
			for (std::size_t colorIndex = 0; colorIndex < static_cast<std::size_t>(EditorColor::Count); ++colorIndex)
			{
				if (NameOf(static_cast<EditorColor>(colorIndex)) == name)
				{
					return colorIndex;
				}
			}

			return std::nullopt;
		}

		std::optional<std::size_t> IndexOfChrome(std::string_view name)
		{
			for (std::size_t colorIndex = 0; colorIndex < static_cast<std::size_t>(ChromeColor::Count); ++colorIndex)
			{
				if (NameOf(static_cast<ChromeColor>(colorIndex)) == name)
				{
					return colorIndex;
				}
			}

			return std::nullopt;
		}

		std::optional<std::size_t> IndexOfGit(std::string_view name)
		{
			for (std::size_t colorIndex = 0; colorIndex < static_cast<std::size_t>(GitColor::Count); ++colorIndex)
			{
				if (NameOf(static_cast<GitColor>(colorIndex)) == name)
				{
					return colorIndex;
				}
			}

			return std::nullopt;
		}

		std::optional<std::size_t> IndexOfSession(std::string_view name)
		{
			for (std::size_t colorIndex = 0; colorIndex < static_cast<std::size_t>(SessionColor::Count); ++colorIndex)
			{
				if (NameOf(static_cast<SessionColor>(colorIndex)) == name)
				{
					return colorIndex;
				}
			}

			return std::nullopt;
		}

		std::optional<std::size_t> IndexOfLayout(std::string_view name)
		{
			for (std::size_t metricIndex = 0; metricIndex < kLayoutMetricCount; ++metricIndex)
			{
				if (NameOf(static_cast<LayoutMetric>(metricIndex)) == name)
				{
					return metricIndex;
				}
			}

			return std::nullopt;
		}

		std::optional<float> ParseMeasurement(const std::string& text)
		{
			float value = 0.0f;
			const char* const end = text.data() + text.size();
			const std::from_chars_result parsed = std::from_chars(text.data(), end, value);

			if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != end || !std::isfinite(value))
			{
				return std::nullopt;
			}

			return value;
		}

		bool HoldsCharacterForbiddenInFileName(std::string_view name)
		{
			for (const char character : name)
			{
				if (Core::IsForbiddenInFileName(character))
				{
					return true;
				}
			}

			return false;
		}
	}

	std::filesystem::path SchemeFile(const std::filesystem::path& directory, std::string_view name)
	{
		std::filesystem::path file = directory / Core::FromUtf8(name);
		file += L".json";
		return file;
	}

	ColorScheme LoadScheme(const std::filesystem::path& file)
	{
		ColorScheme scheme;

		std::error_code errorCode;

		if (!std::filesystem::exists(file, errorCode))
		{
			APP_WARN("Colour scheme '{}' is not there", Core::ToUtf8(file));
			return scheme;
		}

		const Core::JsonFile json = Core::JsonFile::Load(file);

		if (json.IsMalformed())
		{
			APP_WARN("Colour scheme '{}' does not parse; showing its base until it is fixed or saved again", Core::ToUtf8(file));
		}

		if (const std::optional<BuiltInScheme> base = ParseBuiltInName(json.Get("base")))
		{
			scheme.Base = *base;
		}
		else
		{
			APP_WARN("Colour scheme '{}' names an unknown base '{}'", Core::ToUtf8(file), json.Get("base"));
		}

		for (const std::string& key : json.CollectKeys())
		{
			if (key == "base")
			{
				continue;
			}

			if (std::string_view(key).starts_with(kLayoutPrefix))
			{
				const std::optional<std::size_t> index = IndexOfLayout(std::string_view(key).substr(kLayoutPrefix.size()));
				const std::optional<float> measurement = ParseMeasurement(json.Get(key));

				if (index && measurement)
				{
					const LayoutRange range = RangeOf(static_cast<LayoutMetric>(*index));

					scheme.Layout[*index] = std::clamp(*measurement, range.Smallest, range.Largest);
				}
				else
				{
					APP_WARN("Colour scheme '{}' has an unreadable measurement for '{}'", Core::ToUtf8(file), key);
				}

				continue;
			}

			const std::optional<Core::Color> color = Core::ParseHexColor(json.Get(key));

			if (!color)
			{
				APP_WARN("Colour scheme '{}' has an unreadable colour for '{}'", Core::ToUtf8(file), key);
				continue;
			}

			const std::string_view name = key;

			if (name.starts_with(kSyntaxPrefix))
			{
				if (const std::optional<std::size_t> index = IndexOfSyntax(name.substr(kSyntaxPrefix.size())))
				{
					scheme.Syntax[*index] = *color;
					continue;
				}
			}
			else if (name.starts_with(kEditorPrefix))
			{
				if (const std::optional<std::size_t> index = IndexOfEditor(name.substr(kEditorPrefix.size())))
				{
					scheme.Editor[*index] = *color;
					continue;
				}
			}
			else if (name.starts_with(kChromePrefix))
			{
				if (const std::optional<std::size_t> index = IndexOfChrome(name.substr(kChromePrefix.size())))
				{
					scheme.Chrome[*index] = *color;
					continue;
				}
			}
			else if (name.starts_with(kGitPrefix))
			{
				if (const std::optional<std::size_t> index = IndexOfGit(name.substr(kGitPrefix.size())))
				{
					scheme.Git[*index] = *color;
					continue;
				}
			}
			else if (name.starts_with(kSessionPrefix))
			{
				if (const std::optional<std::size_t> index = IndexOfSession(name.substr(kSessionPrefix.size())))
				{
					scheme.Session[*index] = *color;
					continue;
				}
			}

			APP_WARN("Colour scheme '{}' names an unknown colour '{}'", Core::ToUtf8(file), key);
		}

		return scheme;
	}

	Core::Result<void> SaveScheme(const std::filesystem::path& directory, std::string_view name,
								  const ColorScheme& scheme)
	{
		if (ParseBuiltInName(name).has_value())
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, std::format("'{}' is a built-in scheme", name)};
		}

		const bool escapesDirectory = name.find('/') != std::string_view::npos ||
									  name.find('\\') != std::string_view::npos ||
									  name.find("..") != std::string_view::npos;

		if (escapesDirectory)
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, std::format("'{}' is not a scheme name", name)};
		}

		std::error_code errorCode;
		std::filesystem::create_directories(directory, errorCode);

		Core::JsonFile json;
		json.Set("base", std::string(NameOf(scheme.Base)));

		for (std::size_t kindIndex = 0; kindIndex < scheme.Syntax.size(); ++kindIndex)
		{
			if (scheme.Syntax[kindIndex].has_value())
			{
				json.Set(std::string(kSyntaxPrefix) + std::string(NameOf(static_cast<TokenKind>(kindIndex))),
						 Core::ToHexColor(*scheme.Syntax[kindIndex]));
			}
		}

		for (std::size_t colorIndex = 0; colorIndex < scheme.Editor.size(); ++colorIndex)
		{
			if (scheme.Editor[colorIndex].has_value())
			{
				json.Set(std::string(kEditorPrefix) + std::string(NameOf(static_cast<EditorColor>(colorIndex))),
						 Core::ToHexColor(*scheme.Editor[colorIndex]));
			}
		}

		for (std::size_t colorIndex = 0; colorIndex < scheme.Chrome.size(); ++colorIndex)
		{
			if (scheme.Chrome[colorIndex].has_value())
			{
				json.Set(std::string(kChromePrefix) + std::string(NameOf(static_cast<ChromeColor>(colorIndex))),
						 Core::ToHexColor(*scheme.Chrome[colorIndex]));
			}
		}

		for (std::size_t colorIndex = 0; colorIndex < scheme.Git.size(); ++colorIndex)
		{
			if (scheme.Git[colorIndex].has_value())
			{
				json.Set(std::string(kGitPrefix) + std::string(NameOf(static_cast<GitColor>(colorIndex))),
						 Core::ToHexColor(*scheme.Git[colorIndex]));
			}
		}

		for (std::size_t colorIndex = 0; colorIndex < scheme.Session.size(); ++colorIndex)
		{
			if (scheme.Session[colorIndex].has_value())
			{
				json.Set(std::string(kSessionPrefix) + std::string(NameOf(static_cast<SessionColor>(colorIndex))),
						 Core::ToHexColor(*scheme.Session[colorIndex]));
			}
		}

		for (std::size_t metricIndex = 0; metricIndex < scheme.Layout.size(); ++metricIndex)
		{
			if (scheme.Layout[metricIndex].has_value())
			{
				json.Set(std::string(kLayoutPrefix) + std::string(NameOf(static_cast<LayoutMetric>(metricIndex))),
						 std::format("{}", *scheme.Layout[metricIndex]));
			}
		}

		const std::filesystem::path file = SchemeFile(directory, name);

		if (Core::JsonFile::Load(file).IsMalformed() && !BackUpBeforeWritingOver(file, "does not parse"))
		{
			return Core::Error{Core::ErrorCode::IoFailure,
							   std::format("'{}' does not parse and could not be kept as .bak before writing over it",
										   Core::ToUtf8(file))};
		}

		return json.Save(file);
	}

	std::vector<std::string> ListSchemes(const std::filesystem::path& directory)
	{
		std::vector<std::string> names;

		std::error_code errorCode;

		for (const std::filesystem::directory_entry& entry :
			 std::filesystem::directory_iterator(directory, errorCode))
		{
			if (!entry.is_regular_file(errorCode))
			{
				continue;
			}

			if (entry.path().extension() != ".json")
			{
				continue;
			}

			const std::string stem = Core::ToUtf8(entry.path().stem());

			if (ParseBuiltInName(stem).has_value())
			{
				continue;
			}

			names.push_back(stem);
		}

		return names;
	}

	Core::Result<void> DeleteScheme(const std::filesystem::path& directory, std::string_view name)
	{
		const std::filesystem::path file = SchemeFile(directory, name);

		std::error_code errorCode;

		if (!std::filesystem::remove(file, errorCode))
		{
			return Core::Error{Core::ErrorCode::IoFailure,
							   std::format("cannot delete '{}': {}", Core::ToUtf8(file),
										   errorCode ? errorCode.message() : std::string{"it is not there"})};
		}

		return Core::Result<void>::Ok();
	}

	std::optional<std::string> SchemeNameProblem(std::string_view name, const std::vector<std::string>& existing)
	{
		if (name.empty())
		{
			return "Enter a name.";
		}

		const std::size_t characters = Core::CharacterCount(name);

		if (characters > kLongestSchemeName)
		{
			return std::format("A name can have at most {} characters.", kLongestSchemeName);
		}

		if (HoldsCharacterForbiddenInFileName(name))
		{
			return "A name cannot contain control characters or any of < > : \" / \\ | ? *";
		}

		if (name.front() == ' ' || name.back() == ' ' || name.back() == '.' ||
			name.find("..") != std::string_view::npos)
		{
			return "A name cannot start or end with a space, end with a dot, or contain \"..\".";
		}

		if (Core::NamesWindowsDevice(name))
		{
			return "That name is reserved by Windows.";
		}

		for (const std::string& other : existing)
		{
			if (Core::EqualsIgnoringCase(other, name))
			{
				return "A scheme with that name already exists.";
			}
		}

		return std::nullopt;
	}

	Core::Result<void> RenameScheme(const std::filesystem::path& directory, std::string_view from,
									std::string_view to)
	{
		if (ParseBuiltInName(from).has_value() || ParseBuiltInName(to).has_value())
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, "a built-in scheme cannot be renamed or replaced"};
		}

		const std::vector<std::string> noExistingNames;

		if (const std::optional<std::string> problem = SchemeNameProblem(to, noExistingNames))
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, std::format("'{}': {}", to, *problem)};
		}

		const std::filesystem::path source = SchemeFile(directory, from);
		const std::filesystem::path target = SchemeFile(directory, to);

		std::error_code errorCode;

		if (!std::filesystem::exists(source, errorCode))
		{
			return Core::Error{Core::ErrorCode::NotFound, std::format("'{}' is not there", Core::ToUtf8(source))};
		}

		const bool differsInCaseOnly = from != to && Core::EqualsIgnoringCase(from, to);

		if (!differsInCaseOnly && std::filesystem::exists(target, errorCode))
		{
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   std::format("'{}' already exists", Core::ToUtf8(target))};
		}

		std::filesystem::rename(source, target, errorCode);

		if (errorCode)
		{
			return Core::Error{Core::ErrorCode::IoFailure,
							   std::format("cannot rename '{}': {}", Core::ToUtf8(source), errorCode.message())};
		}

		return Core::Result<void>::Ok();
	}
}
