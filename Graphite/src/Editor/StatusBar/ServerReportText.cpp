#include "Editor/StatusBar/ServerReportText.h"

#include "UI/TypographicMarks.h"

#include "Core/Base/Strings.h"
#include "Core/FileSystem/FileSystem.h"

#include <format>
#include <optional>
#include <string_view>
#include <vector>

namespace Graphite {

	namespace {
		constexpr long long kSecondsPerMinute = 60;
		constexpr long long kSecondsPerHour = 3600;
		constexpr long long kHoursPerDay = 24;
		constexpr std::string_view kEventTimeGap = "  ";

		std::string DurationText(std::chrono::seconds span)
		{
			const long long seconds = span.count();

			if (seconds < kSecondsPerMinute)
			{
				return std::format("{} s", seconds);
			}

			if (seconds < kSecondsPerHour)
			{
				const long long leftOver = seconds % kSecondsPerMinute;

				return leftOver == 0 ? std::format("{} min", seconds / kSecondsPerMinute)
									 : std::format("{} min {} s", seconds / kSecondsPerMinute, leftOver);
			}

			return std::format("{} h {} min", seconds / kSecondsPerHour, seconds % kSecondsPerHour / kSecondsPerMinute);
		}

		std::optional<std::string_view> StateWord(ServerState state)
		{
			switch (state)
			{
			case ServerState::Starting:
				return "Starting";
			case ServerState::Indexing:
				return "Indexing";
			case ServerState::Ready:
				return "Ready";
			case ServerState::ProjectIncomplete:
				return "Limited";
			case ServerState::Failed:
				return "Failed";

			case ServerState::NotConfigured:
			case ServerState::Off:
			case ServerState::NotFound:
			case ServerState::NotInstalled:
			case ServerState::MissingRuntime:
			case ServerState::Restricted:
				break;
			}

			return std::nullopt;
		}

		std::string TimeOfDay(std::chrono::system_clock::time_point at, std::chrono::minutes localOffset)
		{
			constexpr long long kSecondsPerDay = kSecondsPerHour * kHoursPerDay;

			const std::chrono::sys_seconds instant = std::chrono::floor<std::chrono::seconds>(at);
			const long long local = (instant + localOffset).time_since_epoch().count();
			const long long seconds = (local % kSecondsPerDay + kSecondsPerDay) % kSecondsPerDay;

			return std::format("{:02}:{:02}:{:02}", seconds / kSecondsPerHour, seconds % kSecondsPerHour / kSecondsPerMinute,
							   seconds % kSecondsPerMinute);
		}

		std::string EventLine(const ServerEvent& event, std::chrono::minutes localOffset)
		{
			std::string line = TimeOfDay(event.At, localOffset) + std::string{kEventTimeGap} + event.Text;

			if (event.Took)
			{
				line += " in " + DurationText(*event.Took);
			}

			return line;
		}

		std::string BusyLine(const ServerReport& report)
		{
			return Core::Counted(report.OpenDocuments, "open file") + std::string{kMiddleDotSeparator} +
				   Core::Counted(report.RequestsInFlight, "request") + " in flight";
		}
	}

	std::string ServerReportText(const ServerReport& report, std::chrono::minutes localOffset)
	{
		std::vector<std::string> lines;

		if (!report.Status.Activity.empty())
		{
			lines.push_back("Working on: " + report.Status.Activity);
		}

		lines.push_back("Folder: " + Core::ToUtf8(report.Root));
		lines.push_back(BusyLine(report));

		if (const std::optional<std::string_view> state = StateWord(report.Status.State); state && report.InStateFor)
		{
			lines.push_back(std::string{*state} + " for " + DurationText(*report.InStateFor));
		}

		std::string text = Core::JoinedWith(lines, "\n");

		if (report.History.empty())
		{
			return text;
		}

		text += "\n\nRecent:";

		const std::size_t firstShown = report.History.size() > kServerEventsShown ? report.History.size() - kServerEventsShown : 0;

		for (std::size_t eventIndex = firstShown; eventIndex < report.History.size(); ++eventIndex)
		{
			text += "\n" + EventLine(report.History[eventIndex], localOffset);
		}

		return text;
	}
}
