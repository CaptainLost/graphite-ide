#include "corepch.h"

#include "Core/Remote/GraphCodec.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace Core {

	namespace {

		constexpr std::uint8_t kParentsCut = 1;
		constexpr std::uint8_t kCommittedByAuthor = 2;

		template<typename Integer>
		void Put(std::string& bytes, Integer value)
		{
			using Unsigned = std::make_unsigned_t<Integer>;
			const auto bits = static_cast<Unsigned>(value);

			for (std::size_t index = 0; index < sizeof(Integer); ++index)
			{
				bytes.push_back(static_cast<char>((bits >> (8 * index)) & 0xFF));
			}
		}

		void PutId(std::string& bytes, const Oid& id)
		{
			bytes.append(reinterpret_cast<const char*>(id.Bytes.data()), id.Bytes.size());
		}

		// Reads front to back; every read says whether the bytes held it.
		class Reader
		{
		public:
			explicit Reader(std::string_view bytes) : m_Bytes(bytes) {}

			template<typename Integer>
			std::optional<Integer> Take()
			{
				if (m_Bytes.size() < sizeof(Integer))
				{
					return std::nullopt;
				}

				std::make_unsigned_t<Integer> bits = 0;

				for (std::size_t index = 0; index < sizeof(Integer); ++index)
				{
					bits |= static_cast<std::make_unsigned_t<Integer>>(static_cast<std::uint8_t>(m_Bytes[index])) << (8 * index);
				}

				m_Bytes.remove_prefix(sizeof(Integer));
				return static_cast<Integer>(bits);
			}

			std::optional<Oid> TakeId()
			{
				if (m_Bytes.size() < Oid::kByteCount)
				{
					return std::nullopt;
				}

				Oid id;
				std::memcpy(id.Bytes.data(), m_Bytes.data(), Oid::kByteCount);
				m_Bytes.remove_prefix(Oid::kByteCount);
				return id;
			}

			std::optional<std::string> TakeText(std::size_t size)
			{
				if (m_Bytes.size() < size)
				{
					return std::nullopt;
				}

				std::string text{m_Bytes.substr(0, size)};
				m_Bytes.remove_prefix(size);
				return text;
			}

			std::size_t Left() const { return m_Bytes.size(); }

		private:
			std::string_view m_Bytes;
		};

		Error Malformed(const char* why)
		{
			return Error{ErrorCode::InvalidArgument, std::string{"the graph commits "} + why};
		}
	}

	std::string EncodeGraphCommits(const std::vector<GraphCommit>& commits)
	{
		// Author keys repeat across a history: each is written once, and commits name it by index.
		std::vector<const std::string*> keys;
		std::unordered_map<std::string_view, std::uint32_t> indexOfKey;
		std::vector<std::uint32_t> keyOfCommit;
		keyOfCommit.reserve(commits.size());

		for (const GraphCommit& commit : commits)
		{
			const auto [found, added] = indexOfKey.try_emplace(commit.AuthorKey, static_cast<std::uint32_t>(keys.size()));

			if (added)
			{
				keys.push_back(&commit.AuthorKey);
			}

			keyOfCommit.push_back(found->second);
		}

		std::string bytes;
		Put(bytes, static_cast<std::uint32_t>(keys.size()));

		for (const std::string* key : keys)
		{
			Put(bytes, static_cast<std::uint32_t>(key->size()));
			bytes += *key;
		}

		Put(bytes, static_cast<std::uint32_t>(commits.size()));

		for (std::size_t index = 0; index < commits.size(); ++index)
		{
			const GraphCommit& commit = commits[index];
			PutId(bytes, commit.Id);
			Put(bytes, static_cast<std::uint8_t>((commit.ParentsCut ? kParentsCut : 0) | (commit.CommittedByAuthor ? kCommittedByAuthor : 0)));
			Put(bytes, commit.Time);
			Put(bytes, keyOfCommit[index]);
			Put(bytes, static_cast<std::uint32_t>(commit.Parents.size()));

			for (const Oid& parent : commit.Parents)
			{
				PutId(bytes, parent);
			}
		}

		return bytes;
	}

	Result<std::vector<GraphCommit>> DecodeGraphCommits(std::string_view bytes)
	{
		Reader reader{bytes};
		const std::optional<std::uint32_t> keyCount = reader.Take<std::uint32_t>();

		if (!keyCount)
		{
			return Malformed("end early");
		}

		std::vector<std::string> keys;
		keys.reserve(std::min<std::size_t>(*keyCount, reader.Left() / 4));

		for (std::uint32_t index = 0; index < *keyCount; ++index)
		{
			const std::optional<std::uint32_t> size = reader.Take<std::uint32_t>();
			std::optional<std::string> key = size ? reader.TakeText(*size) : std::nullopt;

			if (!key)
			{
				return Malformed("end early");
			}

			keys.push_back(std::move(*key));
		}

		const std::optional<std::uint32_t> count = reader.Take<std::uint32_t>();

		if (!count)
		{
			return Malformed("end early");
		}

		// Each commit takes at least its id, flags, time, key index and parent count: a count no
		// payload this size could hold is not reserved for.
		constexpr std::size_t kSmallestCommit = Oid::kByteCount + 1 + 8 + 4 + 4;
		std::vector<GraphCommit> commits;
		commits.reserve(std::min<std::size_t>(*count, reader.Left() / kSmallestCommit));

		for (std::uint32_t index = 0; index < *count; ++index)
		{
			GraphCommit commit;
			const std::optional<Oid> id = reader.TakeId();
			const std::optional<std::uint8_t> flags = reader.Take<std::uint8_t>();
			const std::optional<std::int64_t> time = reader.Take<std::int64_t>();
			const std::optional<std::uint32_t> key = reader.Take<std::uint32_t>();
			const std::optional<std::uint32_t> parents = reader.Take<std::uint32_t>();

			if (!id || !flags || !time || !key || !parents)
			{
				return Malformed("end early");
			}

			if (*key >= keys.size())
			{
				return Malformed("name an author key they do not hold");
			}

			commit.Id = *id;
			commit.ParentsCut = (*flags & kParentsCut) != 0;
			commit.CommittedByAuthor = (*flags & kCommittedByAuthor) != 0;
			commit.Time = *time;
			commit.AuthorKey = keys[*key];

			for (std::uint32_t parent = 0; parent < *parents; ++parent)
			{
				const std::optional<Oid> parentId = reader.TakeId();

				if (!parentId)
				{
					return Malformed("end early");
				}

				commit.Parents.push_back(*parentId);
			}

			commits.push_back(std::move(commit));
		}

		if (reader.Left() != 0)
		{
			return Malformed("are followed by bytes that are none of them");
		}

		return commits;
	}

	std::string EncodeCommitIds(const std::vector<Oid>& ids)
	{
		std::string bytes;
		bytes.reserve(ids.size() * Oid::kByteCount);

		for (const Oid& id : ids)
		{
			PutId(bytes, id);
		}

		return bytes;
	}

	Result<std::vector<Oid>> DecodeCommitIds(std::string_view bytes)
	{
		if (bytes.size() % Oid::kByteCount != 0)
		{
			return Error{ErrorCode::InvalidArgument, "the commit ids are not a whole number of ids"};
		}

		Reader reader{bytes};
		std::vector<Oid> ids;
		ids.reserve(bytes.size() / Oid::kByteCount);

		while (reader.Left() > 0)
		{
			ids.push_back(*reader.TakeId());
		}

		return ids;
	}
}
