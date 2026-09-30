#include "Workspace/WorkspaceTrust.h"

#include "Core/Base/Log.h"
#include "Core/Fs/AtomicWrite.h"
#include "Core/Fs/FileSystem.h"

#include <nlohmann/json.hpp>

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {
		constexpr const char* kVersionKey = "version";
		constexpr const char* kFoldersKey = "folders";

		// The form a folder is kept and shown in: absolute, normalised, backslashes, and no
		// trailing separator except a drive root's own. Empty for a path that cannot name a
		// trusted folder.
		std::filesystem::path Tidy(const std::filesystem::path& folder)
		{
			if (folder.empty() || !folder.is_absolute())
			{
				return {};
			}

			std::filesystem::path tidy = folder.lexically_normal();
			tidy.make_preferred();

			// lexically_normal leaves "D:\Repos\" with its separator, which would make it a
			// different string from "D:\Repos".
			if (!tidy.has_filename() && tidy != tidy.root_path())
			{
				tidy = tidy.parent_path();
			}

			return tidy;
		}

		// What two tidied paths are compared by. CharUpperBuffW rather than towupper: it folds
		// the letters Windows itself treats as the same in a file name, not only ASCII.
		std::wstring Key(const std::filesystem::path& tidy)
		{
			std::wstring key = tidy.native();

			if (!key.empty())
			{
				CharUpperBuffW(key.data(), static_cast<DWORD>(key.size()));
			}

			return key;
		}

		bool Covers(const std::wstring& entry, const std::wstring& folder)
		{
			if (entry.empty() || !folder.starts_with(entry))
			{
				return false;
			}

			// "D:\" already ends in a separator. "D:\Repos" has to be followed by one, or
			// "D:\Repos2" would count as inside it.
			return folder.size() == entry.size() || entry.back() == L'\\' ||
				   folder[entry.size()] == L'\\';
		}
	}

	WorkspaceTrust::WorkspaceTrust(std::filesystem::path file) : m_File(std::move(file))
	{
		if (m_File.empty())
		{
			return;
		}

		std::ifstream in(m_File, std::ios::binary);

		if (!in)
		{
			// No file is an empty list. A file that is there but cannot be opened - held without
			// sharing by something else for a moment - is a list that could not be read, and
			// writing the next change over it would wipe every folder it names. When existence
			// itself cannot be told, the same caution applies.
			std::error_code failed;

			if (std::filesystem::exists(m_File, failed) || failed)
			{
				m_Writable = false;
				APP_WARN("Cannot open '{}': no folder is trusted this session, and changes to trust "
						 "will not be saved",
						 Core::ToUtf8(m_File));
			}

			return;
		}

		const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
		const nlohmann::json json = nlohmann::json::parse(text, nullptr, false);

		if (json.is_discarded() || !json.is_object())
		{
			APP_WARN("Ignoring unreadable '{}': no folder is trusted until one is trusted again",
					 Core::ToUtf8(m_File));
			return;
		}

		if (const auto version = json.find(kVersionKey);
			version != json.end() && version->is_number_integer() &&
			version->get<std::int64_t>() > kFileVersion)
		{
			m_Writable = false;
			APP_WARN("'{}' was written by a newer version of Graphite; changes to trust will not be saved",
					 Core::ToUtf8(m_File));
		}

		const auto folders = json.find(kFoldersKey);

		if (folders == json.end() || !folders->is_array())
		{
			return;
		}

		for (const nlohmann::json& folder : *folders)
		{
			if (!folder.is_string())
			{
				continue;
			}

			// The parser has already rejected invalid UTF-8, so FromUtf8 cannot meet any.
			if (std::filesystem::path tidy = Tidy(Core::FromUtf8(folder.get<std::string>())); !tidy.empty())
			{
				m_Entries.push_back(std::move(tidy));
			}
		}
	}

	bool WorkspaceTrust::IsTrusted(const std::filesystem::path& folder) const
	{
		return TrustedThrough(folder).has_value();
	}

	std::optional<std::filesystem::path> WorkspaceTrust::TrustedThrough(const std::filesystem::path& folder) const
	{
		const std::wstring key = Key(Tidy(folder));

		for (const std::filesystem::path& entry : m_Entries)
		{
			if (Covers(Key(entry), key))
			{
				return entry;
			}
		}

		return std::nullopt;
	}

	bool WorkspaceTrust::IsEntry(const std::filesystem::path& folder) const
	{
		const std::wstring key = Key(Tidy(folder));

		return !key.empty() && std::any_of(m_Entries.begin(), m_Entries.end(),
										   [&key](const std::filesystem::path& entry) { return Key(entry) == key; });
	}

	void WorkspaceTrust::Trust(const std::filesystem::path& folder)
	{
		std::filesystem::path tidy = Tidy(folder);

		if (tidy.empty() || IsEntry(tidy))
		{
			return;
		}

		m_Entries.push_back(std::move(tidy));
		++m_Revision;
		Save();
	}

	void WorkspaceTrust::Revoke(const std::filesystem::path& entry)
	{
		const std::wstring key = Key(Tidy(entry));

		const auto removed = std::erase_if(m_Entries, [&key](const std::filesystem::path& listed) { return Key(listed) == key; });

		if (removed == 0)
		{
			return;
		}

		++m_Revision;
		Save();
	}

	void WorkspaceTrust::Save() const
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

		nlohmann::json folders = nlohmann::json::array();

		for (const std::filesystem::path& entry : m_Entries)
		{
			folders.push_back(Core::ToUtf8(entry));
		}

		nlohmann::json json = nlohmann::json::object();
		json[kVersionKey] = kFileVersion;
		json[kFoldersKey] = std::move(folders);

		if (!Core::WriteFileAtomically(m_File, json.dump(4, ' ', false, nlohmann::json::error_handler_t::replace)))
		{
			APP_WARN("Cannot write '{}'", Core::ToUtf8(m_File));
		}
	}

	std::optional<std::filesystem::path> ParentWorthTrusting(const std::filesystem::path& folder)
	{
		const std::filesystem::path tidy = Tidy(folder);

		if (tidy.empty() || tidy == tidy.root_path())
		{
			return std::nullopt;
		}

		std::filesystem::path parent = tidy.parent_path();

		if (parent == parent.root_path())
		{
			return std::nullopt;
		}

		return parent;
	}
}
