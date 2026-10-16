#include "Persistence/StoredVersion.h"

#include <nlohmann/json.hpp>

#include <compare>

namespace Graphite {

	namespace {

		StoredVersion FromComparison(std::partial_ordering storedAgainstCurrent)
		{
			if (storedAgainstCurrent < 0)
			{
				return StoredVersion::Older;
			}

			if (storedAgainstCurrent > 0)
			{
				return StoredVersion::Newer;
			}

			return StoredVersion::Current;
		}
	}

	StoredVersion StoredVersionOf(const nlohmann::json& document, std::string_view key, std::int64_t current)
	{
		if (!document.is_object())
		{
			return StoredVersion::Missing;
		}

		const nlohmann::json::const_iterator field = document.find(key);

		if (field == document.end())
		{
			return StoredVersion::Missing;
		}

		if (field->is_number_integer())
		{
			return FromComparison(field->get<std::int64_t>() <=> current);
		}

		if (field->is_number())
		{
			return FromComparison(field->get<double>() <=> static_cast<double>(current));
		}

		return StoredVersion::NotANumber;
	}
}