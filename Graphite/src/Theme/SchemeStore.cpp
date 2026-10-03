#include "Theme/SchemeStore.h"

#include "Core/Base/Color.h"
#include "Core/Base/JsonFile.h"
#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Theme/BuiltInSchemes.h"

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

		std::optional<std::size_t> IndexOfSyntax(std::string_view name)
		{
			for (std::size_t kind = 0; kind < static_cast<std::size_t>(TokenKind::Count); ++kind)
			{
				if (NameOf(static_cast<TokenKind>(kind)) == name)
				{
					return kind;
				}
			}

			return std::nullopt;
		}

		std::optional<std::size_t> IndexOfEditor(std::string_view name)
		{
			for (std::size_t color = 0; color < static_cast<std::size_t>(EditorColor::Count);
				 ++color)
			{
				if (NameOf(static_cast<EditorColor>(color)) == name)
				{
					return color;
				}
			}

			return std::nullopt;
		}

		std::optional<std::size_t> IndexOfChrome(std::string_view name)
		{
			for (std::size_t color = 0; color < static_cast<std::size_t>(ChromeColor::Count);
				 ++color)
			{
				if (NameOf(static_cast<ChromeColor>(color)) == name)
				{
					return color;
				}
			}

			return std::nullopt;
		}

		std::optional<std::size_t> IndexOfGit(std::string_view name)
		{
			for (std::size_t color = 0; color < static_cast<std::size_t>(GitColor::Count); ++color)
			{
				if (NameOf(static_cast<GitColor>(color)) == name)
				{
					return color;
				}
			}

			return std::nullopt;
		}

		std::optional<std::size_t> IndexOfSession(std::string_view name)
		{
			for (std::size_t color = 0; color < static_cast<std::size_t>(SessionColor::Count);
				 ++color)
			{
				if (NameOf(static_cast<SessionColor>(color)) == name)
				{
					return color;
				}
			}

			return std::nullopt;
		}
	}

	ColorScheme LoadScheme(const std::filesystem::path& file)
	{
		ColorScheme scheme;

		std::error_code ec;

		// Absent is not an error: a scheme on a drive that is not plugged in should come back
		// when the drive does, and the caller falls back to a built-in meanwhile.
		if (!std::filesystem::exists(file, ec))
		{
			APP_WARN("Colour scheme '{}' is not there", Core::ToUtf8(file));
			return scheme;
		}

		const Core::JsonFile json = Core::JsonFile::Load(file);

		if (const std::optional<BuiltInScheme> base = ParseBuiltInName(json.Get("base")))
		{
			scheme.Base = *base;
		}
		else
		{
			// A base nobody has heard of, or none at all. Dark is the fallback and the scheme's
			// own overrides still apply on top of it.
			APP_WARN("Colour scheme '{}' names an unknown base '{}'", Core::ToUtf8(file),
					 json.Get("base"));
		}

		for (const std::string& key : json.CollectKeys())
		{
			if (key == "base")
			{
				continue;
			}

			const std::optional<Core::Color> color = Core::ParseHexColor(json.Get(key));

			if (!color)
			{
				// The key is skipped, not the file. Its colour then comes from the base, which
				// is what an unset colour does anyway.
				APP_WARN("Colour scheme '{}' has an unreadable colour for '{}'", Core::ToUtf8(file),
						 key);
				continue;
			}

			const std::string_view name = key;

			if (name.starts_with(kSyntaxPrefix))
			{
				if (const auto index = IndexOfSyntax(name.substr(kSyntaxPrefix.size())))
				{
					scheme.Syntax[*index] = *color;
					continue;
				}
			}
			else if (name.starts_with(kEditorPrefix))
			{
				if (const auto index = IndexOfEditor(name.substr(kEditorPrefix.size())))
				{
					scheme.Editor[*index] = *color;
					continue;
				}
			}
			else if (name.starts_with(kChromePrefix))
			{
				if (const auto index = IndexOfChrome(name.substr(kChromePrefix.size())))
				{
					scheme.Chrome[*index] = *color;
					continue;
				}
			}
			else if (name.starts_with(kGitPrefix))
			{
				if (const auto index = IndexOfGit(name.substr(kGitPrefix.size())))
				{
					scheme.Git[*index] = *color;
					continue;
				}
			}
			else if (name.starts_with(kSessionPrefix))
			{
				if (const auto index = IndexOfSession(name.substr(kSessionPrefix.size())))
				{
					scheme.Session[*index] = *color;
					continue;
				}
			}

			// A scheme written against a newer version, naming a colour this one does not have.
			APP_WARN("Colour scheme '{}' names an unknown colour '{}'", Core::ToUtf8(file), key);
		}

		return scheme;
	}

	Core::Result<void> SaveScheme(const std::filesystem::path& directory, std::string_view name,
								  const ColorScheme& scheme)
	{
		// The built-ins are not files, so writing over one would only ever succeed at making
		// dark.json shadow the real thing.
		if (ParseBuiltInName(name).has_value())
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, std::format("'{}' is a built-in scheme", name)};
		}

		// The name is joined straight onto the themes directory below. Harmless while it only
		// ever comes from a user editing their own JSON, but a name with a path separator or a
		// ".." segment would resolve outside that directory the moment something else supplies
		// it — a text field, say.
		if (name.find('/') != std::string_view::npos || name.find('\\') != std::string_view::npos ||
			name.find("..") != std::string_view::npos)
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, std::format("'{}' is not a scheme name", name)};
		}

		std::error_code ec;
		std::filesystem::create_directories(directory, ec);

		Core::JsonFile json;
		json.Set("base", std::string(NameOf(scheme.Base)));

		for (std::size_t kind = 0; kind < scheme.Syntax.size(); ++kind)
		{
			if (scheme.Syntax[kind].has_value())
			{
				json.Set(std::string(kSyntaxPrefix) + std::string(NameOf(static_cast<TokenKind>(kind))),
						 Core::ToHexColor(*scheme.Syntax[kind]));
			}
		}

		for (std::size_t color = 0; color < scheme.Editor.size(); ++color)
		{
			if (scheme.Editor[color].has_value())
			{
				json.Set(std::string(kEditorPrefix) +
							 std::string(NameOf(static_cast<EditorColor>(color))),
						 Core::ToHexColor(*scheme.Editor[color]));
			}
		}

		for (std::size_t color = 0; color < scheme.Chrome.size(); ++color)
		{
			if (scheme.Chrome[color].has_value())
			{
				json.Set(std::string(kChromePrefix) +
							 std::string(NameOf(static_cast<ChromeColor>(color))),
						 Core::ToHexColor(*scheme.Chrome[color]));
			}
		}

		for (std::size_t color = 0; color < scheme.Git.size(); ++color)
		{
			if (scheme.Git[color].has_value())
			{
				json.Set(std::string(kGitPrefix) + std::string(NameOf(static_cast<GitColor>(color))),
						 Core::ToHexColor(*scheme.Git[color]));
			}
		}

		for (std::size_t color = 0; color < scheme.Session.size(); ++color)
		{
			if (scheme.Session[color].has_value())
			{
				json.Set(std::string(kSessionPrefix) +
							 std::string(NameOf(static_cast<SessionColor>(color))),
						 Core::ToHexColor(*scheme.Session[color]));
			}
		}

		return json.Save(directory / (std::string(name) + ".json"));
	}

	std::vector<std::string> ListSchemes(const std::filesystem::path& directory)
	{
		std::vector<std::string> names;

		std::error_code ec;

		for (const std::filesystem::directory_entry& entry :
			 std::filesystem::directory_iterator(directory, ec))
		{
			if (!entry.is_regular_file(ec))
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
		const std::filesystem::path file = directory / (std::string(name) + ".json");

		std::error_code ec;

		if (!std::filesystem::remove(file, ec))
		{
			return Core::Error{Core::ErrorCode::IoFailure,
							   std::format("cannot delete '{}': {}", Core::ToUtf8(file),
										   ec ? ec.message() : std::string{"it is not there"})};
		}

		return Core::Result<void>::Ok();
	}

	namespace {

		std::string Lowered(std::string_view text)
		{
			std::string lowered(text);

			for (char& character : lowered)
			{
				if (character >= 'A' && character <= 'Z')
				{
					character = static_cast<char>(character - 'A' + 'a');
				}
			}

			return lowered;
		}

		bool IsDeviceName(std::string_view lowered)
		{
			if (lowered == "con" || lowered == "prn" || lowered == "aux" || lowered == "nul")
			{
				return true;
			}

			return lowered.size() == 4 && (lowered.starts_with("com") || lowered.starts_with("lpt")) &&
				   lowered[3] >= '1' && lowered[3] <= '9';
		}
	}

	std::optional<std::string> SchemeNameProblem(std::string_view name, const std::vector<std::string>& existing)
	{
		constexpr std::size_t kLongestName = 64;

		if (name.empty())
		{
			return "Enter a name.";
		}

		if (name.size() > kLongestName)
		{
			return std::format("A name can have at most {} characters.", kLongestName);
		}

		for (const char character : name)
		{
			if (static_cast<unsigned char>(character) < 0x20 ||
				std::string_view{"<>:\"/\\|?*"}.find(character) != std::string_view::npos)
			{
				return "A name cannot contain control characters or any of < > : \" / \\ | ? *";
			}
		}

		if (name.front() == ' ' || name.back() == ' ' || name.back() == '.' ||
			name.find("..") != std::string_view::npos)
		{
			return "A name cannot start or end with a space, end with a dot, or contain \"..\".";
		}

		if (IsDeviceName(Lowered(name)))
		{
			return "That name is reserved by Windows.";
		}

		const std::string lowered = Lowered(name);

		for (const std::string& other : existing)
		{
			if (Lowered(other) == lowered)
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

		// Nothing is listed as existing: the file system is asked below, which is the authority on
		// what is taken. What is left is the part of the check that is about the name itself.
		if (const std::optional<std::string> problem = SchemeNameProblem(to, {}))
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, std::format("'{}': {}", to, *problem)};
		}

		const std::filesystem::path source = directory / (std::string(from) + ".json");
		const std::filesystem::path target = directory / (std::string(to) + ".json");

		std::error_code ec;

		if (!std::filesystem::exists(source, ec))
		{
			return Core::Error{Core::ErrorCode::NotFound, std::format("'{}' is not there", Core::ToUtf8(source))};
		}

		if (std::filesystem::exists(target, ec))
		{
			return Core::Error{Core::ErrorCode::InvalidArgument,
							   std::format("'{}' already exists", Core::ToUtf8(target))};
		}

		std::filesystem::rename(source, target, ec);

		if (ec)
		{
			return Core::Error{Core::ErrorCode::IoFailure,
							   std::format("cannot rename '{}': {}", Core::ToUtf8(source), ec.message())};
		}

		return Core::Result<void>::Ok();
	}
}
