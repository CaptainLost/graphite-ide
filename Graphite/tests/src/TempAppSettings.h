#pragma once

#include "Settings/AppSettings.h"

#include "TempDirectory.h"

#include <filesystem>

// An AppSettings over graphite.json in a fresh temporary directory, so a case starts from the
// defaults and never touches the settings of whoever runs the suite. Nothing is written until a
// case calls Flush; Reload stands in for the next launch.
class TempAppSettings
{
public:
	Graphite::AppSettings& Get() { return m_Settings; }

	const std::filesystem::path& GetFile() const { return m_File; }

	// Writes what is held and reads it back into a fresh instance, as a restart would.
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
