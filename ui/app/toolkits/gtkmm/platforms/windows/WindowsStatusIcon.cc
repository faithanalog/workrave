// Copyright (C) 2010, 2011, 2013 Rob Caelers <robc@krandor.org>
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "WindowsStatusIcon.hh"

#include <string>
#include <shellapi.h>

#include "core/CoreTypes.hh"
#include "commonui/MenuDefs.hh"

#include "debug.hh"

using namespace std;

const UINT MYWM_TRAY_MESSAGE = WM_USER + 0x100;

WindowsStatusIcon::WindowsStatusIcon(std::shared_ptr<IApplication> app)
  : toolkit(app->get_toolkit())
  , menu_model(app->get_menu_model())
  , menu_helper(menu_model)
{
  init();
  menu_helper.setup_event();
  auto core = app->get_core();
  workrave::utils::connect(core->signal_operation_mode_changed(), tracker, [this](auto mode) { set_operation_mode(mode); });
  workrave::OperationMode mode = core->get_regular_operation_mode();
  set_operation_mode(mode);
}

WindowsStatusIcon::~WindowsStatusIcon()
{
  cleanup();
}

void
WindowsStatusIcon::set_operation_mode(workrave::OperationMode m)
{
  TRACE_ENTRY();

  HICON old_hicon = nid.hIcon;

  HINSTANCE hinstance = GetModuleHandle(nullptr);

  switch (m)
    {
    case workrave::OperationMode::Normal:
      nid.hIcon = LoadIconA(hinstance, "workrave");
      break;
    case workrave::OperationMode::Quiet:
      nid.hIcon = LoadIconA(hinstance, "workravequiet");
      break;
    case workrave::OperationMode::Suspended:
      nid.hIcon = LoadIconA(hinstance, "workravesusp");
      break;
    }

  nid.uFlags |= NIF_ICON;
  if (nid.hWnd != nullptr && visible)
    {
      Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

  if (old_hicon != nullptr)
    {
      DestroyIcon(old_hicon);
    }
}

void
WindowsStatusIcon::set_tooltip(const Glib::ustring &text)
{
  gunichar2 *wtext = g_utf8_to_utf16(text.c_str(), -1, nullptr, nullptr, nullptr);

  if (wtext != nullptr)
    {
      nid.uFlags |= NIF_TIP;
      wcsncpy(nid.szTip, (wchar_t *)wtext, G_N_ELEMENTS(nid.szTip) - 1);
      nid.szTip[G_N_ELEMENTS(nid.szTip) - 1] = 0;
      g_free(wtext);
    }
  else
    {
      nid.uFlags &= ~NIF_TIP;
      nid.szTip[0] = 0;
    }

  if (nid.hWnd != nullptr && visible)
    {
      Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
}

void
WindowsStatusIcon::show_balloon(string id, const Glib::ustring &balloon)
{
  TRACE_ENTRY();
  gunichar2 *winfo = g_utf8_to_utf16(balloon.c_str(), -1, nullptr, nullptr, nullptr);
  gunichar2 *wtitle = g_utf8_to_utf16("Workrave", -1, nullptr, nullptr, nullptr);

  current_id = id;

  if (winfo != nullptr && wtitle != nullptr)
    {
      nid.uFlags |= NIF_INFO;
      nid.uTimeout = 20000;
      nid.dwInfoFlags = NIIF_INFO;

      wcsncpy(nid.szInfo, (wchar_t *)winfo, G_N_ELEMENTS(nid.szInfo) - 1);
      nid.szInfo[G_N_ELEMENTS(nid.szInfo) - 1] = 0;

      wcsncpy(nid.szInfoTitle, (wchar_t *)wtitle, G_N_ELEMENTS(nid.szInfoTitle) - 1);
      nid.szInfoTitle[G_N_ELEMENTS(nid.szInfoTitle) - 1] = 0;

      if (nid.hWnd != nullptr && visible)
        {
          Shell_NotifyIconW(NIM_MODIFY, &nid);
        }

      nid.uFlags &= ~NIF_INFO;
    }

  if (winfo != nullptr)
    {
      g_free(winfo);
    }
  if (wtitle != nullptr)
    {
      g_free(wtitle);
    }
}

void
WindowsStatusIcon::set_visible(bool visible)
{
  if (this->visible != visible)
    {
      this->visible = visible;
      if (nid.hWnd != nullptr)
        {
          Shell_NotifyIconW(visible ? NIM_ADD : NIM_DELETE, &nid);
        }
    }
}

bool
WindowsStatusIcon::get_visible() const
{
  return visible;
}

bool
WindowsStatusIcon::is_embedded() const
{
  return true;
}

sigc::signal<void>
WindowsStatusIcon::signal_activate()
{
  return activate_signal;
}

sigc::signal<void, string>
WindowsStatusIcon::signal_balloon_activate()
{
  return balloon_activate_signal;
}

void
WindowsStatusIcon::init()
{
  HINSTANCE hinstance = GetModuleHandle(nullptr);

  WNDCLASSA wclass;
  memset(&wclass, 0, sizeof(WNDCLASS));
  wclass.lpszClassName = "WorkraveTrayObserver";
  wclass.lpfnWndProc = window_proc;
  wclass.hInstance = hinstance;

  ATOM atom = RegisterClassA(&wclass);
  if (atom != 0)
    {
      tray_hwnd = CreateWindow(MAKEINTRESOURCE(atom), nullptr, WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, hinstance, nullptr);
    }

  if (tray_hwnd == nullptr)
    {
      UnregisterClass(MAKEINTRESOURCE(atom), hinstance);
    }
  else
    {
      SetWindowLongPtr(tray_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
      wm_taskbarcreated = RegisterWindowMessageA("TaskbarCreated");
    }

  memset(&nid, 0, sizeof(NOTIFYICONDATA));
  nid.cbSize = NOTIFYICONDATAW_V2_SIZE;
  nid.uID = 1;
  nid.uFlags = NIF_MESSAGE;
  nid.uCallbackMessage = MYWM_TRAY_MESSAGE;
  nid.hWnd = tray_hwnd;

  set_tooltip("Workrave");
}

void
WindowsStatusIcon::cleanup()
{
  if (nid.hWnd != nullptr && visible)
    {
      Shell_NotifyIconW(NIM_DELETE, &nid);
      if (nid.hIcon)
        {
          DestroyIcon(nid.hIcon);
        }
    }
}

void
WindowsStatusIcon::show_menu()
{
  POINT pt = {0};
  GetCursorPos(&pt);

  HMENU menu = CreatePopupMenu();
  init_menu(menu, 0, menu_model->get_root());

  SetForegroundWindow(nid.hWnd);
  UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, NULL, nid.hWnd, nullptr);
  DestroyMenu(menu);
  auto node = menu_helper.find_node(command);
  if (node)
    {
      node->activate();
    }
}

static std::wstring
ConvertAnsiToWide(const std::string &str)
{
  int count = MultiByteToWideChar(CP_ACP, 0, str.c_str(), str.length(), nullptr, 0);
  std::wstring wstr(count, 0);
  MultiByteToWideChar(CP_ACP, 0, str.c_str(), str.length(), &wstr[0], count);
  return wstr;
}

void
WindowsStatusIcon::init_menu(HMENU current_menu, int level, menus::Node::Ptr node)
{
  uint32_t command = menu_helper.allocate_command(node->get_id());

  std::wstring text = ConvertAnsiToWide(node->get_dynamic_text());
  std::replace(text.begin(), text.end(), '_', '&');

  UINT flags = MF_STRING | MF_BYPOSITION;

  if (auto n = std::dynamic_pointer_cast<menus::SubMenuNode>(node); n)
    {
      HMENU popup{nullptr};
      if (level > 0)
        {
          popup = CreatePopupMenu();
          InsertMenuW(current_menu, -1, MF_POPUP | flags, (UINT_PTR)popup, text.c_str());
        }
      else
        {
          popup = current_menu;
        }

      for (auto &menu_to_add: n->get_children())
        {
          init_menu(popup, level + 1, menu_to_add);
        }
    }

  else if (auto n = std::dynamic_pointer_cast<menus::RadioGroupNode>(node); n)
    {
      for (auto &menu_to_add: n->get_children())
        {
          init_menu(current_menu, level + 1, menu_to_add);
        }
    }

  else if (auto n = std::dynamic_pointer_cast<menus::ActionNode>(node); n)
    {
      InsertMenuW(current_menu, -1, flags, (UINT_PTR)(command), text.c_str());
    }

  else if (auto n = std::dynamic_pointer_cast<menus::ToggleNode>(node); n)
    {
      if (n->is_checked())
        {
          flags |= MF_CHECKED;
        }
      InsertMenuW(current_menu, -1, flags, (UINT_PTR)(command), text.c_str());
    }

  else if (auto n = std::dynamic_pointer_cast<menus::RadioNode>(node); n)
    {
      if (n->is_checked())
        {
          flags |= MF_CHECKED;
        }
      InsertMenuW(current_menu, -1, flags, (UINT_PTR)(command), text.c_str());
    }

  else if (auto n = std::dynamic_pointer_cast<menus::SeparatorNode>(node); n)
    {
      InsertMenuW(current_menu, -1, MF_SEPARATOR | flags, (UINT_PTR)(command), text.c_str());
    }
}

LRESULT CALLBACK
WindowsStatusIcon::window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  TRACE_ENTRY_PAR(uMsg, wParam);
  auto *status_icon = (WindowsStatusIcon *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
  if (status_icon != nullptr)
    {
      if (uMsg == status_icon->wm_taskbarcreated)
        {
          if (status_icon->visible && status_icon->nid.hWnd != nullptr)
            {
              Shell_NotifyIconW(NIM_ADD, &status_icon->nid);
            }
        }
      else if (uMsg == MYWM_TRAY_MESSAGE)
        {
          switch (lParam)
            {
            case WM_RBUTTONDOWN:
              status_icon->show_menu();
              break;
            case WM_LBUTTONDOWN:
              status_icon->activate_signal.emit();
              break;
            case NIN_BALLOONUSERCLICK:
              status_icon->balloon_activate_signal.emit(status_icon->current_id);
            }
        }
    }

  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
