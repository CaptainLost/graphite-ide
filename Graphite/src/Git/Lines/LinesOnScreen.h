#pragma once

#include "Git/Lines/BranchLines.h"
#include "Git/Lines/LineReader.h"

#include "Core/Base/Base.h"

#include <cstddef>
#include <optional>
#include <string_view>

namespace Graphite {

	// The line read the Git window draws, with what is looked up in it. Followed once at the start of every frame the
	// window is drawn, so the sidebar and the history read the same one, whichever of them is drawn or asked first.
	class LinesOnScreen
	{
	public:
		// Takes `read` when it is not the read held; null lets go of the one held.
		void Follow(Core::Ref<const LineRead> read);

		void Reset();

		const Core::Ref<const LineRead>& GetRead() const { return m_Read; }

		// Null without a read.
		const BranchLines* GetLines() const { return m_Lines.has_value() ? &*m_Lines : nullptr; }

		// The checked-out branch's ref, into LineRead::Refs: its row stands for HEAD's history.
		std::optional<std::size_t> GetHeadRef() const { return m_HeadRef; }

		// The full name of the branch the read names checked out; empty without a read and while it names none.
		std::string_view CheckedOutName() const;

	private:
		// WARNING: The lines view into the read, so they go before it does.
		Core::Ref<const LineRead> m_Read;
		std::optional<BranchLines> m_Lines;
		std::optional<std::size_t> m_HeadRef;
	};
}
