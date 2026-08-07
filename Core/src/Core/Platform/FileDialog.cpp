#include "corepch.h"

#include "Core/Platform/FileDialog.h"

#include <windows.h>

#include <shobjidl.h>

namespace core
{
    namespace
    {
        /// Scoped COM initialisation. The picker is the only COM user in the process,
        /// so it initialises and uninitialises around each call.
        class ComGuard
        {
        public:
            ComGuard()
            {
                const HRESULT result = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                                                     COINIT_DISABLE_OLE1DDE);
                m_owned = SUCCEEDED(result);
                m_ok = m_owned || result == RPC_E_CHANGED_MODE;
            }

            ~ComGuard()
            {
                if (m_owned)
                    ::CoUninitialize();
            }

            ComGuard(const ComGuard&) = delete;
            ComGuard& operator=(const ComGuard&) = delete;

            [[nodiscard]] bool Ok() const noexcept { return m_ok; }

        private:
            bool m_owned = false;
            bool m_ok = false;
        };

        std::wstring Widen(const std::string& text)
        {
            if (text.empty())
                return {};

            const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                                     static_cast<int>(text.size()), nullptr, 0);
            std::wstring wide(static_cast<std::size_t>(length), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                  wide.data(), length);
            return wide;
        }
    }

    Result<std::filesystem::path> FileDialog::PickFolder(const std::string& title)
    {
        const ComGuard com;
        if (!com.Ok())
            return Error{ErrorCode::Unsupported, "COM initialisation failed"};

        IFileOpenDialog* dialog = nullptr;
        HRESULT result = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                            IID_PPV_ARGS(&dialog));

        if (FAILED(result) || dialog == nullptr)
            return Error{ErrorCode::Unsupported, "cannot create the system folder dialog"};

        DWORD options = 0;
        if (FAILED(dialog->GetOptions(&options)) ||
            FAILED(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST)))
        {
            dialog->Release();
            return Error{ErrorCode::Unsupported, "cannot configure the dialog as a folder picker"};
        }

        const std::wstring wideTitle = Widen(title);
        if (!wideTitle.empty())
            dialog->SetTitle(wideTitle.c_str());

        result = dialog->Show(nullptr);

        if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        {
            dialog->Release();
            return std::filesystem::path{};
        }

        if (FAILED(result))
        {
            dialog->Release();
            return Error{ErrorCode::IoFailure, "the folder dialog failed"};
        }

        IShellItem* item = nullptr;
        result = dialog->GetResult(&item);

        if (FAILED(result) || item == nullptr)
        {
            dialog->Release();
            return Error{ErrorCode::IoFailure, "the folder dialog returned no selection"};
        }

        PWSTR selected = nullptr;
        result = item->GetDisplayName(SIGDN_FILESYSPATH, &selected);

        std::filesystem::path picked;
        if (SUCCEEDED(result) && selected != nullptr)
        {
            picked = selected;
            ::CoTaskMemFree(selected);
        }

        item->Release();
        dialog->Release();

        if (picked.empty())
            return Error{ErrorCode::IoFailure, "the selected item has no filesystem path"};

        return picked;
    }
}
