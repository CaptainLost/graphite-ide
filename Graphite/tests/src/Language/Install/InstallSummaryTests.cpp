#include <doctest/doctest.h>

#include "DeferredJobs.h"
#include "InstalledServers.h"
#include "Language/Install/InstallSummary.h"
#include "Language/Install/ServerInstalls.h"
#include "TempDirectory.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace {

	constexpr std::uint64_t kMegabyte = 1024 * 1024;

	Graphite::InstallProgress Downloading()
	{
		return Graphite::InstallProgress{.ServerId = "roslyn",
										 .Step = Graphite::InstallStep::Downloading,
										 .ReceivedBytes = 19 * kMegabyte + kMegabyte / 2,
										 .TotalBytes = 57 * kMegabyte};
	}

	Graphite::InstalledVersion Installed(std::string version, bool hasChecksum)
	{
		return Graphite::InstalledVersion{.Version = std::move(version), .HasChecksum = hasChecksum};
	}

	using Actions = std::vector<Graphite::InstallAction>;
}

TEST_CASE("The download line counts what arrived against the whole in megabytes")
{
	CHECK(Graphite::InstallProgressText(Downloading()) == "Downloading 34% (19 / 57 MB)");
	CHECK(Graphite::InstallPercent(Downloading()) == 34);

	Graphite::InstallProgress unknownSize = Downloading();
	unknownSize.TotalBytes.reset();

	CHECK(Graphite::InstallProgressText(unknownSize) == "Downloading 19 MB");
	CHECK_FALSE(Graphite::InstallPercent(unknownSize).has_value());
}

TEST_CASE("Each step of an install has its words and an outcome has none")
{
	CHECK(Graphite::InstallProgressText({.Step = Graphite::InstallStep::Resolving}) == "Looking up the latest version");
	CHECK(Graphite::InstallProgressText({.Step = Graphite::InstallStep::Verifying}) == "Verifying");
	CHECK(Graphite::InstallProgressText({.Step = Graphite::InstallStep::Extracting}) == "Extracting");
	CHECK(Graphite::InstallProgressText({.Step = Graphite::InstallStep::Installed}).empty());
}

TEST_CASE("A server not installed offers Install")
{
	const Graphite::InstallSummary summary = Graphite::SummariseInstall({});

	CHECK(summary.State == "Not installed");
	CHECK(summary.Actions == Actions{Graphite::InstallAction::Install});
}

TEST_CASE("An installed server offers a check and an uninstall and says when it has no checksum")
{
	const Graphite::InstallSummary verified = Graphite::SummariseInstall({.Installed = Installed("5.12.0", true)});
	const Graphite::InstallSummary unverified = Graphite::SummariseInstall({.Installed = Installed("0.28.0", false)});

	CHECK(verified.State == "Installed 5.12.0");
	CHECK(unverified.State == "Installed 0.28.0, no checksum");
	CHECK(verified.Actions == Actions{Graphite::InstallAction::CheckForUpdates, Graphite::InstallAction::Uninstall});
}

TEST_CASE("A newer release found by a check is offered as an update")
{
	const Graphite::InstallProgress newer{.ServerId = "roslyn", .Step = Graphite::InstallStep::UpdateAvailable, .Version = "5.13.0", .Kind = Graphite::InstallJobKind::CheckForUpdates};
	const Graphite::InstallProgress same{.ServerId = "roslyn", .Step = Graphite::InstallStep::UpToDate, .Version = "5.12.0", .Kind = Graphite::InstallJobKind::CheckForUpdates};

	const Graphite::InstallSummary update = Graphite::SummariseInstall({.Installed = Installed("5.12.0", true), .Latest = newer});
	const Graphite::InstallSummary current = Graphite::SummariseInstall({.Installed = Installed("5.12.0", true), .Latest = same});

	CHECK(update.Note == "5.13.0 available");
	CHECK(update.Actions == Actions{Graphite::InstallAction::Update, Graphite::InstallAction::Uninstall});
	CHECK(current.Note == "Up to date");
	CHECK(current.Actions == Actions{Graphite::InstallAction::CheckForUpdates, Graphite::InstallAction::Uninstall});
}

TEST_CASE("A check on a server never installed reads as not installed")
{
	const Graphite::InstallProgress newer{.ServerId = "roslyn", .Step = Graphite::InstallStep::UpdateAvailable, .Version = "5.13.0", .Kind = Graphite::InstallJobKind::CheckForUpdates};

	const Graphite::InstallSummary summary = Graphite::SummariseInstall({.Latest = newer});

	CHECK(summary.State == "Not installed");
	CHECK(summary.Note.empty());
	CHECK(summary.Actions == Actions{Graphite::InstallAction::Install});
}

TEST_CASE("A running install says its step and offers Cancel until a cancel is asked for")
{
	const Graphite::InstallSummary running = Graphite::SummariseInstall({.Running = Graphite::InstallJobKind::Install, .Latest = Downloading()});
	const Graphite::InstallSummary cancelling =
		Graphite::SummariseInstall({.Running = Graphite::InstallJobKind::Install, .Cancelling = true, .Latest = Downloading()});

	CHECK(running.State == "Downloading 34% (19 / 57 MB)");
	CHECK(running.Actions == Actions{Graphite::InstallAction::Cancel});
	CHECK(cancelling.State == "Cancelling");
	CHECK(cancelling.Actions.empty());
}

TEST_CASE("A job that has not reported yet reads as waiting rather than as the last job's outcome")
{
	const Graphite::InstallProgress earlier{.ServerId = "roslyn", .Step = Graphite::InstallStep::Failed, .Failure = "the network is unreachable"};

	const Graphite::InstallSummary summary = Graphite::SummariseInstall({.Running = Graphite::InstallJobKind::Install, .Latest = earlier});

	CHECK(summary.State == "Waiting to install");
	CHECK(summary.Failure.empty());
	CHECK(summary.Actions == Actions{Graphite::InstallAction::Cancel});
}

TEST_CASE("Uninstalling and checking say so and offer nothing")
{
	CHECK(Graphite::SummariseInstall({.Installed = Installed("5.12.0", true), .Running = Graphite::InstallJobKind::Uninstall}).State == "Uninstalling");
	CHECK(Graphite::SummariseInstall({.Installed = Installed("5.12.0", true), .Running = Graphite::InstallJobKind::CheckForUpdates}).State ==
		  "Checking for updates");
	CHECK(Graphite::SummariseInstall({.Running = Graphite::InstallJobKind::Uninstall}).Actions.empty());
}

TEST_CASE("Without downloads only Uninstall is offered and the tab says why")
{
	const Graphite::InstallSummary installed = Graphite::SummariseInstall({.CanDownload = false, .Installed = Installed("5.12.0", true)});
	const Graphite::InstallSummary missing = Graphite::SummariseInstall({.CanDownload = false});

	CHECK(installed.Actions == Actions{Graphite::InstallAction::Uninstall});
	CHECK(installed.Note == "Graphite cannot download in this session; the log says why.");
	CHECK(missing.Actions.empty());
	CHECK(missing.Note == "Graphite cannot download in this session; the log says why.");
}

TEST_CASE("A failed job's reason is kept beside the state")
{
	const Graphite::InstallProgress failed{.ServerId = "roslyn", .Step = Graphite::InstallStep::Failed, .Failure = "HTTP 404 from api.nuget.org"};

	const Graphite::InstallSummary summary = Graphite::SummariseInstall({.Latest = failed});

	CHECK(summary.State == "Not installed");
	CHECK(summary.Failure == "HTTP 404 from api.nuget.org");
	CHECK(summary.Actions == Actions{Graphite::InstallAction::Install});
}

TEST_CASE("A platform without a package and a read not yet polled in offer nothing")
{
	CHECK(Graphite::SummariseInstall({.AvailableOnPlatform = false}).State == "Not available on this platform");
	CHECK(Graphite::SummariseInstall({.AvailableOnPlatform = false}).Actions.empty());
	CHECK(Graphite::SummariseInstall({.HasReadInstalls = false}).State == "Reading what is installed");
	CHECK(Graphite::SummariseInstall({.HasReadInstalls = false}).Actions.empty());
}

TEST_CASE("A tab's inputs come from the install record and the jobs")
{
	TempDirectory temp;
	InstallCsharpLsIn(temp.GetPath() / "servers");
	DeferredJobs jobs;
	Graphite::ServerInstalls installs{nullptr, temp.GetPath() / "servers", jobs.Submitter()};

	installs.SweepAtStartup();
	jobs.RunAll();
	installs.Poll();
	REQUIRE(installs.Uninstall("csharp-ls"));

	const Graphite::InstallInputs csharpLs = Graphite::InstallInputsFor(installs, *Graphite::FindServer("csharp-ls"));
	const Graphite::InstallInputs roslyn = Graphite::InstallInputsFor(installs, *Graphite::FindServer("roslyn"));

	CHECK(csharpLs.HasReadInstalls);
	CHECK_FALSE(csharpLs.CanDownload);
	CHECK(csharpLs.Installed == Graphite::InstalledVersion{.Version = "0.28.0", .HasChecksum = false});
	CHECK(csharpLs.Running == Graphite::InstallJobKind::Uninstall);
	CHECK(csharpLs.AvailableOnPlatform);
	CHECK_FALSE(roslyn.Installed.has_value());
	CHECK_FALSE(roslyn.Running.has_value());
}
