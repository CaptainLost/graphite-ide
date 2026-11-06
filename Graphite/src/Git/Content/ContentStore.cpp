#include "Git/Content/ContentStore.h"

#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::int64_t kContentVersion = 1;

		nlohmann::json VerdictToJson(const Core::ContentVerdict& verdict)
		{
			nlohmann::json json = nlohmann::json::object();
			json["outcome"] = verdict.Outcome == Core::ContentOutcome::Merged ? "merged" : "notMerged";

			if (verdict.Outcome != Core::ContentOutcome::Merged)
			{
				return json;
			}

			json["kind"] = verdict.Kind == Core::ContentKind::Squash ? "squash" : "rebase";

			if (verdict.Landing)
			{
				json["landing"] = verdict.Landing->ToHex();
			}

			nlohmann::json twins = nlohmann::json::array();

			for (const Core::PatchTwin& twin : verdict.Twins)
			{
				twins.push_back(nlohmann::json::array({twin.Own.ToHex(), twin.Twin.ToHex()}));
			}

			json["twins"] = std::move(twins);
			return json;
		}

		std::optional<Core::Oid> OidFrom(const nlohmann::json& json)
		{
			if (!json.is_string())
			{
				return std::nullopt;
			}

			return Core::Oid::FromHex(json.get<std::string>());
		}

		std::optional<Core::Oid> OidAt(const nlohmann::json& object, const char* key)
		{
			const auto found = object.find(key);

			if (found == object.end())
			{
				return std::nullopt;
			}

			return OidFrom(*found);
		}

		std::optional<Core::PatchTwin> TwinFrom(const nlohmann::json& json)
		{
			if (!json.is_array() || json.size() != 2)
			{
				return std::nullopt;
			}

			const std::optional<Core::Oid> own = OidFrom(json[0]);
			const std::optional<Core::Oid> twin = OidFrom(json[1]);

			if (!own || !twin)
			{
				return std::nullopt;
			}

			return Core::PatchTwin{*own, *twin};
		}

		std::optional<Core::ContentKind> KindFrom(const nlohmann::json& json)
		{
			const auto kind = json.find("kind");

			if (kind == json.end() || !kind->is_string())
			{
				return std::nullopt;
			}

			if (kind->get<std::string>() == "squash")
			{
				return Core::ContentKind::Squash;
			}

			if (kind->get<std::string>() == "rebase")
			{
				return Core::ContentKind::Rebase;
			}

			return std::nullopt;
		}

		// Nothing for an entry that is malformed or says Unknown.
		std::optional<Core::ContentVerdict> VerdictFrom(const nlohmann::json& json)
		{
			if (!json.is_object())
			{
				return std::nullopt;
			}

			const auto outcome = json.find("outcome");

			if (outcome == json.end() || !outcome->is_string())
			{
				return std::nullopt;
			}

			Core::ContentVerdict verdict;

			if (outcome->get<std::string>() == "notMerged")
			{
				return verdict;
			}

			if (outcome->get<std::string>() != "merged")
			{
				return std::nullopt;
			}

			const std::optional<Core::ContentKind> kind = KindFrom(json);

			if (!kind)
			{
				return std::nullopt;
			}

			verdict.Outcome = Core::ContentOutcome::Merged;
			verdict.Kind = *kind;

			if (json.contains("landing"))
			{
				verdict.Landing = OidAt(json, "landing");

				if (!verdict.Landing)
				{
					return std::nullopt;
				}
			}

			if (const auto twins = json.find("twins"); twins != json.end())
			{
				if (!twins->is_array())
				{
					return std::nullopt;
				}

				for (const nlohmann::json& entry : *twins)
				{
					const std::optional<Core::PatchTwin> twin = TwinFrom(entry);

					if (!twin)
					{
						return std::nullopt;
					}

					verdict.Twins.push_back(*twin);
				}
			}

			return verdict;
		}

		void ReadVerdicts(const nlohmann::json& document, ContentCache& cache)
		{
			const auto list = document.find("verdicts");

			if (list == document.end() || !list->is_array())
			{
				return;
			}

			for (const nlohmann::json& entry : *list)
			{
				if (!entry.is_object())
				{
					continue;
				}

				const std::optional<Core::Oid> branch = OidAt(entry, "branch");
				const std::optional<Core::Oid> line = OidAt(entry, "line");
				const auto verdictJson = entry.find("verdict");

				if (!branch || !line || verdictJson == entry.end())
				{
					continue;
				}

				if (const std::optional<Core::ContentVerdict> verdict = VerdictFrom(*verdictJson))
				{
					cache.Verdicts[ContentKey{*branch, *line}] = *verdict;
				}
			}
		}

		void ReadFound(const nlohmann::json& document, ContentCache& cache)
		{
			const auto list = document.find("found");

			if (list == document.end() || !list->is_array())
			{
				return;
			}

			for (const nlohmann::json& entry : *list)
			{
				if (!entry.is_object())
				{
					continue;
				}

				const std::optional<Core::Oid> branch = OidAt(entry, "branch");
				const auto line = entry.find("line");
				const auto verdictJson = entry.find("verdict");

				if (!branch || line == entry.end() || !line->is_string() || verdictJson == entry.end())
				{
					continue;
				}

				const std::optional<Core::ContentVerdict> verdict = VerdictFrom(*verdictJson);

				if (verdict && verdict->Outcome == Core::ContentOutcome::Merged)
				{
					cache.Found[*branch] = ContentFound{line->get<std::string>(), *verdict};
				}
			}
		}
	}

	ContentCache LoadContentCache(const std::filesystem::path& file)
	{
		const Core::Result<std::string> bytes = Core::ReadFileBytes(file);

		if (!bytes)
		{
			return {};
		}

		const nlohmann::json document = nlohmann::json::parse(bytes.GetValue(), nullptr, false);

		if (document.is_discarded() || !document.is_object())
		{
			return {};
		}

		const auto version = document.find("version");

		if (version == document.end() || !version->is_number_integer() || version->get<std::int64_t>() != kContentVersion)
		{
			return {};
		}

		ContentCache cache;
		ReadVerdicts(document, cache);
		ReadFound(document, cache);
		return cache;
	}

	Core::Result<void> SaveContentCache(const std::filesystem::path& file, const ContentCache& cache)
	{
		nlohmann::json verdicts = nlohmann::json::array();

		for (const auto& [key, verdict] : cache.Verdicts)
		{
			if (verdict.Outcome == Core::ContentOutcome::Unknown)
			{
				continue;
			}

			nlohmann::json entry = nlohmann::json::object();
			entry["branch"] = key.BranchTip.ToHex();
			entry["line"] = key.LineTip.ToHex();
			entry["verdict"] = VerdictToJson(verdict);
			verdicts.push_back(std::move(entry));
		}

		nlohmann::json found = nlohmann::json::array();

		for (const auto& [branch, remembered] : cache.Found)
		{
			if (remembered.Verdict.Outcome != Core::ContentOutcome::Merged)
			{
				continue;
			}

			nlohmann::json entry = nlohmann::json::object();
			entry["branch"] = branch.ToHex();
			entry["line"] = remembered.Line;
			entry["verdict"] = VerdictToJson(remembered.Verdict);
			found.push_back(std::move(entry));
		}

		nlohmann::json document = nlohmann::json::object();
		document["version"] = kContentVersion;
		document["verdicts"] = std::move(verdicts);
		document["found"] = std::move(found);

		std::error_code error;
		std::filesystem::create_directories(file.parent_path(), error);

		return Core::WriteFileAtomically(file, document.dump(1, '\t'));
	}
}
