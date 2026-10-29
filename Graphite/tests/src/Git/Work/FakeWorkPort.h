#pragma once

#include "Git/Work/WorkPort.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

class FakeWorkPort final : public Graphite::WorkPort
{
public:
	Core::Result<Graphite::WorkFacts> ReadWorkFacts(const Graphite::WorkFactsRequest& request,
													const Graphite::WorkFactsProgress& progress) override
	{
		++FactsReads;
		LastRequest = request;
		LastHadProgress = static_cast<bool>(progress);

		// Each step is handed out as a read would hand out what it has so far; AfterStep runs after each.
		for (Graphite::WorkFacts step : Steps)
		{
			step.Answers = request;
			step.Partial = true;

			if (progress)
			{
				progress(step);
			}

			if (AfterStep)
			{
				AfterStep();
			}
		}

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

	std::vector<Graphite::WorkFacts> Steps;
	std::function<void()> AfterStep;

	int FactsReads = 0;
	int CountReads = 0;
	Graphite::WorkFactsRequest LastRequest;
	bool LastHadProgress = false;
};
