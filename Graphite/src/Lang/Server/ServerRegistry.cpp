#include "Lang/Server/ServerRegistry.h"

#include <array>

namespace Graphite {

	namespace {

		// Order here does not affect RootFor's answer — it does not prefer one marker over
		// another, see the comment on RootFor in the header. ".git" trails the list on purpose: it names a
		// checkout, not a build, so a nearer compile_commands.json or .clangd should still win
		// when both exist above the same file — which RootFor's proximity-first walk already
		// guarantees without this ordering doing any work. It is kept last anyway because it
		// documents the reasoning for a reader who has not seen RootFor yet.
		constexpr std::array<std::string_view, 4> kCppRootMarkers{
			"compile_commands.json", "compile_flags.txt", ".clangd", ".git"};

		constexpr ServerConfig kCpp{"clangd", "cpp", kCppRootMarkers};

		constexpr std::array<std::string_view, 3> kCompileDatabaseNames{
			"compile_commands.json", "compile_flags.txt", ".clangd"};
	}

	const ServerConfig* ServerConfigFor(LanguageId language)
	{
		switch (language)
		{
			case LanguageId::Cpp:
				return &kCpp;
			case LanguageId::PlainText:
			case LanguageId::CSharp:
			case LanguageId::TypeScript:
				return nullptr;
		}

		return nullptr;
	}

	std::filesystem::path RootFor(const ServerConfig& config, const std::filesystem::path& file,
								   const std::filesystem::path& fallbackRoot)
	{
		std::filesystem::path directory = file.parent_path();

		for (;;)
		{
			for (const std::string_view marker : config.RootMarkers)
			{
				if (std::filesystem::exists(directory / marker))
				{
					return directory;
				}
			}

			const std::filesystem::path parent = directory.parent_path();
			if (parent == directory)
			{
				// Reached the filesystem root without finding a marker.
				break;
			}

			directory = parent;
		}

		return fallbackRoot;
	}

	bool LacksCompileDatabase(const std::filesystem::path& root)
	{
		for (const std::string_view name : kCompileDatabaseNames)
		{
			if (std::filesystem::exists(root / name))
			{
				return false;
			}
		}

		return true;
	}
}
