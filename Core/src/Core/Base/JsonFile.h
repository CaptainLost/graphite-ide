#pragma once

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

	private:
		struct Document;
		std::unique_ptr<Document> m_Document;
	};
}