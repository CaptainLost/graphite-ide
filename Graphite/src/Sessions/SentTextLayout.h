#pragma once

#include "Sessions/RequestTimeline.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	// How many columns a tab takes where sent text is laid out.
	inline constexpr std::size_t kTabColumns = 4;

	// Where `text` breaks into lines no wider than `columns` characters: its own line ends
	// first, a carriage return before one dropped, then after the last space that fits, or
	// where the width runs out when none does — never inside a character. A row a break began
	// skips one space it would otherwise start with. Each line is a half-open byte range of
	// `text`. An empty text has no lines, and a final line end adds no empty one after it.
	std::vector<std::pair<std::size_t, std::size_t>> WrapLines(std::string_view text, std::size_t columns);

	// A request's items in the order its details list them — what it sent, then what it
	// returned — so one index names the same item in the list and in the text beside it.
	std::size_t SegmentCount(const ApiRequest& request);

	// The text of the item at `segment`, or nothing past the last one.
	std::string_view SegmentText(const ApiRequest& request, std::size_t segment);

	// What to show under a segment's header: SegmentText, except an empty thinking block gets a
	// note explaining why there is nothing to show, since the API returns it with an empty text
	// and only an encrypted signature. Copying an item still copies SegmentText, so Copy sees the
	// real, empty text rather than this note.
	std::string_view DisplayText(const ApiRequest& request, std::size_t segment);

	// Every item's text, a blank line between items: what "copy all" copies.
	std::string AllSegmentText(const ApiRequest& request);

	// One line of the text beside a request's items: an item's header, or a slice of its text.
	struct SentTextRow
	{
		std::size_t Segment = 0;
		bool Header = false;

		// The slice, as bytes of the segment's text. Both zero for a header.
		std::size_t Begin = 0;
		std::size_t End = 0;
	};

	// The rows of a request's text at one width. Keeps no text: a row names its segment and
	// a range, and whoever draws it reads the text from the request, so a layout never
	// points into strings that have since moved.
	class SentTextLayout
	{
	public:
		void Build(const std::vector<std::string_view>& segments, std::size_t columns);

		const std::vector<SentTextRow>& GetRows() const { return m_Rows; }

		// The header row of `segment`, or the row count when there is no such segment.
		std::size_t FirstRowOf(std::size_t segment) const;

	private:
		std::vector<SentTextRow> m_Rows;
		std::vector<std::size_t> m_FirstRows;
	};
}
