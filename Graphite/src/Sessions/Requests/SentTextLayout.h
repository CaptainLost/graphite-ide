#pragma once

#include "Sessions/Requests/RequestTimeline.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	inline constexpr std::size_t kTabColumns = 4;

	using ByteRanges = std::vector<std::pair<std::size_t, std::size_t>>;

	ByteRanges WrapLines(std::string_view text, std::size_t columns);

	std::size_t SegmentCount(const ApiRequest& request);

	std::string_view SegmentText(const ApiRequest& request, std::size_t segment);

	bool IsEmptyThinking(const ApiRequest& request, std::size_t segment);

	std::string_view DisplayText(const ApiRequest& request, std::size_t segment);

	std::string AllSegmentText(const ApiRequest& request);

	struct SentTextRow
	{
		std::size_t Segment = 0;
		bool Header = false;
		std::size_t Begin = 0;
		std::size_t End = 0;
	};

	class SentTextLayout
	{
	public:
		void Build(const std::vector<std::string_view>& segments, std::size_t columns);

		const std::vector<SentTextRow>& GetRows() const { return m_Rows; }

		std::size_t FirstRowOf(std::size_t segment) const;

	private:
		std::vector<SentTextRow> m_Rows;
		std::vector<std::size_t> m_FirstRows;
	};
}
