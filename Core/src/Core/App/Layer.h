#pragma once

#include "Core/Base/NonCopyable.h"

#include <string>
#include <utility>

namespace Core
{
    /// Unit of application lifetime: receives updates and draws its own UI.
    class Layer : NonCopyable
    {
    public:
        explicit Layer(std::string name) : m_Name(std::move(name)) {}
        virtual ~Layer() = default;

        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnUpdate(float deltaSeconds) { (void)deltaSeconds; }
        virtual void OnUIRender() {}

        /// Whether the application may close now. A layer returning false is expected to
        /// be asking the user something; it will be asked again every frame until it
        /// answers true, which is what lets a modal draw at all.
        ///
        /// Returning true does not by itself close the window: refusing already cleared
        /// the flag the loop tests, so a layer that has finished asking must call
        /// Window::RequestClose() itself to re-arm it.
        virtual bool OnCloseRequested() { return true; }

        [[nodiscard]] const std::string& GetName() const noexcept { return m_Name; }

    private:
        std::string m_Name;
    };
}
