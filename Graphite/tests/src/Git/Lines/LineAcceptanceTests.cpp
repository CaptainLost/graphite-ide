#include <doctest/doctest.h>

#include "Git/Lines/LineReader.h"
#include "Git/Lines/LineStore.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"

#include "TempDirectory.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>

using namespace Graphite;

namespace {
	std::string EnvironmentText(const char* name)
	{
		char* value = nullptr;
		std::size_t length = 0;
		if (::_dupenv_s(&value, &length, name) != 0 || value == nullptr)
		{
			return {};
		}

		std::string text{value};
		std::free(value);
		return text;
	}

	double Milliseconds(std::chrono::steady_clock::duration duration)
	{
		return std::chrono::duration<double, std::milli>(duration).count();
	}

	const char* ReasonText(Core::BaseReason reason)
	{
		switch (reason)
		{
		case Core::BaseReason::ServerDefault: return "ServerDefault";
		case Core::BaseReason::MainLineByUser: return "MainLineByUser";
		case Core::BaseReason::MainLineGuess: return "MainLineGuess";
		case Core::BaseReason::AddedByUser: return "AddedByUser";
		case Core::BaseReason::Kept: return "Kept";
		case Core::BaseReason::MergedLines: return "MergedLines";
		case Core::BaseReason::Authors: return "Authors";
		}

		return "?";
	}

	const char* KindText(Core::ComparedKind kind)
	{
		switch (kind)
		{
		case Core::ComparedKind::None: return "None";
		case Core::ComparedKind::Base: return "Base";
		case Core::ComparedKind::Branch: return "Branch";
		}

		return "?";
	}

	const char* StateText(Core::BranchState state)
	{
		switch (state)
		{
		case Core::BranchState::NoOwnCommits: return "NoOwnCommits";
		case Core::BranchState::MergedByHistory: return "MergedByHistory";
		case Core::BranchState::NotMerged: return "NotMerged";
		case Core::BranchState::NotCompared: return "NotCompared";
		case Core::BranchState::NoCommonHistory: return "NoCommonHistory";
		case Core::BranchState::HistoryCutOff: return "HistoryCutOff";
		case Core::BranchState::TipMissing: return "TipMissing";
		}

		return "?";
	}
}

// Skipped by default: it needs a real repository. Run with
//   GRAPHITE_LINES_REPO=<repository> [GRAPHITE_LINES_OUT=<tsv file>] GraphiteTests -tc="Lines: acceptance*" --no-skip
TEST_CASE("Lines: acceptance on the repository in GRAPHITE_LINES_REPO" * doctest::skip())
{
	const std::string repositoryPath = EnvironmentText("GRAPHITE_LINES_REPO");
	REQUIRE_MESSAGE(!repositoryPath.empty(), "GRAPHITE_LINES_REPO is not set");

	const Core::Result<Core::Repository> repository = Core::Repository::Open(Core::FromUtf8(repositoryPath));
	INFO((repository.HasValue() ? std::string{} : repository.GetError().Message));
	REQUIRE(repository.HasValue());

	const TempDirectory directory;
	const LineStorePaths store = LineStoreFor(repository.GetValue().CommonDirectory(), directory.GetPath() / "store");
	const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

	const auto coldStart = std::chrono::steady_clock::now();
	const Core::Result<LineRead> cold = ReadLines(repository.GetValue(), store, now);
	const double coldMilliseconds = Milliseconds(std::chrono::steady_clock::now() - coldStart);
	INFO((cold.HasValue() ? std::string{} : cold.GetError().Message));
	REQUIRE(cold.HasValue());

	const auto warmStart = std::chrono::steady_clock::now();
	const Core::Result<LineRead> warm = ReadLines(repository.GetValue(), store, now);
	const double warmMilliseconds = Milliseconds(std::chrono::steady_clock::now() - warmStart);
	INFO((warm.HasValue() ? std::string{} : warm.GetError().Message));
	REQUIRE(warm.HasValue());

	const LineRead& read = warm.GetValue();
	const Core::LineModel& model = read.Model;

	MESSAGE("commits read (cold): ", cold.GetValue().CommitsRead, ", refs: ", read.Refs.size());
	MESSAGE("cold read ", coldMilliseconds, " ms (reads the whole graph), warm read ", warmMilliseconds,
			" ms (cached graph and the model), commits read warm: ", read.CommitsRead);

	for (const Core::LineBase& base : model.Bases)
	{
		MESSAGE("base ", read.Refs[base.Ref].FullName, " (", std::string{ReasonText(base.Reason)}, std::string{base.Active ? ", active)" : ", quiet)"});
	}

	for (const Core::OldCopy& copy : model.OldCopies)
	{
		MESSAGE("old copy ", read.Refs[copy.Ref].FullName, " of ", read.Refs[copy.Of].FullName);
	}

	MESSAGE("MainLineGuessed: ", std::string{model.MainLineGuessed ? "yes" : "no"});

	std::map<std::string, std::size_t> perKind;
	std::map<std::string, std::size_t> perState;
	for (const Core::BranchLine& line : model.Branches)
	{
		++perKind[KindText(line.Kind)];
		++perState[StateText(line.State)];
	}

	for (const auto& [kind, count] : perKind)
	{
		MESSAGE("kind ", kind, ": ", count);
	}

	for (const auto& [state, count] : perState)
	{
		MESSAGE("state ", state, ": ", count);
	}

	if (const std::string outPath = EnvironmentText("GRAPHITE_LINES_OUT"); !outPath.empty())
	{
		std::ofstream out{Core::FromUtf8(outPath), std::ios::binary | std::ios::trunc};
		REQUIRE(out.is_open());

		for (const Core::BranchLine& line : model.Branches)
		{
			const std::string comparedWith = line.ComparedWith.has_value() ? read.Refs[*line.ComparedWith].FullName : std::string{};
			out << read.Refs[line.Ref].FullName << '\t' << comparedWith << '\t' << KindText(line.Kind) << '\t' << StateText(line.State)
				<< '\t' << line.Ahead << '\n';
		}
	}
}
