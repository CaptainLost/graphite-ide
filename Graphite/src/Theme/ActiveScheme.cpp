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
			return Get(*builtIn);
		}

		const ColorScheme scheme = LoadScheme(directory / (std::string(name) + ".json"));

		return Resolve(scheme, Get(scheme.Base));
	}

	void ActiveScheme::Load()
	{
		m_Name = AppSettings::LoadColorScheme();
		m_Resolved = ResolveNamed(m_Name, AppSettings::ThemesDirectory());
	}
}
