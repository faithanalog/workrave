// Copyright (C) 2010, 2011 Rob Caelers <robc@krandor.nl>
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

#ifndef WINDOWSSTATUSICON_HH
#define WINDOWSSTATUSICON_HH

#include "config.h"

#include <string>

#include <glibmm.h>

#include <windows.h>
#include <commctrl.h>

#include "ui/IPlugin.hh"
#include "ui/MenuModel.hh"
#include "ui/MenuHelper.hh"
#include "utils/Signals.hh"

class WindowsStatusIcon
{
public:
  explicit WindowsStatusIcon(std::shared_ptr<IApplication> app);
  virtual ~WindowsStatusIcon();

  void set_tooltip(const Glib::ustring &text);
  void show_balloon(std::string id, const Glib::ustring &balloon);
  void set_visible(bool visible = true);
  bool get_visible() const;
  bool is_embedded() const;

  sigc::signal<void> signal_activate();
  sigc::signal<void, std::string> signal_balloon_activate();

private:
  void init();
  void cleanup();
  void show_menu();
  void init_menu(HMENU current_menu, int level, menus::Node::Ptr node);

  static LRESULT CALLBACK window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
  void set_operation_mode(workrave::OperationMode m);

private:
  std::shared_ptr<IToolkit> toolkit;
  MenuModel::Ptr menu_model;
  MenuHelper menu_helper;

  std::string current_id;
  bool visible{false};
  NOTIFYICONDATAW nid{};
  HWND tray_hwnd{nullptr};
  UINT wm_taskbarcreated{0};

  sigc::signal<void> activate_signal;
  sigc::signal<void, std::string> balloon_activate_signal;

  workrave::utils::Trackable tracker;
};

#endif // WINDOWSSTATUSICON_HH
