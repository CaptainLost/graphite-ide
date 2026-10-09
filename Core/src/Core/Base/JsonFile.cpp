#include "corepch.h"

#include "Core/Base/JsonFile.h"

#include "Core/Base/Base.h"

#include "Core/FileSystem/AtomicWrite.h"

#include <nlohmann/json.hpp>

#include <fstream>

#include <span>

#include <sstream>

#include <string_view>

#include <utility>

namespace Core {

	namespace {
		constexpr bool kNotMalformed = false;
		constexpr std::string_view kJsonWhitespace = " \t\r\n";

		constexpr int kIndentWidth = 4;
		constexpr char kIndentCharacter = ' ';
		constexpr bool kEscapeNonAscii = false;

		constexpr int kCompactIndent = -1;

		std::string DumpCompact(const nlohmann::json& value)
		{
			return value.dump(kCompactIndent, kIndentCharacter, kEscapeNonAscii, nlohmann::json::error_handler_t::replace);
		}

		nlohmann::json ParseOrDiscard(std::string_view text)
		{
			const nlohmann::json::parser_callback_t noParseCallback = nullptr;
			const bool allowExceptions = false;
			return nlohmann::json::parse(text.begin(), text.end(), noParseCallback, allowExceptions);
		}

		const nlohmann::json* FindAt(const nlohmann::json& root, std::span<const std::string> path)
		{
			const nlohmann::json* current = &root;

			for (const std::string& key : path)
			{
				if (!current->is_object())
				{
					return nullptr;
				}

				const nlohmann::json::const_iterator member = current->find(key);

				if (member == current->end())
				{
					return nullptr;
				}

				current = &*member;
			}

			return current;
		}

		bool IsObjectWithMembers(const nlohmann::json& value)
		{
			return value.is_object() && !value.empty();
		}

		void CollectLeavesUnder(const nlohmann::json& value, std::vector<std::string>& path, std::vector<JsonLeaf>& leaves)
		{
			if (!IsObjectWithMembers(value))
			{
				leaves.push_back(JsonLeaf{.Path = path, .Json = DumpCompact(value)});
				return;
			}

			for (nlohmann::json::const_iterator member = value.cbegin(); member != value.cend(); ++member)
			{
				path.push_back(member.key());
				CollectLeavesUnder(member.value(), path, leaves);
				path.pop_back();
			}
		}

		bool EraseAt(nlohmann::json& parent, std::span<const std::string> path)
		{
			if (!parent.is_object())
			{
				return false;
			}

			const std::string& key = path.front();

			if (path.size() == 1)
			{
				return parent.erase(key) > 0;
			}

			const nlohmann::json::iterator child = parent.find(key);

			if (child == parent.end())
			{
				return false;
			}

			const bool erased = EraseAt(*child, path.subspan(1));
			const bool childLeftEmpty = erased && child->is_object() && child->empty();

			if (childLeftEmpty)
			{
				parent.erase(key);
			}

			return erased;
		}
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

	std::optional<std::string> JsonFile::GetJsonAt(std::span<const std::string> path) const
	{
		if (path.empty())
		{
			return std::nullopt;
		}

		const nlohmann::json* value = FindAt(Document::RootOf(m_Document), path);

		if (value == nullptr)
		{
			return std::nullopt;
		}

		return DumpCompact(*value);
	}

	void JsonFile::SetJsonAt(std::span<const std::string> path, std::string_view json)
	{
		if (path.empty())
		{
			return;
		}

		nlohmann::json parsed = ParseOrDiscard(json);

		if (parsed.is_discarded())
		{
			return;
		}

		nlohmann::json* parent = &Document::WritableRootOf(m_Document);

		for (const std::string& key : path.first(path.size() - 1))
		{
			nlohmann::json& child = (*parent)[key];

			if (!child.is_object())
			{
				child = nlohmann::json::object();
			}

			parent = &child;
		}

		(*parent)[path.back()] = std::move(parsed);
	}

	void JsonFile::RemoveAt(std::span<const std::string> path)
	{
		if (path.empty())
		{
			return;
		}

		EraseAt(Document::WritableRootOf(m_Document), path);
	}

	std::vector<JsonLeaf> JsonFile::CollectLeaves() const
	{
		const nlohmann::json& root = Document::RootOf(m_Document);

		std::vector<JsonLeaf> leaves;
		std::vector<std::string> path;

		for (nlohmann::json::const_iterator member = root.cbegin(); member != root.cend(); ++member)
		{
			path.push_back(member.key());
			CollectLeavesUnder(member.value(), path, leaves);
			path.pop_back();
		}

		return leaves;
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
