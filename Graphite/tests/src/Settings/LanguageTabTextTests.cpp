#include <doctest/doctest.h>

#include "Language/Server/ServerCatalog.h"
#include "Settings/LanguageTabText.h"

#include <ostream>
#include <string>
#include <vector>

TEST_CASE("The first server a language lists is marked recommended")
{
	CHECK(Graphite::ServerOptionLabel(*Graphite::FindServer("roslyn"), true) == "Roslyn (recommended)");
	CHECK(Graphite::ServerOptionLabel(*Graphite::FindServer("csharp-ls"), false) == "csharp-ls");
}

TEST_CASE("The runtime line names the version found or says none was")
{
	const Graphite::ServerRuntime dotnet10{.Kind = Graphite::RuntimeKind::Dotnet, .MinimumMajor = 10};
	const std::vector<Graphite::DotnetInstallation> withTen{
		Graphite::DotnetInstallation{.Executable = "C:/dotnet/dotnet.exe", .RuntimeVersions = {"10.0.3", "8.0.11"}}};
	const std::vector<Graphite::DotnetInstallation> withEight{
		Graphite::DotnetInstallation{.Executable = "C:/dotnet/dotnet.exe", .RuntimeVersions = {"8.0.11"}}};

	CHECK(Graphite::RuntimeLine(dotnet10, withTen) == ".NET 10 runtime \xE2\x80\x94 found 10.0.3");
	CHECK(Graphite::RuntimeFound(dotnet10, withTen));
	CHECK(Graphite::RuntimeLine(dotnet10, withEight) == ".NET 10 runtime \xE2\x80\x94 not found");
	CHECK_FALSE(Graphite::RuntimeFound(dotnet10, withEight));
	CHECK(Graphite::RuntimeLine(Graphite::ServerRuntime{}, withTen).empty());
}
