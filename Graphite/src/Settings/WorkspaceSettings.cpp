#include "Settings/WorkspaceSettings.h"

#include "Settings/SettingsBackup.h"

#include "Persistence/StateCodec.h"

#include "Core/Base/JsonFile.h"
#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Fs/FileOperations.h"

#include <filesystem>
#include <utility>

namespace Graphite {

	namespace {
		constexpr const char* kDirectory = ".graphite";
		constexpr const char* kFile = "workspace.json";

		bool EnsureDirectoryExists(const std::filesystem::path& directory)
		{
			// MakeDirectory reports an error when the directory is already there, which is
			// every write after the first.
			std::error_code code;
			if (std::filesystem::exists(directory, code))
			{
				return true;
			}

			if (const auto made = Core::MakeDirectory(directory); !made)
			{
				APP_WARN("Cannot create '{}': {}", Core::ToUtf8(directory), made.GetError().Message);
				return false;
			}

			return true;
		}
	}

	WorkspaceSettings::WorkspaceSettings(std::filesystem::path root) : m_Root(std::move(root))
	{
	}

	std::filesystem::path WorkspaceSettings::GetFilePath() const
	{
		return m_Root / kDirectory / kFile;
	}

	std::string WorkspaceSettings::Read(std::string_view key) const
	{
		if (m_Root.empty())
		{
			return {};
		}

		return Core::JsonFile::Load(GetFilePath()).Get(key);
	}

	void WorkspaceSettings::Write(std::string_view key, std::string value)
	{
		Modify([&](Core::JsonFile& file) { file.Set(key, std::move(value)); });
	}

	void WorkspaceSettings::Modify(const std::function<void(Core::JsonFile&)>& edit)
	{
		if (m_Root.empty() || !EnsureDirectoryExists(m_Root / kDirectory))
		{
			return;
		}

		Core::JsonFile file = Core::JsonFile::Load(GetFilePath());

		if (file.IsMalformed() && !BackUpMalformedSettings(GetFilePath()))
		{
			APP_WARN("Not writing '{}': it does not parse and could not be copied aside", Core::ToUtf8(GetFilePath()));
			return;
		}

		edit(file);

		if (const Core::Result<void> saved = file.Save(GetFilePath()); !saved)
		{
			APP_WARN("Cannot write '{}': {}", Core::ToUtf8(GetFilePath()), saved.GetError().Message);
		}
	}

	WorkspaceState WorkspaceSettings::LoadState() const
	{
		if (m_Root.empty())
		{
			return {};
		}

		return ReadWorkspaceState(Core::JsonFile::Load(GetFilePath()), m_Root);
	}

	void WorkspaceSettings::SaveState(const WorkspaceState& state)
	{
		Modify([&](Core::JsonFile& file) { WriteWorkspaceState(state, m_Root, file); });
	}
}