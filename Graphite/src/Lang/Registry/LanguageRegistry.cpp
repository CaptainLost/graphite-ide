#include "Lang/Registry/LanguageRegistry.h"

#include "Core/Base/Ascii.h"

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace Graphite {

	namespace {

		constexpr std::array<std::pair<std::string_view, LanguageId>, 9> kByExtension{{
			{".cpp", LanguageId::Cpp},
			{".cc", LanguageId::Cpp},
			{".cxx", LanguageId::Cpp},
			{".h", LanguageId::Cpp},
			{".hpp", LanguageId::Cpp},
			{".hxx", LanguageId::Cpp},
			{".ipp", LanguageId::Cpp},
			{".cs", LanguageId::CSharp},
			{".ts", LanguageId::TypeScript},
		}};
	}

	LanguageId LanguageOfPath(const std::filesystem::path& path)
	{
		// stem().empty() rejects the dotfiles: for ".gitignore" the filesystem library reports
		// the whole name as the extension, which would otherwise make every dotfile a language.
		if (!path.has_extension() || path.stem().empty())
		{
			return LanguageId::PlainText;
		}

		const std::string extension = Core::Lowered(path.extension().string());

		for (const auto& [suffix, language] : kByExtension)
		{
			if (extension == suffix)
			{
				return language;
			}
		}

		return LanguageId::PlainText;
	}
}
