#include "Settings/WorkspaceSettings.h"

#include "Core/Base/JsonFile.h"
#include "Core/Base/Log.h"
#include "Core/Fs/FileOperations.h"

#include <filesystem>
#include <utility>

namespace Graphite {

	namespace {
		constexpr const char* kDirectory = ".graphite";
		constexpr const char* kFile = "workspace.json";
	}

	WorkspaceSettings::WorkspaceSettings(std::filesystem::path root) : m_Root(std::move(root))
	{
	}

	std::filesystem::path WorkspaceSettings::GetFilePath() const
	{
		return m_Root / kDirectory / kFile;
	}

	std::string WorkspaceSettings::Get(std::string_view key) const
	{
		if (m_Root.empty())
			return {};

		return Core::JsonFile::Load(GetFilePath()).Get(key);
	}

	void WorkspaceSettings::Set(std::string_view key, std::string value)
	{
		if (m_Root.empty())
			return;

		// Created on the first write rather than when the folder opens, so browsing a
		// folder read-only never leaves anything behind in it.
		const std::filesystem::path directory = m_Root / kDirectory;

		// MakeDirectory reports an error when the directory already exists, so the
		// ordinary case — every write after the first — has to be guarded here rather
		// than treated as a failure.
		std::error_code code;
		if (!std::filesystem::exists(directory, code))
		{
			if (const auto made = Core::MakeDirectory(directory); !made)
			{
				APP_WARN("Cannot create '{}': {}", directory.string(), made.GetError().Message);
				return;
			}
		}

		Core::JsonFile file = Core::JsonFile::Load(GetFilePath());
		file.Set(key, std::move(value));

		if (!file.Save(GetFilePath()))
			APP_WARN("Cannot write '{}'", GetFilePath().string());
	}
}