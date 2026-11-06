#include "Git/Lines/GraphFile.h"

#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		constexpr std::string_view kMagic = "GLG2";
		constexpr std::uint8_t kNoFlags = 0;
		constexpr std::uint8_t kParentsCutFlag = 1;
		constexpr std::uint8_t kCommittedByOtherFlag = 2;

		std::uint8_t FlagsOf(bool parentsCut, bool committedByAuthor)
		{
			const std::uint8_t cutFlag = parentsCut ? kParentsCutFlag : kNoFlags;
			const std::uint8_t committerFlag = committedByAuthor ? kNoFlags : kCommittedByOtherFlag;
			return static_cast<std::uint8_t>(cutFlag | committerFlag);
		}

		void PutUnsigned(std::string& out, std::uint64_t value, int byteCount)
		{
			for (int index = 0; index < byteCount; ++index)
			{
				out.push_back(static_cast<char>((value >> (8 * index)) & 0xFF));
			}
		}

		// Reads from a buffer without ever stepping past its end; every read says whether it fitted.
		class Reader
		{
		public:
			explicit Reader(std::string_view bytes) : m_bytes(bytes) {}

			bool ReadUnsigned(std::uint64_t& value, int byteCount)
			{
				if (m_bytes.size() - m_position < static_cast<std::size_t>(byteCount))
				{
					return false;
				}

				value = 0;

				for (int index = 0; index < byteCount; ++index)
				{
					value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(m_bytes[m_position + index])) << (8 * index);
				}

				m_position += static_cast<std::size_t>(byteCount);
				return true;
			}

			bool ReadText(std::string& text, std::size_t length)
			{
				if (m_bytes.size() - m_position < length)
				{
					return false;
				}

				text.assign(m_bytes.substr(m_position, length));
				m_position += length;
				return true;
			}

			bool AtEnd() const { return m_position == m_bytes.size(); }

		private:
			std::string_view m_bytes;
			std::size_t m_position = 0;
		};

		enum class Visit : std::uint8_t
		{
			NotSeen,
			Open,
			Done
		};

		// Every commit after all of its parents, so that in the file a parent's index is always below its child's
		// and a file that holds a cycle can be told from one that does not. Depth first on an explicit stack: a
		// first-parent chain can be hundreds of thousands long.
		std::vector<Core::GraphCommit> ParentsFirst(std::vector<Core::GraphCommit> commits)
		{
			std::unordered_map<Core::Oid, std::size_t> positionById;
			positionById.reserve(commits.size());

			for (std::size_t position = 0; position < commits.size(); ++position)
			{
				positionById.emplace(commits[position].Id, position);
			}

			std::vector<Visit> visits(commits.size(), Visit::NotSeen);
			std::vector<std::size_t> order;
			order.reserve(commits.size());
			std::vector<std::size_t> pending;

			for (std::size_t start = 0; start < commits.size(); ++start)
			{
				if (visits[start] == Visit::Done)
				{
					continue;
				}

				pending.push_back(start);

				while (!pending.empty())
				{
					const std::size_t position = pending.back();

					if (visits[position] == Visit::NotSeen)
					{
						visits[position] = Visit::Open;

						for (const Core::Oid& parent : commits[position].Parents)
						{
							const auto found = positionById.find(parent);

							if (found != positionById.end() && visits[found->second] == Visit::NotSeen)
							{
								pending.push_back(found->second);
							}
						}

						continue;
					}

					pending.pop_back();

					if (visits[position] == Visit::Open)
					{
						visits[position] = Visit::Done;
						order.push_back(position);
					}
				}
			}

			std::vector<Core::GraphCommit> ordered;
			ordered.reserve(commits.size());

			for (const std::size_t position : order)
			{
				ordered.push_back(std::move(commits[position]));
			}

			return ordered;
		}
	}

	Core::Result<void> SaveGraph(const std::filesystem::path& file, const Core::AncestryGraph& graph)
	{
		const std::vector<Core::GraphCommit> commits = ParentsFirst(graph.ToCommits());

		std::unordered_map<Core::Oid, std::uint32_t> indexById;
		indexById.reserve(commits.size());

		for (std::size_t index = 0; index < commits.size(); ++index)
		{
			indexById.emplace(commits[index].Id, static_cast<std::uint32_t>(index));
		}

		std::unordered_map<std::string, std::uint32_t> authorIndex;
		std::vector<const std::string*> authors;

		for (const Core::GraphCommit& commit : commits)
		{
			if (authorIndex.emplace(commit.AuthorKey, static_cast<std::uint32_t>(authors.size())).second)
			{
				authors.push_back(&commit.AuthorKey);
			}
		}

		std::string bytes(kMagic);
		PutUnsigned(bytes, authors.size(), 4);

		for (const std::string* author : authors)
		{
			PutUnsigned(bytes, author->size(), 4);
			bytes += *author;
		}

		PutUnsigned(bytes, commits.size(), 4);

		for (const Core::GraphCommit& commit : commits)
		{
			std::vector<std::uint32_t> parents;
			bool cut = commit.ParentsCut;

			for (const Core::Oid& parent : commit.Parents)
			{
				const auto found = indexById.find(parent);

				if (found == indexById.end() || parents.size() == std::numeric_limits<std::uint8_t>::max())
				{
					cut = true;
					continue;
				}

				parents.push_back(found->second);
			}

			bytes.append(reinterpret_cast<const char*>(commit.Id.Bytes.data()), Core::Oid::kByteCount);
			PutUnsigned(bytes, static_cast<std::uint64_t>(commit.Time), 8);
			PutUnsigned(bytes, authorIndex.at(commit.AuthorKey), 4);
			PutUnsigned(bytes, FlagsOf(cut, commit.CommittedByAuthor), 1);
			PutUnsigned(bytes, parents.size(), 1);

			for (const std::uint32_t parent : parents)
			{
				PutUnsigned(bytes, parent, 4);
			}
		}

		std::error_code error;
		std::filesystem::create_directories(file.parent_path(), error);

		return Core::WriteFileAtomically(file, bytes);
	}

	std::optional<Core::AncestryGraph> LoadGraph(const std::filesystem::path& file)
	{
		const Core::Result<std::string> bytes = Core::ReadFileBytes(file);

		if (!bytes)
		{
			return std::nullopt;
		}

		Reader reader(bytes.GetValue());
		std::string magic;

		if (!reader.ReadText(magic, kMagic.size()) || magic != kMagic)
		{
			return std::nullopt;
		}

		std::uint64_t authorCount = 0;

		if (!reader.ReadUnsigned(authorCount, 4))
		{
			return std::nullopt;
		}

		std::vector<std::string> authors;

		for (std::uint64_t index = 0; index < authorCount; ++index)
		{
			std::uint64_t length = 0;
			std::string author;

			// A length longer than the rest of the file fails in ReadText, so a hostile count cannot allocate much.
			if (!reader.ReadUnsigned(length, 4) || !reader.ReadText(author, static_cast<std::size_t>(length)))
			{
				return std::nullopt;
			}

			authors.push_back(std::move(author));
		}

		std::uint64_t commitCount = 0;

		if (!reader.ReadUnsigned(commitCount, 4))
		{
			return std::nullopt;
		}

		struct Row
		{
			Core::GraphCommit Commit;
			std::vector<std::uint32_t> Parents;
		};

		std::vector<Row> rows;

		for (std::uint64_t index = 0; index < commitCount; ++index)
		{
			Row row;
			std::string id;
			std::uint64_t time = 0;
			std::uint64_t author = 0;
			std::uint64_t flags = 0;
			std::uint64_t parentCount = 0;

			if (!reader.ReadText(id, Core::Oid::kByteCount) || !reader.ReadUnsigned(time, 8) || !reader.ReadUnsigned(author, 4)
				|| !reader.ReadUnsigned(flags, 1) || !reader.ReadUnsigned(parentCount, 1) || author >= authors.size())
			{
				return std::nullopt;
			}

			std::memcpy(row.Commit.Id.Bytes.data(), id.data(), Core::Oid::kByteCount);
			row.Commit.Time = static_cast<std::int64_t>(time);
			row.Commit.AuthorKey = authors[static_cast<std::size_t>(author)];
			row.Commit.ParentsCut = (flags & kParentsCutFlag) != 0;
			row.Commit.CommittedByAuthor = (flags & kCommittedByOtherFlag) == 0;

			// SaveGraph writes parents first, so a parent at or above its child is damage, and refusing it keeps a
			// cycle, which every first-parent walk would go round for ever, out of the graph.
			for (std::uint64_t parent = 0; parent < parentCount; ++parent)
			{
				std::uint64_t target = 0;

				if (!reader.ReadUnsigned(target, 4) || target >= index)
				{
					return std::nullopt;
				}

				row.Parents.push_back(static_cast<std::uint32_t>(target));
			}

			rows.push_back(std::move(row));
		}

		if (!reader.AtEnd())
		{
			return std::nullopt;
		}

		std::vector<Core::GraphCommit> commits;
		commits.reserve(rows.size());

		for (Row& row : rows)
		{
			for (const std::uint32_t parent : row.Parents)
			{
				row.Commit.Parents.push_back(rows[parent].Commit.Id);
			}

			commits.push_back(std::move(row.Commit));
		}

		Core::AncestryGraph graph;
		graph.Append(std::move(commits));
		return graph;
	}
}
