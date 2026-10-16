#include "Workspace/WorkspaceTrust.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"
#include "Persistence/FileBackup.h"
#include "Persistence/StoredVersion.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {
		constexpr const char* kTrustFileVersionKey = "version";
		constexpr const char* kTrustFileFoldersKey = "folders";
		constexpr int kJsonIndentWidth = 4;
		constexpr char kJsonIndentCharacter = ' ';
		constexpr bool kJsonEscapeNonAscii = false;

		std::filesystem::path NormalisedAbsoluteFolder(const std::filesystem::path& folder)
		{
			if (folder.empty() || !folder.is_absolute())
			{
				return std::filesystem::path();
			}

			std::filesystem::path normalised = folder.lexically_normal();
			normalised.make_preferred();

			// WARNING: lexically_normal leaves "D:\Repos\" with its trailing separator, which
			// would make it a different string from "D:\Repos".
			const bool endsInSeparator = !normalised.has_filename();
			const bool isDriveRoot = normalised == normalised.root_path();

			if (endsInSeparator && !isDriveRoot)
			{
				normalised = normalised.parent_path();
			}

			return normalised;
		}

		bool EntryHasKey(const std::filesystem::path& entry, const Core::PathKeyText& key)
		{
			return Core::PathKey(entry) == key;
		}
	}

	WorkspaceTrust::WorkspaceTrust(std::filesystem::path file) : m_File(std::move(file))
	{
		if (m_File.empty())
		{
			return;
		}

		const Core::Result<std::string> fileBytes = Core::ReadFileBytes(m_File);

		if (!fileBytes)
		{
			const bool fileIsAbsent = fileBytes.GetError().Code == Core::ErrorCode::NotFound;

			// WARNING: Only NotFound means the list is empty. A file that exists but cannot be
			// read stays unwritten, or the next change would wipe every folder it names.
			if (!fileIsAbsent)
			{
				m_Writable = false;
				APP_WARN("Cannot open '{}': no folder is trusted this session, and changes to trust "
						 "will not be saved",
						 Core::ToUtf8(m_File));
			}

			return;
		}

		const nlohmann::json json = nlohmann::json::parse(fileBytes.GetValue(), nullptr, false);

		if (json.is_discarded() || !json.is_object())
		{
			APP_WARN("Ignoring unreadable '{}': no folder is trusted until one is trusted again",
					 Core::ToUtf8(m_File));
			m_KeepBeforeWriting = true;
			return;
		}

		const bool writtenByNewerVersion = StoredVersionOf(json, kTrustFileVersionKey, kFileVersion) == StoredVersion::Newer;

		if (writtenByNewerVersion)
		{
			m_Writable = false;
			APP_WARN("'{}' was written by a newer version of Graphite; changes to trust will not be saved",
					 Core::ToUtf8(m_File));
		}

		const nlohmann::json::const_iterator foldersEntry = json.find(kTrustFileFoldersKey);

		if (foldersEntry == json.end() || !foldersEntry->is_array())
		{
			return;
		}

		for (const nlohmann::json& folder : *foldersEntry)
		{
			if (!folder.is_string())
			{
				continue;
			}

			std::filesystem::path normalised = NormalisedAbsoluteFolder(Core::FromUtf8(folder.get<std::string>()));

			if (!normalised.empty())
			{
				m_Entries.push_back(std::move(normalised));
			}
		}
	}

	bool WorkspaceTrust::IsTrusted(const std::filesystem::path& folder) const
	{
		return TrustedThrough(folder).has_value();
	}

	std::optional<std::filesystem::path> WorkspaceTrust::TrustedThrough(const std::filesystem::path& folder) const
	{
		const std::filesystem::path normalised = NormalisedAbsoluteFolder(folder);

		if (normalised.empty())
		{
			return std::nullopt;
		}

		for (const std::filesystem::path& entry : m_Entries)
		{
			if (Core::IsSameOrWithin(normalised, entry))
			{
				return entry;
			}
		}

		return std::nullopt;
	}

	bool WorkspaceTrust::IsEntry(const std::filesystem::path& folder) const
	{
		const Core::PathKeyText key = Core::PathKey(NormalisedAbsoluteFolder(folder));

		if (key.empty())
		{
			return false;
		}

		for (const std::filesystem::path& entry : m_Entries)
		{
			if (EntryHasKey(entry, key))
			{
				return true;
			}
		}

		return false;
	}

	void WorkspaceTrust::Trust(const std::filesystem::path& folder)
	{
		std::filesystem::path normalised = NormalisedAbsoluteFolder(folder);

		if (normalised.empty() || IsEntry(normalised))
		{
			return;
		}

		m_Entries.push_back(std::move(normalised));
		++m_Revision;
		Save();
	}

	void WorkspaceTrust::Revoke(const std::filesystem::path& entry)
	{
		const Core::PathKeyText key = Core::PathKey(NormalisedAbsoluteFolder(entry));

		const std::size_t removedCount = std::erase_if(
			m_Entries, [&key](const std::filesystem::path& listed) { return EntryHasKey(listed, key); });

		if (removedCount == 0)
		{
			return;
		}

		++m_Revision;
		Save();
	}

	void WorkspaceTrust::Save()
	{
		if (m_File.empty())
		{
			return;
		}

		if (!m_Writable)
		{
			APP_WARN("Not writing '{}': it was written by a newer version of Graphite, or could "
					 "not be opened",
					 Core::ToUtf8(m_File));
			return;
		}

		if (m_KeepBeforeWriting && !BackUpBeforeWritingOver(m_File, "does not parse"))
		{
			APP_WARN("Not writing '{}': it does not parse and could not be kept as .bak first", Core::ToUtf8(m_File));
			return;
		}

		m_KeepBeforeWriting = false;

		nlohmann::json folders = nlohmann::json::array();

		for (const std::filesystem::path& entry : m_Entries)
		{
			folders.push_back(Core::ToUtf8(entry));
		}

		nlohmann::json json = nlohmann::json::object();
		json[kTrustFileVersionKey] = kFileVersion;
		json[kTrustFileFoldersKey] = std::move(folders);

		const std::string text = json.dump(kJsonIndentWidth, kJsonIndentCharacter, kJsonEscapeNonAscii,
										   nlohmann::json::error_handler_t::replace);

		if (!Core::WriteFileAtomically(m_File, text))
		{
			APP_WARN("Cannot write '{}'", Core::ToUtf8(m_File));
		}
	}

	std::optional<std::filesystem::path> ParentWorthTrusting(const std::filesystem::path& folder)
	{
		const std::filesystem::path normalised = NormalisedAbsoluteFolder(folder);

		if (normalised.empty() || normalised == normalised.root_path())
		{
			return std::nullopt;
		}

		std::filesystem::path parent = normalised.parent_path();

		if (parent == parent.root_path())
		{
			return std::nullopt;
		}

		return parent;
	}
}
