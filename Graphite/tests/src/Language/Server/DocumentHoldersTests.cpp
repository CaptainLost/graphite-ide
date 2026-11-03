#include <doctest/doctest.h>

#include "Language/Server/DocumentHolders.h"

#include "Core/FileSystem/FileSystem.h"

#include <filesystem>
#include <string>
#include <vector>

namespace {

	using Graphite::DocumentHolder;
	using Graphite::DocumentHolders;
	using Graphite::DocumentRole;
	using Graphite::LanguageId;

	// Writes every call into a log, one line each, so a test reads the conversation with the server in order.
	class RecordingServer final : public Graphite::HeldDocumentPort
	{
	public:
		void EnsureStarted(const std::filesystem::path& path, LanguageId) override { Log.push_back("start " + Name(path)); }

		void Open(const std::filesystem::path& path, LanguageId, std::string text, DocumentRole role) override
		{
			Log.push_back("open " + Name(path) + " " + (role == DocumentRole::Shown ? "shown" : "consulted") + " '" + text + "'");
		}

		void Change(const std::filesystem::path& path, LanguageId, std::string text) override
		{
			Log.push_back("change " + Name(path) + " '" + text + "'");
		}

		void Close(const std::filesystem::path& path, LanguageId) override { Log.push_back("close " + Name(path)); }

		std::vector<std::string> Log;

	private:
		static std::string Name(const std::filesystem::path& path) { return Core::ToUtf8(path.filename()); }
	};

	const std::filesystem::path kFile = "D:\\Work\\main.cpp";
}

TEST_CASE("The first holder opens the document and the last to let go closes it")
{
	RecordingServer server;
	DocumentHolders holders{server};

	holders.Hold(DocumentHolder::Analysis, kFile, "disk");
	holders.Hold(DocumentHolder::Preview, kFile, "disk");
	holders.Release(DocumentHolder::Analysis, kFile);

	CHECK(holders.IsHeld(kFile));

	holders.Release(DocumentHolder::Preview, kFile);

	const std::vector<std::string> expected{"start main.cpp", "open main.cpp consulted 'disk'", "close main.cpp"};
	CHECK(server.Log == expected);
	CHECK_FALSE(holders.IsHeld(kFile));
}

TEST_CASE("A tab taking a previewed document gives the server the tab's text as a shown document")
{
	RecordingServer server;
	DocumentHolders holders{server};

	holders.Hold(DocumentHolder::Preview, kFile, "disk");
	holders.Hold(DocumentHolder::Tab, kFile, "edited");

	const std::vector<std::string> expected{"start main.cpp", "open main.cpp consulted 'disk'", "close main.cpp",
											"open main.cpp shown 'edited'"};
	CHECK(server.Log == expected);
}

TEST_CASE("A preview or the analysis joining a document the tab holds sends nothing")
{
	RecordingServer server;
	DocumentHolders holders{server};

	holders.Hold(DocumentHolder::Tab, kFile, "edited");
	server.Log.clear();

	holders.Hold(DocumentHolder::Preview, kFile, "disk");
	holders.Hold(DocumentHolder::Analysis, kFile, "disk");
	holders.Release(DocumentHolder::Analysis, kFile);

	CHECK(server.Log.empty());
}

TEST_CASE("A tab letting go of a document a preview still shows hands it back with the preview's text")
{
	RecordingServer server;
	DocumentHolders holders{server};

	holders.Hold(DocumentHolder::Tab, kFile, "edited");
	holders.Hold(DocumentHolder::Preview, kFile, "disk");
	server.Log.clear();

	holders.Release(DocumentHolder::Tab, kFile);

	const std::vector<std::string> expected{"close main.cpp", "open main.cpp consulted 'disk'"};
	CHECK(server.Log == expected);
	CHECK(holders.IsHeldBy(DocumentHolder::Preview, kFile));
}

TEST_CASE("A preview holding alone sends its new text when it holds again")
{
	RecordingServer server;
	DocumentHolders holders{server};

	holders.Hold(DocumentHolder::Preview, kFile, "disk");
	server.Log.clear();

	holders.Hold(DocumentHolder::Preview, kFile, "disk, read again");

	const std::vector<std::string> expected{"change main.cpp 'disk, read again'"};
	CHECK(server.Log == expected);
}

TEST_CASE("A preview holding again under the tab sends nothing: the tab's text stands")
{
	RecordingServer server;
	DocumentHolders holders{server};

	holders.Hold(DocumentHolder::Tab, kFile, "edited");
	holders.Hold(DocumentHolder::Preview, kFile, "disk");
	server.Log.clear();

	holders.Hold(DocumentHolder::Preview, kFile, "disk, read again");

	CHECK(server.Log.empty());
}

TEST_CASE("A change reaches the server only while somebody holds the document")
{
	RecordingServer server;
	DocumentHolders holders{server};

	holders.Change(kFile, "nobody holds this");
	CHECK(server.Log.empty());

	holders.Hold(DocumentHolder::Tab, kFile, "edited");
	server.Log.clear();

	holders.Change(kFile, "edited again");

	const std::vector<std::string> expected{"change main.cpp 'edited again'"};
	CHECK(server.Log == expected);
}

TEST_CASE("Letting go of a document one does not hold changes nothing")
{
	RecordingServer server;
	DocumentHolders holders{server};

	holders.Hold(DocumentHolder::Tab, kFile, "edited");
	server.Log.clear();

	holders.Release(DocumentHolder::Preview, kFile);
	holders.Release(DocumentHolder::Analysis, "D:\\Work\\other.cpp");

	CHECK(server.Log.empty());
	CHECK(holders.IsHeldBy(DocumentHolder::Tab, kFile));
}

TEST_CASE("Who holds a document is asked of one place")
{
	RecordingServer server;
	DocumentHolders holders{server};

	holders.Hold(DocumentHolder::Analysis, kFile, "disk");

	CHECK(holders.IsHeldBy(DocumentHolder::Analysis, kFile));
	CHECK_FALSE(holders.IsHeldBy(DocumentHolder::Tab, kFile));
	CHECK_FALSE(holders.IsHeldByAnyBut(DocumentHolder::Analysis, kFile));

	holders.Hold(DocumentHolder::Preview, kFile, "disk");

	CHECK(holders.IsHeldByAnyBut(DocumentHolder::Analysis, kFile));
}

TEST_CASE("A document passed from a preview to a tab and back is closed only once nobody holds it")
{
	RecordingServer server;
	DocumentHolders holders{server};

	holders.Hold(DocumentHolder::Preview, kFile, "disk");
	holders.Hold(DocumentHolder::Tab, kFile, "disk");
	holders.Change(kFile, "edited");
	holders.Release(DocumentHolder::Tab, kFile);
	holders.Release(DocumentHolder::Preview, kFile);

	const std::vector<std::string> expected{"start main.cpp",
											"open main.cpp consulted 'disk'",
											"close main.cpp",
											"open main.cpp shown 'disk'",
											"change main.cpp 'edited'",
											"close main.cpp",
											"open main.cpp consulted 'disk'",
											"close main.cpp"};
	CHECK(server.Log == expected);
	CHECK_FALSE(holders.IsHeld(kFile));
}
