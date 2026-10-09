#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/Result.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	struct JsonLeaf
	{
		std::vector<std::string> Path;
		std::string Json;

		friend bool operator==(const JsonLeaf&, const JsonLeaf&) = default;
	};

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

		std::optional<std::string> GetJsonAt(std::span<const std::string> path) const;

		void SetJsonAt(std::span<const std::string> path, std::string_view json);

		void RemoveAt(std::span<const std::string> path);

		// A leaf is any value that is not an object with members, so an empty object is kept as one.
		std::vector<JsonLeaf> CollectLeaves() const;

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