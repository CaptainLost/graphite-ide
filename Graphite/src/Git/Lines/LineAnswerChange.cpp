#include "Git/Lines/LineAnswerChange.h"

#include "Core/Git/Lines/LineModel.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		bool Contains(const std::vector<std::string>& names, const std::string& name)
		{
			return std::ranges::find(names, name) != names.end();
		}

		void AppendWhenAbsent(std::vector<std::string>& names, const std::string& name)
		{
			if (!Contains(names, name))
			{
				names.push_back(name);
			}
		}

		void UseAsBase(Core::LineAnswers& answers, const std::string& ref)
		{
			std::erase(answers.RemovedBases, ref);
			AppendWhenAbsent(answers.AddedBases, ref);
			AppendWhenAbsent(answers.BaseOrder, ref);
		}

		void StopUsingAsBase(Core::LineAnswers& answers, const std::string& ref)
		{
			AppendWhenAbsent(answers.RemovedBases, ref);
			std::erase(answers.AddedBases, ref);
			std::erase(answers.KeptBases, ref);
			std::erase(answers.BaseOrder, ref);

			if (answers.MainLine == ref)
			{
				answers.MainLine.reset();
			}
		}

		void UseAsMainLine(Core::LineAnswers& answers, const std::string& ref)
		{
			answers.MainLine = ref;
			std::erase(answers.RemovedBases, ref);
		}
	}

	void ApplyLineChange(Core::LineAnswers& answers, const LineAnswerChange& change)
	{
		switch (change.What)
		{
		case LineAnswerChange::Kind::UseAsBase:
			UseAsBase(answers, change.Ref);
			break;
		case LineAnswerChange::Kind::StopUsingAsBase:
			StopUsingAsBase(answers, change.Ref);
			break;
		case LineAnswerChange::Kind::UseAsMainLine:
			UseAsMainLine(answers, change.Ref);
			break;
		case LineAnswerChange::Kind::CompareWith:
			answers.Comparisons[change.Ref] = change.Target;
			break;
		case LineAnswerChange::Kind::CompareWithNothing:
			answers.Comparisons[change.Ref] = std::nullopt;
			break;
		case LineAnswerChange::Kind::CompareAutomatically:
			answers.Comparisons.erase(change.Ref);
			break;
		}
	}

	Core::Result<LineRead> RecomputeLines(const LineRead& last, const LineStorePaths& store, const LineAnswerChange& change, std::int64_t now)
	{
		// WARNING: The store is resolved from the folder open when the change runs. After a folder switch it is another
		// repository's, whose answers this read's names must never be written into.
		if (last.Store.Answers != store.Answers)
		{
			return Core::Error{Core::ErrorCode::InvalidArgument, "The folder changed, so the change was not saved."};
		}

		std::optional<Core::LineAnswers> answers = LoadLineAnswersToUpdate(store.Answers);

		if (!answers.has_value())
		{
			return Core::Error{Core::ErrorCode::IoFailure, "The line answers file cannot be read, so the change was not saved."};
		}

		ApplyLineChange(*answers, change);

		const Core::Result<void> saved = SaveLineAnswers(store.Answers, *answers);
		if (!saved.HasValue())
		{
			return saved.GetError();
		}

		LineRead read;
		read.Branches = last.Branches;
		read.Tags = last.Tags;
		read.Remotes = last.Remotes;
		read.Refs = last.Refs;
		read.Graph = last.Graph;
		read.Generations = last.Generations;
		read.Model = Core::ComputeLines(Core::LineModelInput{*read.Graph, read.Refs, *answers, now});
		read.Now = now;
		read.Store = store;
		read.Answers = std::move(*answers);
		return read;
	}
}
