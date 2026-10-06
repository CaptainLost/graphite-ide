#include "corepch.h"

#include "Core/Base/JsonFile.h"

#include "Core/Base/Base.h"

#include "Core/FileSystem/AtomicWrite.h"

#include <nlohmann/json.hpp>

#include <fstream>

#include <sstream>

#include <utility>

namespace Core {

	namespace {
		constexpr bool kNotMalformed = false;
		constexpr std::string_view kJsonWhitespace = " \t\r\n";

		constexpr int kIndentWidth = 4;
		constexpr char kIndentCharacter = ' ';
		constexpr bool kEscapeNonAscii = false;
	}

	struct JsonFile::Document
	{
		nlohmann::json Root = nlohmann::json::object();

		static const nlohmann::json& RootOf(const Scope<Document>& document)
		{
			static const nlohmann::json emptyRootOfMovedFromFile = nlohmann::json::object();
			const bool isMovedFrom = document == nullptr;

			return isMovedFrom ? emptyRootOfMovedFromFile : document->Root;
		}

		static nlohmann::json& WritableRootOf(Scope<Document>& document)
		{
			const bool isMovedFrom = document == nullptr;

			if (isMovedFrom)
			{
				document = CreateScope<Document>();
			}

			return document->Root;
		}
	};

	JsonFile::JsonFile() : m_Document(CreateScope<Document>())
	{
	}

	JsonFile::~JsonFile() = default;

	JsonFile::JsonFile(JsonFile&& other) noexcept
		: m_Document(std::move(other.m_Document)), m_Malformed(std::exchange(other.m_Malformed, kNotMalformed))
	{
	}

	JsonFile& JsonFile::operator=(JsonFile&& other) noexcept
	{
		m_Document = std::move(other.m_Document);
		m_Malformed = std::exchange(other.m_Malformed, kNotMalformed);
		return *this;
	}

	JsonFile JsonFile::Load(const std::filesystem::path& path)
	{
		JsonFile file;

		std::ifstream stream{path};
		if (!stream.is_open())
		{
			return file;
		}

		std::ostringstream buffer;
		buffer << stream.rdbuf();

		// nlohmann/json throws on a parse error by default; with exceptions off it returns a
		// discarded value instead, so a hand-edited settings file loads as empty like a missing one.
		const std::string text = buffer.str();
		const nlohmann::json::parser_callback_t noParseCallback = nullptr;
		const bool allowExceptions = false;
		nlohmann::json parsed = nlohmann::json::parse(text, noParseCallback, allowExceptions);

		if (parsed.is_discarded() || !parsed.is_object())
		{
			const bool holdsMoreThanWhitespace = text.find_first_not_of(kJsonWhitespace) != std::string::npos;
			file.m_Malformed = holdsMoreThanWhitespace;
			return file;
		}

		file.m_Document->Root = std::move(parsed);
		return file;
	}

	std::string JsonFile::Get(std::string_view key) const
	{
		const nlohmann::json& root = Document::RootOf(m_Document);

		const nlohmann::json::const_iterator entry = root.find(std::string{key});
		if (entry == root.end() || !entry->is_string())
		{
			return {};
		}

		return entry->get<std::string>();
	}

	void JsonFile::Set(std::string_view key, std::string value)
	{
		Document::WritableRootOf(m_Document)[std::string{key}] = std::move(value);
	}

	void JsonFile::Remove(std::string_view key)
	{
		Document::WritableRootOf(m_Document).erase(std::string{key});
	}

	std::vector<std::string> JsonFile::CollectKeys() const
	{
		const nlohmann::json& root = Document::RootOf(m_Document);

		std::vector<std::string> keys;
		keys.reserve(root.size());

		for (nlohmann::json::const_iterator entry = root.cbegin(); entry != root.cend(); ++entry)
		{
			keys.push_back(entry.key());
		}

		return keys;
	}

	std::size_t JsonFile::GetCount() const
	{
		return Document::RootOf(m_Document).size();
	}

	Result<void> JsonFile::Save(const std::filesystem::path& path) const
	{
		// dump() throws on invalid UTF-8 unless told to replace it with U+FFFD, and a visibly
		// mangled setting is better than an exception crossing out of Core. Serialising before
		// WriteFileAtomically opens anything also means a throw here could not leave a .tmp behind.
		const std::string text = Document::RootOf(m_Document).dump(kIndentWidth, kIndentCharacter, kEscapeNonAscii, nlohmann::json::error_handler_t::replace);

		return WriteFileAtomically(path, text);
	}
}
