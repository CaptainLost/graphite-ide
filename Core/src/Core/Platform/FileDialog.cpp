#include "corepch.h"

#include "Core/Platform/FileDialog.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <shobjidl.h>

#include <memory>
#include <vector>
#endif

namespace Core {

#ifdef CORE_PLATFORM_WINDOWS
	namespace {
		class ComGuard
		{
		public:
			ComGuard()
			{
				const HRESULT result = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
																	 COINIT_DISABLE_OLE1DDE);
				m_Owned = SUCCEEDED(result);
				m_Ok = m_Owned || result == RPC_E_CHANGED_MODE;
			}

			~ComGuard()
			{
				if (m_Owned)
				{
					::CoUninitialize();
				}
			}

			ComGuard(const ComGuard&) = delete;
			ComGuard& operator=(const ComGuard&) = delete;

			bool Ok() const { return m_Ok; }

		private:
			bool m_Owned = false;
			bool m_Ok = false;
		};

		std::wstring Widen(const std::string& text)
		{
			if (text.empty())
			{
				return {};
			}

			const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
													 static_cast<int>(text.size()), nullptr, 0);
			std::wstring wide(static_cast<std::size_t>(length), L'\0');
			::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
								  wide.data(), length);
			return wide;
		}

		// Owners for what the dialog hands out, so every return - and a throw from a vector or a
		// string between them - releases it.
		struct ComRelease
		{
			void operator()(IUnknown* object) const { object->Release(); }
		};

		struct CoTaskMemDelete
		{
			void operator()(wchar_t* text) const { ::CoTaskMemFree(text); }
		};

		template<typename Interface>
		using ComOwner = std::unique_ptr<Interface, ComRelease>;

		// Both pickers: one dialog, configured by `options`, offering `filters` if any. `noun`
		// names what is picked in the messages. An empty path when the user cancels.
		Result<std::filesystem::path> Pick(FILEOPENDIALOGOPTIONS options, const std::string& title,
										   const std::vector<FileDialog::Filter>& filters, const std::string& noun)
		{
			const ComGuard com;
			if (!com.Ok())
			{
				return Error{ErrorCode::Unsupported, "COM initialisation failed"};
			}

			IFileOpenDialog* created = nullptr;
			if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&created))) ||
				created == nullptr)
			{
				return Error{ErrorCode::Unsupported, "cannot create the system " + noun + " dialog"};
			}

			const ComOwner<IFileOpenDialog> dialog{created};

			DWORD current = 0;
			if (FAILED(dialog->GetOptions(&current)) || FAILED(dialog->SetOptions(current | options)))
			{
				return Error{ErrorCode::Unsupported, "cannot configure the dialog as a " + noun + " picker"};
			}

			// The COMDLG_FILTERSPEC entries below hold raw pointers into these strings, and moving
			// a short wstring copies its buffer inline, so the reserve is load-bearing rather than
			// a performance hint: exactly one push_back per reserved slot, and no reallocation.
			std::vector<std::wstring> storage;
			storage.reserve(filters.size() * 2);

			std::vector<COMDLG_FILTERSPEC> specs;
			specs.reserve(filters.size());

			for (const FileDialog::Filter& filter : filters)
			{
				storage.push_back(Widen(filter.Label));
				storage.push_back(Widen(filter.Pattern));

				specs.push_back(COMDLG_FILTERSPEC{storage[storage.size() - 2].c_str(), storage.back().c_str()});
			}

			if (!specs.empty())
			{
				dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
				dialog->SetFileTypeIndex(1);
			}

			const std::wstring wideTitle = Widen(title);
			if (!wideTitle.empty())
			{
				dialog->SetTitle(wideTitle.c_str());
			}

			const HRESULT shown = dialog->Show(nullptr);

			if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED))
			{
				return std::filesystem::path{};
			}

			if (FAILED(shown))
			{
				return Error{ErrorCode::IoFailure, "the " + noun + " dialog failed"};
			}

			IShellItem* result = nullptr;
			if (FAILED(dialog->GetResult(&result)) || result == nullptr)
			{
				return Error{ErrorCode::IoFailure, "the " + noun + " dialog returned no selection"};
			}

			const ComOwner<IShellItem> item{result};

			PWSTR name = nullptr;
			if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)) || name == nullptr)
			{
				return Error{ErrorCode::IoFailure, "the selected item has no filesystem path"};
			}

			const std::unique_ptr<wchar_t, CoTaskMemDelete> selected{name};
			std::filesystem::path picked{selected.get()};

			if (picked.empty())
			{
				return Error{ErrorCode::IoFailure, "the selected item has no filesystem path"};
			}

			return picked;
		}
	}
#endif

	Result<std::filesystem::path> FileDialog::PickFolder(const std::string& title)
	{
#ifdef CORE_PLATFORM_WINDOWS
		return Pick(FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST, title, {}, "folder");
#else
		(void)title;

		return Error{ErrorCode::Unsupported,
					 "a native folder picker is not implemented on this platform"};
#endif
	}

	Result<std::filesystem::path> FileDialog::PickFile(const std::string& title,
													   const std::vector<Filter>& filters)
	{
#ifdef CORE_PLATFORM_WINDOWS
		return Pick(FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST, title, filters, "file");
#else
		(void)title;
		(void)filters;

		return Error{ErrorCode::Unsupported,
					 "a native file picker is not implemented on this platform"};
#endif
	}
}
