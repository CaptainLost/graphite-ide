#pragma once

// For use by Graphite applications

#include "Core/Base/Base.h"

#include "Core/App/Application.h"
#include "Core/App/Layer.h"
#include "Core/App/LayerStack.h"

#include "Core/Base/Assert.h"
#include "Core/Base/Ascii.h"
#include "Core/Base/Color.h"
#include "Core/Base/JsonFile.h"
#include "Core/Base/Log.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Base/RingBufferSink.h"
#include "Core/Base/Timestep.h"
#include "Core/Base/Uuid.h"

// ---Commands and shortcuts----------
#include "Core/Command/Binding.h"
#include "Core/Command/Chord.h"
#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Conflict.h"
#include "Core/Command/KeyName.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/MenuBar.h"
#include "Core/Command/MenuItem.h"
#include "Core/Command/ModifierName.h"
#include "Core/Command/Trigger.h"
#include "Core/Command/ShortcutRouter.h"
// -----------------------------------

#include "Core/Events/EventBus.h"

// ---Files---------------------------
#include "Core/Fs/AtomicWrite.h"
#include "Core/Fs/ChangeCollector.h"
#include "Core/Fs/DirectoryWatcher.h"
#include "Core/Fs/FileOperations.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Fs/FileTree.h"
// -----------------------------------

#include "Core/Git/Repository.h"

// ---ImGui---------------------------
#include "Core/ImGui/CaptionLayout.h"
#include "Core/ImGui/DockSpaceHost.h"
#include "Core/ImGui/Icons.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/Theme.h"
// -----------------------------------

// ---Platform------------------------
// GlfwWindow.h and WindowFrame.h are deliberately absent: they are the implementation
// behind Window::Create, and an application that names them has reached past the seam.
#include "Core/Platform/Caption.h"
#include "Core/Platform/FileDialog.h"
#include "Core/Platform/Process.h"
#include "Core/Platform/Shell.h"
#include "Core/Platform/Window.h"
// -----------------------------------

// ---Text----------------------------
#include "Core/Text/Caret.h"
#include "Core/Text/Codec.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Editing.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/FileProbe.h"
#include "Core/Text/FileSync.h"
#include "Core/Text/Indent.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextBuffer.h"
#include "Core/Text/TextDocument.h"
#include "Core/Text/WrapLayout.h"
// -----------------------------------

#include "Core/UI/Panel.h"
#include "Core/UI/PanelManager.h"
