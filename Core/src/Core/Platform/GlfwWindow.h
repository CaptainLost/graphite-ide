#pragma once

#include "Core/Platform/Window.h"
#include "Core/Platform/WindowFrame.h"

#include <memory>
#include <utility>

struct GLFWwindow;

namespace Core
{
    class GlfwWindow final : public Window
    {
    public:
        explicit GlfwWindow(GLFWwindow* handle);
        ~GlfwWindow() override;

        void PollEvents() override;
        void SwapBuffers() override;

        [[nodiscard]] bool ShouldClose() const override;
        void RequestClose() override;
        void CancelClose() override;

        void FramebufferSize(int& width, int& height) const override;
        void SetVSync(bool enabled) override;

        [[nodiscard]] void* NativeHandle() const override { return m_handle; }

        void Minimize() override;
        void ToggleMaximize() override;
        [[nodiscard]] bool IsMaximized() const override;
        [[nodiscard]] bool HasCustomTitleBar() const override;
        void SetCaptionMetrics(const CaptionMetrics& metrics) override;

        /// Takes over a frame built for this window. Create does it once the window
        /// exists, which is the earliest the platform can be asked to drop its caption.
        void AdoptFrame(std::unique_ptr<WindowFrame> frame) { m_frame = std::move(frame); }

    private:
        GLFWwindow* m_handle = nullptr;

        /// Null while the window still wears the system title bar.
        std::unique_ptr<WindowFrame> m_frame;
    };
}
