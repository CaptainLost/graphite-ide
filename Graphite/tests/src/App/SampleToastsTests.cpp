#include <doctest/doctest.h>

#include "App/SampleToasts.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>

namespace {
	const std::filesystem::path kOpenFolder = "D:/Repos/Game";

	Graphite::ToastEvent SampleOf(Graphite::SampleToastKind sampleKind)
	{
		return Graphite::SampleToast(sampleKind, kOpenFolder);
	}

	std::size_t LineCountOf(const std::string& text)
	{
		const std::size_t lineBreaks = static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
		const bool endsWithLineBreak = !text.empty() && text.back() == '\n';
		return endsWithLineBreak ? lineBreaks : lineBreaks + 1;
	}
}

TEST_CASE("Every sample toast has a command of its own, labelled for the menu and for lists outside it")
{
	std::set<std::string_view> commandIds;
	for (const Graphite::SampleToastCommand& sample : Graphite::kSampleToastCommands)
	{
		CHECK_FALSE(sample.Label.empty());
		CHECK_FALSE(sample.ListLabel.empty());
		commandIds.insert(sample.CommandId);
	}

	CHECK(commandIds.size() == std::size(Graphite::kSampleToastCommands));
}

TEST_CASE("The sample toasts show every kind, each about the application but the folder one")
{
	CHECK(SampleOf(Graphite::SampleToastKind::Success).Kind == Core::ToastKind::Success);
	CHECK(SampleOf(Graphite::SampleToastKind::Warning).Kind == Core::ToastKind::Warning);
	CHECK(SampleOf(Graphite::SampleToastKind::Error).Kind == Core::ToastKind::Error);
	CHECK(SampleOf(Graphite::SampleToastKind::LongError).Kind == Core::ToastKind::Error);
	CHECK(SampleOf(Graphite::SampleToastKind::FolderError).Kind == Core::ToastKind::Error);

	CHECK(SampleOf(Graphite::SampleToastKind::Success).Scope == Graphite::ToastScope::Application);
	CHECK(SampleOf(Graphite::SampleToastKind::Warning).Scope == Graphite::ToastScope::Application);
	CHECK(SampleOf(Graphite::SampleToastKind::Error).Scope == Graphite::ToastScope::Application);
	CHECK(SampleOf(Graphite::SampleToastKind::LongError).Scope == Graphite::ToastScope::Application);
	CHECK(SampleOf(Graphite::SampleToastKind::FolderError).Scope == Graphite::ToastScope::Folder);
}

TEST_CASE("The long sample error runs to twelve lines, long enough to scroll in its toast and fold in the history")
{
	CHECK(LineCountOf(SampleOf(Graphite::SampleToastKind::LongError).Message) == 12);
}

TEST_CASE("The folder sample error shows its path relative to the open folder and copies it whole")
{
	const Graphite::ToastEvent folderError = SampleOf(Graphite::SampleToastKind::FolderError);
	const std::string openFolderText = Core::ToUtf8(kOpenFolder);

	CHECK(folderError.Message.find(openFolderText) == std::string::npos);
	CHECK(folderError.CopyText.find(openFolderText) != std::string::npos);
}

TEST_CASE("Every sample toast has a title and a message")
{
	for (const Graphite::SampleToastCommand& sample : Graphite::kSampleToastCommands)
	{
		const Graphite::ToastEvent event = SampleOf(sample.Kind);
		CAPTURE(sample.CommandId);
		CHECK_FALSE(event.Title.empty());
		CHECK_FALSE(event.Message.empty());
	}
}
