#include "Language/Install/InstallSummary.h"

#include "Core/Platform/CurrentPlatform.h"

#include <format>
#include <string_view>

namespace Graphite {

	namespace {

		constexpr std::uint64_t kMegabyte = 1024 * 1024;
		constexpr std::uint64_t kPercent = 100;
		constexpr std::string_view kCannotDownload = "Graphite cannot download in this session; the log says why.";

		void OfferDownload(InstallSummary& summary, bool canDownload, InstallAction action)
		{
			if (canDownload)
			{
				summary.Actions.push_back(action);
				return;
			}

			if (summary.Note.empty())
			{
				summary.Note = std::string{kCannotDownload};
			}
		}

		// The job before this one stays the latest change until this one reports its first step.
		bool HasReported(const std::optional<InstallProgress>& latest)
		{
			return latest.has_value() && latest->Kind == InstallJobKind::Install && !IsFinished(latest->Step);
		}

		InstallSummary SummariseRunningJob(InstallJobKind kind, bool cancelling, const std::optional<InstallProgress>& latest)
		{
			if (cancelling)
			{
				return InstallSummary{.State = "Cancelling"};
			}

			switch (kind)
			{
			case InstallJobKind::CheckForUpdates:
				return InstallSummary{.State = "Checking for updates"};
			case InstallJobKind::Uninstall:
				return InstallSummary{.State = "Uninstalling"};
			case InstallJobKind::Install:
				break;
			}

			return InstallSummary{.State = HasReported(latest) ? InstallProgressText(*latest) : std::string{"Waiting to install"},
								  .Actions = {InstallAction::Cancel}};
		}

		bool OffersUpdate(const InstallInputs& inputs)
		{
			return inputs.Latest.has_value() && inputs.Latest->Step == InstallStep::UpdateAvailable &&
				   inputs.Latest->Version != inputs.Installed->Version;
		}

		bool ConfirmedUpToDate(const InstallInputs& inputs)
		{
			return inputs.Latest.has_value() && inputs.Latest->Step == InstallStep::UpToDate;
		}
	}

	InstallInputs InstallInputsFor(const ServerInstalls& installs, const ServerDefinition& server)
	{
		return InstallInputs{.AvailableOnPlatform = IsAvailableOn(server.Package, Core::CurrentPlatform()),
							 .CanDownload = installs.CanDownload(),
							 .HasReadInstalls = installs.HasReadInstalls(),
							 .Installed = installs.InstalledVersionOf(server.Id),
							 .Running = installs.RunningJobOf(server.Id),
							 .Cancelling = installs.IsCancelling(server.Id),
							 .Latest = installs.ProgressOf(server.Id)};
	}

	InstallSummary SummariseInstall(const InstallInputs& inputs)
	{
		if (!inputs.AvailableOnPlatform)
		{
			return InstallSummary{.State = "Not available on this platform"};
		}

		if (!inputs.HasReadInstalls)
		{
			return InstallSummary{.State = "Reading what is installed"};
		}

		if (inputs.Running)
		{
			return SummariseRunningJob(*inputs.Running, inputs.Cancelling, inputs.Latest);
		}

		InstallSummary summary;

		if (inputs.Latest && inputs.Latest->Step == InstallStep::Failed)
		{
			summary.Failure = inputs.Latest->Failure;
		}

		if (!inputs.Installed)
		{
			summary.State = "Not installed";
			OfferDownload(summary, inputs.CanDownload, InstallAction::Install);
			return summary;
		}

		summary.State = "Installed " + inputs.Installed->Version + (inputs.Installed->HasChecksum ? "" : ", no checksum");

		if (OffersUpdate(inputs))
		{
			summary.Note = inputs.Latest->Version + " available";
			OfferDownload(summary, inputs.CanDownload, InstallAction::Update);
		}
		else
		{
			if (ConfirmedUpToDate(inputs))
			{
				summary.Note = "Up to date";
			}

			OfferDownload(summary, inputs.CanDownload, InstallAction::CheckForUpdates);
		}

		summary.Actions.push_back(InstallAction::Uninstall);
		return summary;
	}

	std::string InstallProgressText(const InstallProgress& progress)
	{
		switch (progress.Step)
		{
		case InstallStep::Resolving:
			return "Looking up the latest version";
		case InstallStep::Downloading:
			if (const std::optional<int> percent = InstallPercent(progress))
			{
				return std::format("Downloading {}% ({} / {} MB)", *percent, progress.ReceivedBytes / kMegabyte, *progress.TotalBytes / kMegabyte);
			}

			return std::format("Downloading {} MB", progress.ReceivedBytes / kMegabyte);
		case InstallStep::Verifying:
			return "Verifying";
		case InstallStep::Extracting:
			return "Extracting";
		case InstallStep::Installed:
		case InstallStep::UpToDate:
		case InstallStep::UpdateAvailable:
		case InstallStep::Uninstalled:
		case InstallStep::Cancelled:
		case InstallStep::Failed:
			break;
		}

		return {};
	}

	std::optional<int> InstallPercent(const InstallProgress& progress)
	{
		const bool sizeKnown = progress.Step == InstallStep::Downloading && progress.TotalBytes.has_value() && *progress.TotalBytes > 0;

		if (!sizeKnown)
		{
			return std::nullopt;
		}

		return static_cast<int>(progress.ReceivedBytes * kPercent / *progress.TotalBytes);
	}
}
