#include "Language/Analysis/AnalysisRecord.h"

#include "Core/FileSystem/FileSystem.h"

#include <utility>

namespace Graphite {

	namespace {

		bool IsKnownTokenKind(std::uint32_t kind)
		{
			return kind < static_cast<std::uint32_t>(TokenKind::Count);
		}

		bool IsKnownDeclarationKind(std::uint32_t kind)
		{
			return kind <= static_cast<std::uint32_t>(DeclarationKind::Other);
		}

		bool IsEarlierOrNoParent(int parent, std::size_t index)
		{
			return parent == kNoParentDeclaration || (parent >= 0 && static_cast<std::size_t>(parent) < index);
		}

		void AppendSemanticLine(std::string& bytes, std::size_t line,
								const std::vector<SemanticSpan>& spans)
		{
			AppendUint64(bytes, static_cast<std::uint64_t>(line));
			AppendUint32(bytes, static_cast<std::uint32_t>(spans.size()));

			for (const SemanticSpan& span : spans)
			{
				AppendUint64(bytes, static_cast<std::uint64_t>(span.From));
				AppendUint64(bytes, static_cast<std::uint64_t>(span.To));
				AppendUint32(bytes, static_cast<std::uint32_t>(span.Kind));
			}
		}
	}

	void AppendUint32(std::string& bytes, std::uint32_t value)
	{
		for (int shift = 0; shift < kUint32Bits; shift += kBitsPerByte)
		{
			bytes.push_back(static_cast<char>((value >> shift) & kLowestByteMask));
		}
	}

	void AppendUint64(std::string& bytes, std::uint64_t value)
	{
		for (int shift = 0; shift < kUint64Bits; shift += kBitsPerByte)
		{
			bytes.push_back(static_cast<char>((value >> shift) & kLowestByteMask));
		}
	}

	void AppendString(std::string& bytes, std::string_view text)
	{
		AppendUint32(bytes, static_cast<std::uint32_t>(text.size()));
		bytes.append(text);
	}

	std::uint64_t FileTimeToTicks(std::filesystem::file_time_type time)
	{
		return static_cast<std::uint64_t>(time.time_since_epoch().count());
	}

	std::filesystem::file_time_type TicksToFileTime(std::uint64_t ticks)
	{
		return std::filesystem::file_time_type{std::filesystem::file_time_type::duration{
			static_cast<std::filesystem::file_time_type::rep>(ticks)}};
	}

	std::string EncodeAnalysis(const std::filesystem::path& path, const CachedAnalysis& analysis)
	{
		std::string bytes;

		bytes.append(kAnalysisMagic);
		AppendUint32(bytes, kAnalysisRecordVersion);
		AppendString(bytes, Core::ToUtf8(path));
		AppendUint64(bytes, FileTimeToTicks(analysis.ModifiedAt));

		std::uint32_t lineCount = 0;
		analysis.Semantic.ForEachLine(
			[&lineCount](std::size_t, const std::vector<SemanticSpan>&) { ++lineCount; });

		AppendUint32(bytes, lineCount);

		analysis.Semantic.ForEachLine(
			[&bytes](std::size_t line, const std::vector<SemanticSpan>& spans) { AppendSemanticLine(bytes, line, spans); });

		AppendUint32(bytes, static_cast<std::uint32_t>(analysis.Declarations.size()));

		for (const Declaration& declaration : analysis.Declarations)
		{
			AppendString(bytes, declaration.Name);
			AppendUint32(bytes, static_cast<std::uint32_t>(declaration.Kind));
			AppendUint64(bytes, static_cast<std::uint64_t>(declaration.Line));
			AppendUint64(bytes, static_cast<std::uint64_t>(declaration.Column.Value));
			AppendUint64(bytes, static_cast<std::uint64_t>(declaration.NameLine));
			AppendUint64(bytes, static_cast<std::uint64_t>(declaration.NameColumn.Value));
			AppendUint64(bytes, static_cast<std::uint64_t>(declaration.EndLine));
			AppendUint32(bytes, static_cast<std::uint32_t>(declaration.Parent));
		}

		AppendUint32(bytes, static_cast<std::uint32_t>(analysis.Counts.size()));

		for (const KnownCount& knownCount : analysis.Counts)
		{
			AppendString(bytes, knownCount.Name);
			AppendUint32(bytes, static_cast<std::uint32_t>(knownCount.Kind));
			AppendUint64(bytes, static_cast<std::uint64_t>(knownCount.Count));
		}

		return bytes;
	}

	std::optional<CachedAnalysis> DecodeAnalysis(std::string_view bytes,
												 const std::filesystem::path& expectedPath)
	{
		Reader reader{bytes};

		if (!reader.TakeMagic(kAnalysisMagic) || reader.TakeUint32() != kAnalysisRecordVersion)
		{
			return std::nullopt;
		}

		const std::string path = reader.TakeString();

		if (reader.HasFailed() || path != Core::ToUtf8(expectedPath))
		{
			return std::nullopt;
		}

		CachedAnalysis analysis;
		analysis.ModifiedAt = TicksToFileTime(reader.TakeUint64());

		const std::uint32_t lineCount = reader.TakeUint32();

		for (std::uint32_t lineIndex = 0; lineIndex < lineCount && !reader.HasFailed(); ++lineIndex)
		{
			const std::uint64_t line = reader.TakeUint64();
			const std::uint32_t spanCount = reader.TakeUint32();

			std::vector<SemanticSpan> spans;

			for (std::uint32_t spanIndex = 0; spanIndex < spanCount && !reader.HasFailed();
				 ++spanIndex)
			{
				SemanticSpan span;
				span.From = static_cast<std::size_t>(reader.TakeUint64());
				span.To = static_cast<std::size_t>(reader.TakeUint64());

				const std::uint32_t kind = reader.TakeUint32();

				if (reader.HasFailed())
				{
					break;
				}

				if (!IsKnownTokenKind(kind))
				{
					return std::nullopt;
				}

				span.Kind = static_cast<TokenKind>(kind);

				spans.push_back(span);
			}

			if (!reader.HasFailed())
			{
				analysis.Semantic.SetSpansOn(static_cast<std::size_t>(line), std::move(spans));
			}
		}

		const std::uint32_t declarationCount = reader.TakeUint32();

		for (std::uint32_t declarationIndex = 0;
			 declarationIndex < declarationCount && !reader.HasFailed(); ++declarationIndex)
		{
			Declaration declaration;
			declaration.Name = reader.TakeString();

			const std::uint32_t kind = reader.TakeUint32();

			if (reader.HasFailed())
			{
				break;
			}

			if (!IsKnownDeclarationKind(kind))
			{
				return std::nullopt;
			}

			declaration.Kind = static_cast<DeclarationKind>(kind);
			declaration.Line = static_cast<std::size_t>(reader.TakeUint64());
			declaration.Column = Utf16Column(static_cast<std::size_t>(reader.TakeUint64()));
			declaration.NameLine = static_cast<std::size_t>(reader.TakeUint64());
			declaration.NameColumn = Utf16Column(static_cast<std::size_t>(reader.TakeUint64()));
			declaration.EndLine = static_cast<std::size_t>(reader.TakeUint64());
			declaration.Parent = static_cast<int>(reader.TakeUint32());

			if (reader.HasFailed())
			{
				break;
			}

			if (!IsEarlierOrNoParent(declaration.Parent, analysis.Declarations.size()))
			{
				return std::nullopt;
			}

			analysis.Declarations.push_back(std::move(declaration));
		}

		const std::uint32_t knownCountTotal = reader.TakeUint32();

		for (std::uint32_t knownCountIndex = 0;
			 knownCountIndex < knownCountTotal && !reader.HasFailed(); ++knownCountIndex)
		{
			KnownCount knownCount;
			knownCount.Name = reader.TakeString();

			const std::uint32_t kind = reader.TakeUint32();

			if (reader.HasFailed())
			{
				break;
			}

			if (!IsKnownDeclarationKind(kind))
			{
				return std::nullopt;
			}

			knownCount.Kind = static_cast<DeclarationKind>(kind);
			knownCount.Count = static_cast<std::size_t>(reader.TakeUint64());

			if (!reader.HasFailed())
			{
				analysis.Counts.push_back(std::move(knownCount));
			}
		}

		if (!reader.AtEnd())
		{
			return std::nullopt;
		}

		return analysis;
	}
}
