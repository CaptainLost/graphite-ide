#pragma once

#include "Core/Base/NonCopyable.h"

#include <string>
#include <utility>

namespace core
{
    /// Jednostka zycia aplikacji: dostaje aktualizacje i rysuje wlasne UI.
    class Layer : NonCopyable
    {
    public:
        explicit Layer(std::string name) : m_name(std::move(name)) {}
        virtual ~Layer() = default;

        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnUpdate(float deltaSeconds) { (void)deltaSeconds; }
        virtual void OnUIRender() {}

        [[nodiscard]] const std::string& Name() const noexcept { return m_name; }

    private:
        std::string m_name;
    };
}
