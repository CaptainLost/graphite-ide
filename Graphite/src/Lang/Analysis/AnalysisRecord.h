#pragma once

#include "Editor/AnalysisCache.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	// The format the on-disk store writes. Bumped whenever the layout below changes in any way —
	// a record from another version is discarded rather than reinterpreted, which is the whole
	// benefit of a version field on a cache nobody has to migrate.
	inline constexpr std::uint32_t kAnalysisRecordVersion = 1;

	// The four bytes a record opens with. Handed to Reader::TakeMagic rather than baked into it,
	// because the store's index alongside these records opens with four of its own and both are
	// read through the one cursor below.
	inline constexpr std::string_view kAnalysisMagic = "GRAF";

	// Everything is written little endian by hand rather than by memcpy-ing a struct: bytes
	// written by one build and read by another have to agree on layout, and a struct's padding is
	// not something this file gets to decide.
	void PutU32(std::string& bytes, std::uint32_t value);
	void PutU64(std::string& bytes, std::uint64_t value);
	void PutString(std::string& bytes, std::string_view text);

	// A file's modification time as bytes and back. The representation is whatever the standard
	// library's clock counts in, which is why it is only ever compared against another stamp read
	// from the same build — a version bump is what makes a change of clock safe.
	std::uint64_t FileTimeToTicks(std::filesystem::file_time_type time);
	std::filesystem::file_time_type TicksToFileTime(std::uint64_t ticks);

	// A cursor that cannot be made to read past its buffer. Every Take below checks what is left
	// before it moves, and a reader that has failed once stays failed, so a caller may read a
	// whole record and check once at the end.
	//
	// Out here rather than private to the record because the store's index needs exactly this and
	// nothing else: a second bounds-checked parser would be the same code twice, and the copy that
	// was not fixed is the one that reads past its buffer.
	class Reader
	{
	public:
		explicit Reader(std::string_view bytes) : m_Bytes(bytes) {}

		bool HasFailed() const { return m_Failed; }

		std::uint32_t TakeU32()
		{
			std::uint32_t value = 0;

			if (!Ensure(4))
			{
				return 0;
			}

			for (int shift = 0; shift < 32; shift += 8)
			{
				value |= static_cast<std::uint32_t>(
							 static_cast<unsigned char>(m_Bytes[m_At++]))
						 << shift;
			}

			return value;
		}

		std::uint64_t TakeU64()
		{
			std::uint64_t value = 0;

			if (!Ensure(8))
			{
				return 0;
			}

			for (int shift = 0; shift < 64; shift += 8)
			{
				value |= static_cast<std::uint64_t>(
							 static_cast<unsigned char>(m_Bytes[m_At++]))
						 << shift;
			}

			return value;
		}

		std::string TakeString()
		{
			const std::uint32_t length = TakeU32();

			if (!Ensure(length))
			{
				return {};
			}

			std::string text{m_Bytes.substr(m_At, length)};
			m_At += length;

			return text;
		}

		bool TakeMagic(std::string_view magic)
		{
			if (!Ensure(magic.size()))
			{
				return false;
			}

			const bool matches = m_Bytes.compare(m_At, magic.size(), magic) == 0;
			m_At += magic.size();

			return matches;
		}

		// A record that decoded cleanly but has bytes left over is as wrong as a short one:
		// it means the writer and this reader disagree about the layout.
		bool AtEnd() const { return !m_Failed && m_At == m_Bytes.size(); }

	private:
		bool Ensure(std::size_t count)
		{
			if (m_Failed || m_Bytes.size() - m_At < count)
			{
				m_Failed = true;
				return false;
			}

			return true;
		}

		std::string_view m_Bytes;
		std::size_t m_At = 0;
		bool m_Failed = false;
	};

	// One analysis as bytes, and back.
	//
	// Binary rather than JSON: a large file's overlay runs to thousands of spans, and this is read
	// on the UI thread in the frame a document opens. Free functions over a string rather than a
	// stream, and no filesystem anywhere in here, so every way a record can be malformed is a unit
	// test rather than a temporary directory.
	//
	// `path` is written into the record and checked on the way back. Records are named on disk by
	// a hash of the path, and a hash has collisions; this is what makes one file's colours unable
	// to be drawn over another's.
	std::string EncodeAnalysis(const std::filesystem::path& path, const CachedAnalysis& analysis);

	// Empty for anything that is not exactly a record of this version for exactly this path:
	// wrong magic, wrong version, a length that claims more than is there, a truncated tail, or a
	// TokenKind/DeclarationKind value outside what this build knows how to draw. A cache has
	// nothing worth half-trusting, so there is deliberately no partial result and no error to
	// report — the caller re-asks the server, which is what it would have done anyway.
	std::optional<CachedAnalysis> DecodeAnalysis(std::string_view bytes,
												 const std::filesystem::path& expectedPath);
}
