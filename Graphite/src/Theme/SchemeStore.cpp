#include "Theme/SchemeStore.h"

#include "Core/Base/Color.h"
#include "Core/Base/JsonFile.h"
#include "Core/Base/Log.h"
#include "Theme/BuiltInSchemes.h"

#include <optional>
#include <string>
#include <system_error>

namespace Graphite {

	namespace {

		constexpr std::string_view kSyntaxPrefix = "syntax.";
		constexpr std::string_view kEditorPrefix = "editor.";

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
	}

	ColorScheme LoadScheme(const std::filesystem::path& file)
	{
		ColorScheme scheme;

		std::error_code ec;

		// Absent is not an error: a scheme on a drive that is not plugged in should come back
		// when the drive does, and the caller falls back to a built-in meanwhile.
		if (!std::filesystem::exists(file, ec))
		{
			APP_WARN("Colour scheme '{}' is not there", file.string());
			return scheme;
		}

		const Core::JsonFile json = Core::JsonFile::Load(file);

		scheme.Base = json.Get("base");

		for (const std::string& key : json.GetKeys())
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
				APP_WARN("Colour scheme '{}' has an unreadable colour for '{}'", file.string(),
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

			// A scheme written against a newer version, naming a colour this one does not have.
			APP_WARN("Colour scheme '{}' names an unknown colour '{}'", file.string(), key);
		}

		return scheme;
	}

	bool SaveScheme(const std::filesystem::path& directory, std::string_view name,
					 const ColorScheme& scheme)
	{
		// The built-ins are not files, so writing over one would only ever succeed at making
		// dark.json shadow the real thing.
		if (IsBuiltInName(name))
		{
			return false;
		}

		std::error_code ec;
		std::filesystem::create_directories(directory, ec);

		Core::JsonFile json;
		json.Set("base", scheme.Base);

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

			const std::string stem = entry.path().stem().string();

			if (IsBuiltInName(stem))
			{
				continue;
			}

			names.push_back(stem);
		}

		return names;
	}

	bool DeleteScheme(const std::filesystem::path& directory, std::string_view name)
	{
		std::error_code ec;
		return std::filesystem::remove(directory / (std::string(name) + ".json"), ec);
	}
}
