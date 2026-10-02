#include "Core/Base/Base.h"

#include "corepch.h"

#include "Core/Base/JsonFile.h"

#include "Core/Fs/AtomicWrite.h"

#include <nlohmann/json.hpp>

#include <fstream>

#include <sstream>

#include <utility>

namespace Core {

	struct JsonFile::Document
	{
		nlohmann::json root = nlohmann::json::object();
	};

	JsonFile::JsonFile() : m_Document(CreateScope<Document>())
	{
	}

	JsonFile::~JsonFile() = default;

	JsonFile::JsonFile(JsonFile&&) noexcept = default;
	JsonFile& JsonFile::operator=(JsonFile&&) noexcept = default;

	JsonFile JsonFile::Load(const std::filesystem::path& path)
	{
		JsonFile file;

		std::ifstream stream{path};
		if (!stream.is_open())
			return file;

		std::ostringstream buffer;
		buffer << stream.rdbuf();

		// nlohmann/json throws by default; `allow_exceptions = false` returns a discarded
		// value instead, so a hand-edited settings file loads as empty like a missing one.
		const std::string text = buffer.str();
		nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);

		if (parsed.is_discarded() || !parsed.is_object())
		{
			file.m_Malformed = text.find_first_not_of(" \t\r\n") != std::string::npos;
			return file;
		}

		file.m_Document->root = std::move(parsed);
		return file;
	}

	std::string JsonFile::Get(std::string_view key) const
	{
		const nlohmann::json& root = m_Document->root;

		const auto entry = root.find(std::string{key});
		if (entry == root.end() || !entry->is_string())
			return {};

		return entry->get<std::string>();
	}

	void JsonFile::Set(std::string_view key, std::string value)
	{
		m_Document->root[std::string{key}] = std::move(value);
	}

	void JsonFile::Remove(std::string_view key)
	{
		m_Document->root.erase(std::string{key});
	}

	std::vector<std::string> JsonFile::GetKeys() const
	{
		const nlohmann::json& root = m_Document->root;

		std::vector<std::string> keys;
		keys.reserve(root.size());

		for (const auto& entry : root.items())
		{
			keys.push_back(entry.key());
		}

		return keys;
	}

	std::size_t JsonFile::GetCount() const
	{
		return m_Document->root.size();
	}

	bool JsonFile::Save(const std::filesystem::path& path) const
	{
		// dump() rejects invalid UTF-8 unless told otherwise, and `replace` substitutes U+FFFD
		// instead: a visibly mangled setting is a better outcome than an exception crossing
		// out of Core. Serialised before WriteFileAtomically opens anything, so a throw here
		// could not leave a .tmp behind either.
		const std::string text =
			m_Document->root.dump(4, ' ', false, nlohmann::json::error_handler_t::replace);

		return static_cast<bool>(WriteFileAtomically(path, text));
	}
}