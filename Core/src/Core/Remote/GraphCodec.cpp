#include "corepch.h"

#include "Core/Remote/GraphCodec.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>
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

		Error EndsEarly()
		{
			return Error{ErrorCode::InvalidArgument, "the graph commits end early"};
		}
	}

	std::string EncodeGraphCommits(const std::vector<GraphCommit>& commits)
	{
		std::string bytes;
		Put(bytes, static_cast<std::uint32_t>(commits.size()));

		for (const GraphCommit& commit : commits)
		{
			PutId(bytes, commit.Id);
			Put(bytes, static_cast<std::uint8_t>((commit.ParentsCut ? kParentsCut : 0) | (commit.CommittedByAuthor ? kCommittedByAuthor : 0)));
			Put(bytes, commit.Time);
			Put(bytes, static_cast<std::uint32_t>(commit.Parents.size()));

			for (const Oid& parent : commit.Parents)
			{
				PutId(bytes, parent);
			}

			Put(bytes, static_cast<std::uint32_t>(commit.AuthorKey.size()));
			bytes += commit.AuthorKey;
		}

		return bytes;
	}

	Result<std::vector<GraphCommit>> DecodeGraphCommits(std::string_view bytes)
	{
		Reader reader{bytes};
		const std::optional<std::uint32_t> count = reader.Take<std::uint32_t>();

		if (!count)
		{
			return EndsEarly();
		}

		std::vector<GraphCommit> commits;

		// Each commit takes at least its id, flags, time and two counts: a count no payload this size
		// could hold is not reserved for.
		constexpr std::size_t kSmallestCommit = Oid::kByteCount + 1 + 8 + 4 + 4;
		commits.reserve(std::min<std::size_t>(*count, reader.Left() / kSmallestCommit));

		for (std::uint32_t index = 0; index < *count; ++index)
		{
			GraphCommit commit;
			const std::optional<Oid> id = reader.TakeId();
			const std::optional<std::uint8_t> flags = reader.Take<std::uint8_t>();
			const std::optional<std::int64_t> time = reader.Take<std::int64_t>();
			const std::optional<std::uint32_t> parents = reader.Take<std::uint32_t>();

			if (!id || !flags || !time || !parents)
			{
				return EndsEarly();
			}

			commit.Id = *id;
			commit.ParentsCut = (*flags & kParentsCut) != 0;
			commit.CommittedByAuthor = (*flags & kCommittedByAuthor) != 0;
			commit.Time = *time;

			for (std::uint32_t parent = 0; parent < *parents; ++parent)
			{
				const std::optional<Oid> parentId = reader.TakeId();

				if (!parentId)
				{
					return EndsEarly();
				}

				commit.Parents.push_back(*parentId);
			}

			const std::optional<std::uint32_t> keySize = reader.Take<std::uint32_t>();
			std::optional<std::string> key = keySize ? reader.TakeText(*keySize) : std::nullopt;

			if (!key)
			{
				return EndsEarly();
			}

			commit.AuthorKey = std::move(*key);
			commits.push_back(std::move(commit));
		}

		return commits;
	}
}
