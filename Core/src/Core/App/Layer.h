#pragma once

#include "Core/Base/NonCopyable.h"

#include <string>
#include <utility>

namespace core
{
    /// Unit of application lifetime: receives updates and draws its own UI.
    class Layer : NonCopyable
    {
    public:
        explicit Layer(std::string name) : m_name(std::move(name)) {}
        virtual ~Layer() = default;

        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnUpdate(float deltaSeconds) { (void)deltaSeconds; }
        virtual void OnUIRender() {}

        /// Whether the application may close now. A layer returning false is expected to
        /// be asking the user something; it will be asked again every frame until it
        /// answers true, which is what lets a modal draw at all.
        virtual bool OnCloseRequested() { return true; }

        [[nodiscard]] const std::string& Name() const noexcept { return m_name; }

    private:
        std::string m_name;
    };
}
