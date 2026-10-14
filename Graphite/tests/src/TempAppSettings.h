#pragma once

#include "Settings/AppSettings.h"

#include "TempDirectory.h"

#include <filesystem>

class TempAppSettings
{
public:
	Graphite::AppSettings& Get() { return m_Settings; }

	const std::filesystem::path& GetFile() const { return m_File; }

	Graphite::AppSettings& Reload()
	{
		m_Settings.Flush();
		m_Settings = Graphite::AppSettings{m_File};
		return m_Settings;
	}

private:
	TempDirectory m_Temp;
	std::filesystem::path m_File = m_Temp.GetPath() / "graphite.json";
	Graphite::AppSettings m_Settings{m_File};
};
