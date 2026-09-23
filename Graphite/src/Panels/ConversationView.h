#pragma once

#include "Sessions/Session.h"

#include "Core/Events/EventBus.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

namespace Graphite {

	// A session drawn as a conversation, virtualised: only rows near the visible range are
	// drawn. Row heights start as estimates and are replaced by what each row really took the
	// first time it is drawn, so scrolling settles without measuring rows nobody looks at.
	class ConversationView
	{
	public:
		void Draw(const Session& session, Core::EventBus& events);

	private:
		enum class RowKind
		{
			Prompt,
			Item
		};

		struct Row
		{
			RowKind Kind = RowKind::Prompt;
			std::size_t Turn = 0;
			std::size_t Item = 0;
		};

		void Rebuild(const Session& session, float width);
		float Estimate(const Session& session, const Row& row, float width) const;
		void DrawRow(const Session& session, std::size_t index, Core::EventBus& events);
		void DrawTool(const ToolCall& tool, std::size_t row, Core::EventBus& events);

		std::vector<Row> m_Rows;
		std::vector<float> m_Heights;
		std::vector<bool> m_Measured;
		std::vector<float> m_Offsets;

		// Rows the reader opened: thinking and tool calls start closed.
		std::set<std::size_t> m_Expanded;

		std::uint64_t m_BuiltVersion = UINT64_MAX;

		// UINT64_MAX means "never built"; a real session's generation starts at 0, so this
		// can never collide with one and mistake a first build for the same session.
		std::uint64_t m_BuiltGeneration = UINT64_MAX;
		float m_BuiltWidth = -1.0f;
		bool m_OffsetsDirty = true;
		bool m_FollowTail = true;
	};
}
