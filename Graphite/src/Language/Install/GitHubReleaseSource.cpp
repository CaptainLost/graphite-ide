#include "Language/Install/GitHubReleaseSource.h"

#include "Language/Install/InstallJson.h"
#include "Language/Install/PackageNames.h"

#include <format>

namespace Graphite {

	namespace {

		constexpr std::string_view kGitHubApi = "https://api.github.com/repos/";
		constexpr int kStatusOk = 200;
		constexpr int kStatusNotFound = 404;

		bool IsDigit(char character)
		{
			return character >= '0' && character <= '9';
		}

		std::string VersionFromTag(std::string_view tag)
		{
			const bool startsWithV = tag.size() > 1 && (tag.front() == 'v' || tag.front() == 'V') && IsDigit(tag[1]);
			return std::string{startsWithV ? tag.substr(1) : tag};
		}

		Core::Error RefusalFrom(const Core::HttpResponse& response, std::string_view url, std::string_view repository)
		{
			if (response.Status == Core::kHttpStatusProxyAuthenticationRequired)
			{
				return Core::ProxyCredentialsError(url);
			}

			const std::optional<std::string> message = TextAt(ParseJsonText(response.Body), "message");
			const Core::ErrorCode code = response.Status == kStatusNotFound ? Core::ErrorCode::NotFound : Core::ErrorCode::IoFailure;

			if (!message)
			{
				return Core::Error{.Code = code, .Message = std::format("GitHub answered {} for {}", response.Status, repository)};
			}

			return Core::Error{.Code = code,
							   .Message = std::format("GitHub answered {} for {}: {}", response.Status, repository, *message)};
		}

		const nlohmann::json* FindAssetNamed(const nlohmann::json& release, std::string_view name)
		{
			const nlohmann::json* assets = ArrayAt(release, "assets");

			if (assets == nullptr)
			{
				return nullptr;
			}

			for (const nlohmann::json& asset : *assets)
			{
				if (TextAt(asset, "name") == name)
				{
					return &asset;
				}
			}

			return nullptr;
		}

		// A digest that is there but cannot be read fails: a checksum Graphite cannot check is not one that passed.
		Core::Result<std::optional<Checksum>> DigestOf(const nlohmann::json& asset, std::string_view assetName)
		{
			const nlohmann::json::const_iterator digest = asset.find("digest");

			if (digest == asset.end() || digest->is_null())
			{
				return std::optional<Checksum>{};
			}

			std::optional<Checksum> checksum = digest->is_string() ? ParseChecksum(digest->get<std::string>()) : std::nullopt;

			if (!checksum)
			{
				return Core::Error{.Code = Core::ErrorCode::InvalidArgument,
								   .Message = std::format("GitHub's digest for {} cannot be read: {}", assetName,
														  digest->dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))};
			}

			return checksum;
		}
	}

	Core::Result<ResolvedPackage> ResolveGitHubRelease(Core::HttpClient& http, const GitHubReleasePackage& package,
													   Core::PlatformTarget platform)
	{
		const GitHubAsset* asset = FindAssetFor(package, platform);

		if (asset == nullptr)
		{
			return Core::Error{.Code = Core::ErrorCode::Unsupported,
							   .Message = std::format("{} publishes no build for this platform", package.Repository)};
		}

		const std::string latestUrl = std::format("{}{}/releases/latest", kGitHubApi, package.Repository);
		const Core::Result<Core::HttpResponse> response =
			http.Fetch({.Url = latestUrl, .Headers = {{.Name = "Accept", .Value = "application/vnd.github+json"}}});

		if (!response)
		{
			return response.GetError();
		}

		if (response.GetValue().Status != kStatusOk)
		{
			return RefusalFrom(response.GetValue(), latestUrl, package.Repository);
		}

		const nlohmann::json release = ParseJsonText(response.GetValue().Body);
		const std::optional<std::string> tag = TextAt(release, "tag_name");

		if (!tag)
		{
			return Core::Error{.Code = Core::ErrorCode::IoFailure,
							   .Message = std::format("the latest release of {} has no tag", package.Repository)};
		}

		const std::string version = VersionFromTag(*tag);

		if (!IsSafeDirectoryName(version))
		{
			return Core::Error{.Code = Core::ErrorCode::InvalidArgument,
							   .Message = std::format("the latest release of {} is tagged '{}', which cannot name a folder",
													  package.Repository, *tag)};
		}

		const TemplateValues values{.Version = version, .Platform = platform};
		const Core::Result<std::string> assetName = ExpandPackageTemplate(asset->AssetTemplate, values);
		const Core::Result<std::string> executable = ExpandPackageTemplate(asset->ExecutableTemplate, values);

		if (!assetName)
		{
			return assetName.GetError();
		}

		if (!executable)
		{
			return executable.GetError();
		}

		const nlohmann::json* found = FindAssetNamed(release, assetName.GetValue());

		if (found == nullptr)
		{
			return Core::Error{.Code = Core::ErrorCode::NotFound,
							   .Message = std::format("release {} of {} has no file named {}", *tag, package.Repository,
													  assetName.GetValue())};
		}

		const std::optional<std::string> downloadUrl = TextAt(*found, "browser_download_url");

		if (!downloadUrl || !downloadUrl->starts_with("https://"))
		{
			return Core::Error{.Code = Core::ErrorCode::IoFailure,
							   .Message = std::format("{} in release {} of {} has no https download address",
													  assetName.GetValue(), *tag, package.Repository)};
		}

		Core::Result<std::optional<Checksum>> checksum = DigestOf(*found, assetName.GetValue());

		if (!checksum)
		{
			return checksum.GetError();
		}

		return ResolvedPackage{.Version = version,
							   .DownloadUrl = *downloadUrl,
							   .FileName = assetName.GetValue(),
							   .ExpectedChecksum = std::move(checksum).GetValue(),
							   .Archive = asset->Archive,
							   .ExecutableInArchive = executable.GetValue()};
	}
}
