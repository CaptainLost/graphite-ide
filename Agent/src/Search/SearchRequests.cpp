#include "corepch.h"

#include "Search/SearchRequests.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"
#include "Core/Search/Matcher.h"
#include "Core/Text/FileProbe.h"

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace Agent {

	using nlohmann::json;

	namespace {

		// The host shows no more; a search past it says it stopped there.
		constexpr std::size_t kMostHits = 20000;

		// Every hit carries its whole line, so a long minified line found many times would outgrow a
		// reply (256 MiB): the search stops, and says so, once its lines pass this.
		constexpr std::size_t kMostHitBytes = 64 * 1024 * 1024;

		// What the walk leaves out: .git always, and what git ignores when asked.
		class Walk
		{
		public:
			Walk(std::filesystem::path root, bool respectGitIgnore) : m_Root(std::move(root))
			{
				if (respectGitIgnore)
				{
					Core::Result<Core::Repository> repository = Core::Repository::Open(m_Root);

					if (repository)
					{
						m_Repository.emplace(std::move(repository.GetValue()));
					}
				}
			}

			// Calls onFile with each file's absolute path and its path relative to the root, folders in no order.
			void Files(const std::function<bool(const std::filesystem::path&, const std::string&)>& onFile) const
			{
				std::vector<std::filesystem::path> pending{m_Root};

				while (!pending.empty())
				{
					const std::filesystem::path folder = std::move(pending.back());
					pending.pop_back();
					std::error_code error;

					for (std::filesystem::directory_iterator entries{folder, error}, end; !error && entries != end; entries.increment(error))
					{
						const std::filesystem::directory_entry& entry = *entries;
						std::error_code kindError;

						// Symbolic links to folders are not followed: a link to an ancestor would loop.
						if (entry.is_symlink(kindError))
						{
							if (!entry.is_regular_file(kindError))
							{
								continue;
							}
						}

						const std::filesystem::path& path = entry.path();

						if (entry.is_directory(kindError))
						{
							if (path.filename() == ".git" || IsIgnored(path))
							{
								continue;
							}

							pending.push_back(path);
							continue;
						}

						if (!entry.is_regular_file(kindError) || IsIgnored(path))
						{
							continue;
						}

						if (!onFile(path, path.lexically_relative(m_Root).generic_string())) // not a path conversion: Linux paths are their bytes
						{
							return;
						}
					}
				}
			}

		private:
			bool IsIgnored(const std::filesystem::path& path) const { return m_Repository && m_Repository->IsIgnored(path); }

			std::filesystem::path m_Root;
			std::optional<Core::Repository> m_Repository;
		};

		Core::Result<std::filesystem::path> RootOf(const json& arguments)
		{
			const auto root = arguments.find("root");

			if (root == arguments.end() || !root->is_string() || root->get<std::string>().empty() || root->get<std::string>().front() != '/')
			{
				return Core::Error{Core::ErrorCode::InvalidArgument, "the request has no absolute \"root\""};
			}

			return std::filesystem::path{root->get<std::string>()};
		}

		Core::Result<GitAnswer> ListFiles(const Core::AgentRequest& request)
		{
			const Core::Result<std::filesystem::path> root = RootOf(request.Arguments);

			if (!root)
			{
				return root.GetError();
			}

			GitAnswer answer;
			std::size_t count = 0;
			const Walk walk{root.GetValue(), request.Arguments.value("respectGitIgnore", true)};

			walk.Files([&](const std::filesystem::path&, const std::string& relative) {
				answer.Payload += relative;
				answer.Payload.push_back('\0');
				++count;
				return true;
			});

			answer.Result = {{"count", count}};
			return answer;
		}

		Core::Result<GitAnswer> SearchText(const Core::AgentRequest& request)
		{
			const Core::Result<std::filesystem::path> root = RootOf(request.Arguments);

			if (!root)
			{
				return root.GetError();
			}

			const json asked = request.Arguments.value("query", json::object());
			const Core::SearchQuery query{.Needle = asked.value("needle", std::string{}),
										  .Mask = asked.value("mask", std::string{}),
										  .MatchCase = asked.value("matchCase", false),
										  .WholeWord = asked.value("wholeWord", false)};
			std::set<std::string> skipped;

			for (const json& path : request.Arguments.value("skip", json::array()))
			{
				if (path.is_string())
				{
					skipped.insert(path.get<std::string>());
				}
			}

			std::vector<Core::SearchHit> hits;
			std::size_t hitBytes = 0;
			std::size_t scanned = 0;
			std::size_t notText = 0;
			std::size_t unreadable = 0;
			bool capped = false;
			const Walk walk{root.GetValue(), request.Arguments.value("respectGitIgnore", true)};

			walk.Files([&](const std::filesystem::path& path, const std::string& relative) {
				if (skipped.contains(relative) || !Core::MaskAccepts(query.Mask, path.filename().string())) // not a path conversion
				{
					return true;
				}

				const Core::DiskText read = Core::ReadDiskText(path);

				switch (read.Outcome)
				{
				case Core::DiskTextOutcome::Read:
				{
					++scanned;
					const std::size_t before = hits.size();
					Core::FindInText(query, read.Text, relative, hits);

					for (std::size_t index = before; index < hits.size(); ++index)
					{
						hitBytes += hits[index].Text.size();

						if (hitBytes > kMostHitBytes)
						{
							hits.resize(index);
							capped = true;
							return false;
						}
					}

					break;
				}
				case Core::DiskTextOutcome::Binary:
				case Core::DiskTextOutcome::TooLarge:
					++notText;
					break;
				case Core::DiskTextOutcome::Unreadable:
				case Core::DiskTextOutcome::Undecodable:
					++unreadable;
					break;
				}

				if (hits.size() >= kMostHits)
				{
					hits.resize(kMostHits);
					capped = true;
					return false;
				}

				return true;
			});

			json listed = json::array();

			for (const Core::SearchHit& hit : hits)
			{
				listed.push_back({{"file", hit.File.generic_string()}, {"line", hit.Line}, {"text", hit.Text}, {"from", hit.From}, {"to", hit.To}}); // not a path conversion
			}

			return GitAnswer{.Result = {{"scanned", scanned}, {"skipped", notText}, {"unreadable", unreadable}, {"capped", capped}},
							 .Payload = listed.dump(-1, ' ', false, json::error_handler_t::replace)};
		}
	}

	GitHandlers SearchHandlers()
	{
		GitHandlers handlers;
		handlers["files.list"] = ListFiles;
		handlers["search.text"] = SearchText;
		return handlers;
	}
}
