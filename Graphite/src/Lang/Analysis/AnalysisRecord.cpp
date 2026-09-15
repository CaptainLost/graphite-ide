#include "Lang/Analysis/AnalysisRecord.h"

#include "Core/Fs/FileSystem.h"

#include <cstring>
#include <utility>

namespace Graphite {

	namespace {

		constexpr char kMagic[4] = {'G', 'R', 'A', 'F'};

		// Everything is written little endian by hand rather than by memcpy-ing a struct: a
		// record written by one build and read by another has to agree on layout, and a struct's
		// padding is not something this file gets to decide.
		void PutU32(std::string& bytes, std::uint32_t value)
		{
			for (int shift = 0; shift < 32; shift += 8)
			{
				bytes.push_back(static_cast<char>((value >> shift) & 0xFF));
			}
		}

		void PutU64(std::string& bytes, std::uint64_t value)
		{
			for (int shift = 0; shift < 64; shift += 8)
			{
				bytes.push_back(static_cast<char>((value >> shift) & 0xFF));
			}
		}

		void PutString(std::string& bytes, std::string_view text)
		{
			PutU32(bytes, static_cast<std::uint32_t>(text.size()));
			bytes.append(text);
		}

		// A cursor that cannot be made to read past its buffer. Every Take below checks what is
		// left before it moves, and a reader that has failed once stays failed, so a caller may
		// read a whole record and check once at the end.
		class Reader
		{
		public:
			explicit Reader(std::string_view bytes) : m_Bytes(bytes) {}

			bool Failed() const { return m_Failed; }

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

			bool TakeMagic()
			{
				if (!Ensure(sizeof(kMagic)))
				{
					return false;
				}

				const bool matches = m_Bytes.compare(m_At, sizeof(kMagic), kMagic, sizeof(kMagic)) == 0;
				m_At += sizeof(kMagic);

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

		std::uint64_t TicksOf(std::filesystem::file_time_type time)
		{
			return static_cast<std::uint64_t>(time.time_since_epoch().count());
		}

		std::filesystem::file_time_type TimeOf(std::uint64_t ticks)
		{
			return std::filesystem::file_time_type{
				std::filesystem::file_time_type::duration{
					static_cast<std::filesystem::file_time_type::rep>(ticks)}};
		}
	}

	std::string EncodeAnalysis(const std::filesystem::path& path, const CachedAnalysis& analysis)
	{
		std::string bytes;

		bytes.append(kMagic, sizeof(kMagic));
		PutU32(bytes, kAnalysisRecordVersion);
		PutString(bytes, Core::ToUtf8(path));
		PutU64(bytes, TicksOf(analysis.ModifiedAt));

		std::uint32_t lines = 0;
		analysis.Semantic.ForEachLine([&lines](std::size_t, const std::vector<SemanticSpan>&)
									  { ++lines; });

		PutU32(bytes, lines);

		analysis.Semantic.ForEachLine(
			[&bytes](std::size_t line, const std::vector<SemanticSpan>& spans)
			{
				PutU64(bytes, static_cast<std::uint64_t>(line));
				PutU32(bytes, static_cast<std::uint32_t>(spans.size()));

				for (const SemanticSpan& span : spans)
				{
					PutU64(bytes, static_cast<std::uint64_t>(span.From));
					PutU64(bytes, static_cast<std::uint64_t>(span.To));
					PutU32(bytes, static_cast<std::uint32_t>(span.Kind));
				}
			});

		PutU32(bytes, static_cast<std::uint32_t>(analysis.Declarations.size()));

		for (const Declaration& declaration : analysis.Declarations)
		{
			PutString(bytes, declaration.Name);
			PutU32(bytes, static_cast<std::uint32_t>(declaration.Kind));
			PutU64(bytes, static_cast<std::uint64_t>(declaration.Line));
			PutU64(bytes, static_cast<std::uint64_t>(declaration.Column.Value));
			PutU64(bytes, static_cast<std::uint64_t>(declaration.NameLine));
			PutU64(bytes, static_cast<std::uint64_t>(declaration.NameColumn.Value));
		}

		PutU32(bytes, static_cast<std::uint32_t>(analysis.Counts.size()));

		for (const KnownCount& count : analysis.Counts)
		{
			PutString(bytes, count.Name);
			PutU32(bytes, static_cast<std::uint32_t>(count.Kind));
			PutU64(bytes, static_cast<std::uint64_t>(count.Count));
		}

		return bytes;
	}

	std::optional<CachedAnalysis> DecodeAnalysis(std::string_view bytes,
												 const std::filesystem::path& expectedPath)
	{
		Reader reader{bytes};

		if (!reader.TakeMagic() || reader.TakeU32() != kAnalysisRecordVersion)
		{
			return std::nullopt;
		}

		const std::string path = reader.TakeString();

		if (reader.Failed() || path != Core::ToUtf8(expectedPath))
		{
			return std::nullopt;
		}

		CachedAnalysis analysis;
		analysis.ModifiedAt = TimeOf(reader.TakeU64());

		const std::uint32_t lines = reader.TakeU32();

		for (std::uint32_t index = 0; index < lines && !reader.Failed(); ++index)
		{
			const std::uint64_t line = reader.TakeU64();
			const std::uint32_t spanCount = reader.TakeU32();

			std::vector<SemanticSpan> spans;

			for (std::uint32_t span = 0; span < spanCount && !reader.Failed(); ++span)
			{
				SemanticSpan one;
				one.From = static_cast<std::size_t>(reader.TakeU64());
				one.To = static_cast<std::size_t>(reader.TakeU64());

				const std::uint32_t kind = reader.TakeU32();

				if (reader.Failed())
				{
					break;
				}

				if (kind >= static_cast<std::uint32_t>(TokenKind::Count))
				{
					// A kind is an index into SyntaxPalette::Colours, sized by TokenKind::Count.
					// A record claiming one past the end is corrupt, and drawing from it would
					// read past that array.
					return std::nullopt;
				}

				one.Kind = static_cast<TokenKind>(kind);

				// Appended only once the reader is known to have had the bytes for it, so a
				// corrupt count cannot fill this vector with zeroes.
				spans.push_back(one);
			}

			if (!reader.Failed())
			{
				analysis.Semantic.SetSpansOn(static_cast<std::size_t>(line), std::move(spans));
			}
		}

		const std::uint32_t declarations = reader.TakeU32();

		for (std::uint32_t index = 0; index < declarations && !reader.Failed(); ++index)
		{
			Declaration declaration;
			declaration.Name = reader.TakeString();

			const std::uint32_t kind = reader.TakeU32();

			if (reader.Failed())
			{
				break;
			}

			// Not because a DeclarationKind indexes anything today, but because a decoder that
			// validates one enumeration and waves the other through invites exactly one person to
			// add the array later.
			if (kind > static_cast<std::uint32_t>(DeclarationKind::Other))
			{
				return std::nullopt;
			}

			declaration.Kind = static_cast<DeclarationKind>(kind);
			declaration.Line = static_cast<std::size_t>(reader.TakeU64());
			declaration.Column = Utf16Column(static_cast<std::size_t>(reader.TakeU64()));
			declaration.NameLine = static_cast<std::size_t>(reader.TakeU64());
			declaration.NameColumn = Utf16Column(static_cast<std::size_t>(reader.TakeU64()));

			if (!reader.Failed())
			{
				analysis.Declarations.push_back(std::move(declaration));
			}
		}

		const std::uint32_t counts = reader.TakeU32();

		for (std::uint32_t index = 0; index < counts && !reader.Failed(); ++index)
		{
			KnownCount count;
			count.Name = reader.TakeString();

			const std::uint32_t kind = reader.TakeU32();

			if (reader.Failed())
			{
				break;
			}

			if (kind > static_cast<std::uint32_t>(DeclarationKind::Other))
			{
				return std::nullopt;
			}

			count.Kind = static_cast<DeclarationKind>(kind);
			count.Count = static_cast<std::size_t>(reader.TakeU64());

			if (!reader.Failed())
			{
				analysis.Counts.push_back(std::move(count));
			}
		}

		if (!reader.AtEnd())
		{
			return std::nullopt;
		}

		return analysis;
	}
}
