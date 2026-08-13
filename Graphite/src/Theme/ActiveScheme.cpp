#include "Theme/ActiveScheme.h"

#include "Settings/AppSettings.h"
#include "Theme/SchemeStore.h"

#include <optional>
#include <string>

namespace Graphite {

	ResolvedScheme ResolveNamed(std::string_view name, const std::filesystem::path& directory)
	{
		if (name.empty())
		{
			return DarkScheme();
		}

		if (const std::optional<BuiltInScheme> builtIn = ParseBuiltInName(name))
		{
			return SchemeFor(*builtIn);
		}

		const ColorScheme scheme = LoadScheme(directory / (std::string(name) + ".json"));

		return Resolve(scheme, SchemeFor(scheme.Base));
	}

	void ActiveScheme::Load()
	{
		const std::string name = AppSettings::LoadColorScheme();
		m_Resolved = ResolveNamed(name, AppSettings::ThemesDirectory());
	}
}
