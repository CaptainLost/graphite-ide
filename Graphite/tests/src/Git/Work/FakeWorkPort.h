#pragma once

#include "Git/Work/WorkPort.h"

#include <cstddef>
#include <filesystem>
#include <optional>

class FakeWorkPort final : public Graphite::WorkPort
{
public:
	Core::Result<Graphite::WorkFacts> ReadWorkFacts(const Graphite::WorkFactsRequest& request) override
	{
		++FactsReads;
		LastRequest = request;

		if (FactsFailure.has_value())
		{
			return *FactsFailure;
		}

		Graphite::WorkFacts answer = Facts;
		answer.Answers = request;
		return answer;
	}

	Core::Result<std::size_t> CountUncommitted(const std::filesystem::path&) override
	{
		++CountReads;
		return CountAnswer;
	}

	Graphite::WorkFacts Facts;
	std::optional<Core::Error> FactsFailure;
	std::size_t CountAnswer = 0;

	int FactsReads = 0;
	int CountReads = 0;
	Graphite::WorkFactsRequest LastRequest;
};
