#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/Result.h"

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

		Result<void> Save(const std::filesystem::path& path) const;

		std::size_t GetCount() const;

		std::vector<std::string> CollectKeys() const;

		// A malformed file reads as empty, like a missing one, but unlike a missing or blank one it
		// holds what a person wrote, which a save over it would lose.
		bool IsMalformed() const { return m_Malformed; }

	private:
		struct Document;
		Scope<Document> m_Document;
		bool m_Malformed = false;
	};
}