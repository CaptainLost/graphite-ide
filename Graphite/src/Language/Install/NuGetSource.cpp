#include "Language/Install/NuGetSource.h"

#include "Language/Install/InstallJson.h"
#include "Language/Install/NuGetVersion.h"
#include "Language/Install/PackageNames.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/ByteText.h"

#include <format>
#include <optional>
#include <vector>

namespace Graphite {

	namespace {

		constexpr std::string_view kRegistrationBase = "https://api.nuget.org/v3/registration5-gz-semver2/";
		constexpr std::string_view kPackageBase = "https://api.nuget.org/v3-flatcontainer/";
		constexpr int kStatusOk = 200;
		constexpr int kStatusNotFound = 404;
		constexpr std::size_t kSha512Bytes = 64;

		struct ListedVersion
		{
			NuGetVersion Version;
			std::string Text;
			std::string CatalogUrl;
		};

		Core::Result<nlohmann::json> FetchJson(Core::HttpClient& http, const std::string& url, std::string_view packageId)
		{
			const Core::Result<Core::HttpResponse> response = http.Fetch({.Url = url});

			if (!response)
			{
				return response.GetError();
			}

			if (response.GetValue().Status != kStatusOk)
			{
				const Core::ErrorCode code =
					response.GetValue().Status == kStatusNotFound ? Core::ErrorCode::NotFound : Core::ErrorCode::IoFailure;
				return Core::Error{.Code = code,
								   .Message = std::format("NuGet answered {} for {} at '{}'", response.GetValue().Status,
														  packageId, url)};
			}

			nlohmann::json parsed = ParseJsonText(response.GetValue().Body);

			if (!parsed.is_object())
			{
				return Core::Error{.Code = Core::ErrorCode::IoFailure,
								   .Message = std::format("NuGet's answer at '{}' for {} is not a JSON object", url, packageId)};
			}

			return parsed;
		}

		std::optional<ListedVersion> ListedVersionIn(const nlohmann::json& leaf)
		{
			const nlohmann::json* entry = ObjectAt(leaf, "catalogEntry");

			if (entry == nullptr)
			{
				return std::nullopt;
			}

			const std::optional<std::string> versionText = TextAt(*entry, "version");
			const std::optional<std::string> catalogUrl = TextAt(*entry, "@id");
			const bool isListed = FlagAt(*entry, "listed").value_or(true);

			if (!versionText || !catalogUrl || !isListed)
			{
				return std::nullopt;
			}

			const std::optional<NuGetVersion> version = NuGetVersion::Parse(*versionText);

			if (!version)
			{
				return std::nullopt;
			}

			return ListedVersion{.Version = *version, .Text = *versionText, .CatalogUrl = *catalogUrl};
		}

		Core::Result<std::vector<ListedVersion>> ListedVersionsOn(Core::HttpClient& http, const nlohmann::json& page,
																  std::string_view packageId)
		{
			nlohmann::json fetchedPage;
			const nlohmann::json* leaves = ArrayAt(page, "items");

			if (leaves == nullptr)
			{
				const std::optional<std::string> pageUrl = TextAt(page, "@id");

				if (!pageUrl)
				{
					return Core::Error{.Code = Core::ErrorCode::IoFailure,
									   .Message = std::format("NuGet's index of {} has a page with no address", packageId)};
				}

				Core::Result<nlohmann::json> fetched = FetchJson(http, *pageUrl, packageId);

				if (!fetched)
				{
					return fetched.GetError();
				}

				fetchedPage = std::move(fetched).GetValue();
				leaves = ArrayAt(fetchedPage, "items");

				if (leaves == nullptr)
				{
					return Core::Error{.Code = Core::ErrorCode::IoFailure,
									   .Message = std::format("NuGet's page '{}' of {} lists no versions", *pageUrl, packageId)};
				}
			}

			std::vector<ListedVersion> versions;

			for (const nlohmann::json& leaf : *leaves)
			{
				if (std::optional<ListedVersion> listed = ListedVersionIn(leaf))
				{
					versions.push_back(std::move(*listed));
				}
			}

			return versions;
		}

		const ListedVersion* HighestAllowed(const std::vector<ListedVersion>& versions, bool allowPrerelease)
		{
			const ListedVersion* highest = nullptr;

			for (const ListedVersion& candidate : versions)
			{
				const bool isAllowed = allowPrerelease || !candidate.Version.IsPrerelease();
				const bool isHigher = highest == nullptr || highest->Version < candidate.Version;

				if (isAllowed && isHigher)
				{
					highest = &candidate;
				}
			}

			return highest;
		}

		// A hash that is there but cannot be read fails: a checksum Graphite cannot check is not one that passed.
		Core::Result<std::optional<Checksum>> PackageHashIn(const nlohmann::json& catalogEntry, std::string_view packageId,
															std::string_view version)
		{
			const nlohmann::json::const_iterator hashMember = catalogEntry.find("packageHash");

			if (hashMember == catalogEntry.end() || hashMember->is_null())
			{
				return std::optional<Checksum>{};
			}

			const std::optional<std::string> hash = TextAt(catalogEntry, "packageHash");
			const std::optional<std::string> algorithm = TextAt(catalogEntry, "packageHashAlgorithm");
			std::optional<std::vector<std::uint8_t>> digest =
				hash ? Core::BytesFromBase64(*hash) : std::optional<std::vector<std::uint8_t>>{};
			const bool isSha512 = algorithm.has_value() && Core::EqualsIgnoringCase(*algorithm, "SHA512");

			if (!isSha512 || !digest || digest->size() != kSha512Bytes)
			{
				return Core::Error{.Code = Core::ErrorCode::InvalidArgument,
								   .Message = std::format("NuGet's checksum for {} {} cannot be read", packageId, version)};
			}

			return std::optional<Checksum>{Checksum{.Algorithm = Core::HashAlgorithm::Sha512, .Digest = std::move(*digest)}};
		}
	}

	Core::Result<ResolvedPackage> ResolveNuGetTool(Core::HttpClient& http, const NuGetToolPackage& package,
												   Core::PlatformTarget platform)
	{
		const Core::Result<std::string> expandedId =
			ExpandPackageTemplate(package.PackageIdTemplate, {.Version = "", .Platform = platform});

		if (!expandedId)
		{
			return expandedId.GetError();
		}

		const std::string packageId = Core::Lowered(expandedId.GetValue());
		const Core::Result<nlohmann::json> index =
			FetchJson(http, std::format("{}{}/index.json", kRegistrationBase, packageId), packageId);

		if (!index)
		{
			return index.GetError();
		}

		std::vector<ListedVersion> versions;

		if (const nlohmann::json* pages = ArrayAt(index.GetValue(), "items"))
		{
			for (const nlohmann::json& page : *pages)
			{
				Core::Result<std::vector<ListedVersion>> onPage = ListedVersionsOn(http, page, packageId);

				if (!onPage)
				{
					return onPage.GetError();
				}

				for (ListedVersion& listed : onPage.GetValue())
				{
					versions.push_back(std::move(listed));
				}
			}
		}

		const ListedVersion* chosen = HighestAllowed(versions, package.AllowPrerelease);

		if (chosen == nullptr)
		{
			return Core::Error{.Code = Core::ErrorCode::NotFound,
							   .Message = std::format("NuGet lists no {}version of {}",
													  package.AllowPrerelease ? "" : "stable ", packageId)};
		}

		const std::string_view withoutMetadata = std::string_view{chosen->Text}.substr(0, chosen->Text.find('+'));
		const std::string version = Core::Lowered(withoutMetadata);

		if (!IsSafeDirectoryName(version))
		{
			return Core::Error{.Code = Core::ErrorCode::InvalidArgument,
							   .Message = std::format("NuGet's version '{}' of {} cannot name a folder", chosen->Text, packageId)};
		}

		const Core::Result<nlohmann::json> catalogEntry = FetchJson(http, chosen->CatalogUrl, packageId);

		if (!catalogEntry)
		{
			return catalogEntry.GetError();
		}

		Core::Result<std::optional<Checksum>> checksum = PackageHashIn(catalogEntry.GetValue(), packageId, version);

		if (!checksum)
		{
			return checksum.GetError();
		}

		const std::string fileName = std::format("{}.{}.nupkg", packageId, version);

		return ResolvedPackage{.Version = version,
							   .DownloadUrl = std::format("{}{}/{}/{}", kPackageBase, packageId, version, fileName),
							   .FileName = fileName,
							   .ExpectedChecksum = std::move(checksum).GetValue(),
							   .Archive = ArchiveKind::Zip,
							   .ExecutableInArchive = {}};
	}
}
