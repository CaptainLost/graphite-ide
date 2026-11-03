#include "Git/Lines/LineStore.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <system_error>

namespace Graphite {

	namespace {
		constexpr std::int64_t kAnswersVersion = 1;
		constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
		constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

		std::string NormalisedKey(const std::filesystem::path& directory)
		{
			std::error_code error;
			std::filesystem::path absolute = std::filesystem::absolute(directory, error);

			if (error)
			{
				absolute = directory;
			}

			std::string text = Core::ToGenericUtf8(absolute.lexically_normal());

			while (text.size() > 1 && text.back() == '/')
			{
				text.pop_back();
			}

			for (char& character : text)
			{
				if (character >= 'A' && character <= 'Z')
				{
					character = static_cast<char>(character - 'A' + 'a');
				}
			}

			return text;
		}

		std::string HexName(std::string_view key)
		{
			std::uint64_t hash = kFnvOffset;

			for (const char character : key)
			{
				hash ^= static_cast<unsigned char>(character);
				hash *= kFnvPrime;
			}

			constexpr char kDigits[] = "0123456789abcdef";
			std::string name(16, '0');

			for (std::size_t index = 16; index > 0; --index)
			{
				name[index - 1] = kDigits[hash & 0xF];
				hash >>= 4;
			}

			return name;
		}

		std::vector<std::string> NamesFrom(const nlohmann::json& document, const char* key)
		{
			std::vector<std::string> names;
			const auto field = document.find(key);

			if (field == document.end() || !field->is_array())
			{
				return names;
			}

			for (const nlohmann::json& entry : *field)
			{
				if (entry.is_string())
				{
					names.push_back(entry.get<std::string>());
				}
			}

			return names;
		}

		nlohmann::json NameOrNull(const std::optional<std::string>& name)
		{
			return name ? nlohmann::json(*name) : nlohmann::json(nullptr);
		}
	}

	LineStorePaths LineStoreFor(const std::filesystem::path& gitCommonDirectory, const std::filesystem::path& root)
	{
		const std::filesystem::path folder = root / HexName(NormalisedKey(gitCommonDirectory));

		return LineStorePaths{folder / "answers.json", folder / "graph.bin"};
	}

	Core::LineAnswers LoadLineAnswers(const std::filesystem::path& file)
	{
		const Core::Result<std::string> bytes = Core::ReadFileBytes(file);

		if (!bytes)
		{
			return {};
		}

		const nlohmann::json document = nlohmann::json::parse(bytes.GetValue(), nullptr, false);

		if (document.is_discarded() || !document.is_object())
		{
			APP_WARN("Ignoring the unreadable git answers file '{}'", Core::ToUtf8(file));
			return {};
		}

		const auto version = document.find("version");

		if (version == document.end() || !version->is_number_integer() || version->get<std::int64_t>() != kAnswersVersion)
		{
			return {};
		}

		Core::LineAnswers answers;
		answers.AddedBases = NamesFrom(document, "addedBases");
		answers.RemovedBases = NamesFrom(document, "removedBases");
		answers.KeptBases = NamesFrom(document, "keptBases");

		if (const auto mainLine = document.find("mainLine"); mainLine != document.end() && mainLine->is_string())
		{
			answers.MainLine = mainLine->get<std::string>();
		}

		if (const auto comparisons = document.find("comparisons"); comparisons != document.end() && comparisons->is_object())
		{
			for (const auto& [branch, target] : comparisons->items())
			{
				if (target.is_string())
				{
					answers.Comparisons[branch] = target.get<std::string>();
				}
				else if (target.is_null())
				{
					answers.Comparisons[branch] = std::nullopt;
				}
			}
		}

		return answers;
	}

	Core::Result<void> SaveLineAnswers(const std::filesystem::path& file, const Core::LineAnswers& answers)
	{
		nlohmann::json document = nlohmann::json::object();
		document["version"] = kAnswersVersion;
		document["addedBases"] = answers.AddedBases;
		document["removedBases"] = answers.RemovedBases;
		document["keptBases"] = answers.KeptBases;
		document["mainLine"] = NameOrNull(answers.MainLine);

		nlohmann::json comparisons = nlohmann::json::object();

		for (const auto& [branch, target] : answers.Comparisons)
		{
			comparisons[branch] = NameOrNull(target);
		}

		document["comparisons"] = std::move(comparisons);

		std::error_code error;
		std::filesystem::create_directories(file.parent_path(), error);

		return Core::WriteFileAtomically(file, document.dump(1, '\t'));
	}
}
