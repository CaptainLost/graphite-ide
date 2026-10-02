#pragma once

#include "Core/Base/Base.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	class JsonFile
	{
	public:
		JsonFile();
		~JsonFile();

		JsonFile(JsonFile&&) noexcept;
		JsonFile& operator=(JsonFile&&) noexcept;

		JsonFile(const JsonFile&) = delete;
		JsonFile& operator=(const JsonFile&) = delete;

		static JsonFile Load(const std::filesystem::path& path);

		std::string Get(std::string_view key) const;

		void Set(std::string_view key, std::string value);

		void Remove(std::string_view key);

		bool Save(const std::filesystem::path& path) const;

		std::size_t GetCount() const;

		std::vector<std::string> GetKeys() const;

		// True when Load found a file holding something other than a JSON object - a typo in a
		// hand edit, say. It reads as empty like a missing file, but unlike a missing or blank one
		// it has content a save over it would lose.
		bool IsMalformed() const { return m_Malformed; }

	private:
		struct Document;
		Scope<Document> m_Document;
		bool m_Malformed = false;
	};
}