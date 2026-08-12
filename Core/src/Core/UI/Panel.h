#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"

#include <string>
#include <utility>

namespace Core {

	class Panel : NonCopyable
	{
	public:
		Panel(std::string id, std::string title, bool visible = true)
			: m_Id(std::move(id)), m_Title(std::move(title)), m_Visible(visible),
			  m_DefaultVisible(visible)
		{
		}

		virtual ~Panel() = default;

		virtual void OnUpdate(Timestep ts) { (void)ts; }
		virtual void OnImGuiRender() {}

		virtual void OnNotDrawn() {}

		virtual bool IsCapturingInput() const { return false; }

		const std::string& GetId() const { return m_Id; }
		const std::string& GetTitle() const { return m_Title; }

		bool IsVisible() const { return m_Visible; }
		void SetVisible(bool visible) { m_Visible = visible; }

		bool IsDefaultVisible() const { return m_DefaultVisible; }

	private:
		std::string m_Id;
		std::string m_Title;
		bool m_Visible = true;
		bool m_DefaultVisible = true;
	};
}