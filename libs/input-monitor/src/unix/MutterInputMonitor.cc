// Copyright (C) 2016 Rob Caelers <robc@krandor.nl>
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
#include "config.h"
#endif

#include "MutterInputMonitor.hh"

#include "debug.hh"

using namespace std;

MutterInputMonitor::MutterInputMonitor()
= default;

MutterInputMonitor::~MutterInputMonitor()
{
  if (monitor_thread)
    {
      monitor_thread->join();
    }
}

bool
MutterInputMonitor::init()
{
  TRACE_ENTER("MutterInputMonitor::init");

  bool result = init_idle_monitor();

  if (result)
    {
      init_inhibitors();
    }

  TRACE_EXIT();
  return result;
}

bool
MutterInputMonitor::init_idle_monitor()
{
  TRACE_ENTER("MutterInputMonitor::init");
  GError *error = nullptr;
  bool result = true;

  idle_proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION,
                                             G_DBUS_PROXY_FLAGS_NONE,
                                             nullptr,
                                             "org.gnome.Mutter.IdleMonitor",
                                             "/org/gnome/Mutter/IdleMonitor/Core",
                                             "org.gnome.Mutter.IdleMonitor",
                                             nullptr,
                                             &error);
  if (error == nullptr)
    {
      g_signal_connect(idle_proxy, "g-signal", G_CALLBACK(on_idle_monitor_signal), this);

      result = register_active_watch();
      result = result && register_idle_watch();

      if (result)
        {
          monitor_thread = std::shared_ptr<boost::thread>(new boost::thread(std::bind(&MutterInputMonitor::run, this)));
        }
    }
  else
    {
      TRACE_MSG("Error: " << error->message);
      g_error_free(error);
      result = false;
    }

  TRACE_EXIT();
  return result;
}

void
MutterInputMonitor::init_inhibitors()
{
  TRACE_ENTER("MutterInputMonitor::monitor_inhibitors");

  session_proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SESSION,
                                                G_DBUS_PROXY_FLAGS_NONE,
                                                nullptr,
                                                "org.gnome.SessionManager",
                                                "/org/gnome/SessionManager",
                                                "org.gnome.SessionManager",
                                                nullptr,
                                                nullptr);
  if (session_proxy != nullptr)
    {
      g_signal_connect(session_proxy, "g-properties-changed", G_CALLBACK(on_session_manager_property_changed), this);

      GVariant *v = g_dbus_proxy_get_cached_property(session_proxy, "InhibitedActions");
      inhibited = (g_variant_get_uint32(v) & GSM_INHIBITOR_FLAG_IDLE) != 0;
      TRACE_MSG("Inhibited:" << g_variant_get_uint32(v) << " " << inhibited);
      g_variant_unref(v);
    }
  TRACE_EXIT();
}

bool
MutterInputMonitor::register_active_watch()
{
  TRACE_ENTER("MutterInputMonitor::register_active_watch");
  GError *error = nullptr;
  GVariant *reply = g_dbus_proxy_call_sync(idle_proxy, "AddUserActiveWatch", nullptr, G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &error);

  if (error == nullptr)
    {
      g_variant_get(reply, "(u)", &watch_active);
      g_variant_unref(reply);
    }
  else
    {
      TRACE_MSG("Error: " << error->message);
      g_error_free(error);
    }
  TRACE_EXIT();
  return error == nullptr;
}

void
MutterInputMonitor::register_active_watch_async()
{
  TRACE_ENTER("MutterInputMonitor::register_active_watch_aync");
  g_dbus_proxy_call(idle_proxy, "AddUserActiveWatch", nullptr, G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, on_register_active_watch_reply, this);
  TRACE_EXIT();
}

void
MutterInputMonitor::on_register_active_watch_reply(GObject *object, GAsyncResult *res, gpointer user_data)
{
  TRACE_ENTER("MutterInputMonitor::on_register_active_watch_reply");
  GError *error = nullptr;
  GDBusProxy *proxy = G_DBUS_PROXY(object);
  MutterInputMonitor *self = (MutterInputMonitor *)user_data;

  GVariant *params = g_dbus_proxy_call_finish(proxy, res, &error);
  if (error)
    {
      TRACE_MSG("Error: " << error->message);
      g_clear_error(&error);
      return;
    }

  g_variant_get(params, "(u)", &self->watch_active);
  g_variant_unref(params);
}

bool
MutterInputMonitor::unregister_active_watch()
{
  TRACE_ENTER("MutterInputMonitor::unregister_active_watch");
  GError *error = nullptr;
  if (watch_active != 0)
    {
      GVariant *result = g_dbus_proxy_call_sync(idle_proxy, "RemoveWatch", g_variant_new("(u)", watch_active), G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &error);
      if (error == nullptr)
        {
          g_variant_unref(result);
          watch_active = 0;
        }
      else
        {
          TRACE_MSG("Error: " << error->message);
          g_error_free(error);
        }
    }
  TRACE_EXIT();
  return error == nullptr;
}

void
MutterInputMonitor::unregister_active_watch_async()
{
  TRACE_ENTER("MutterInputMonitor::unregister_active_watch_async");
  if (watch_active != 0)
    {
      g_dbus_proxy_call(idle_proxy, "RemoveWatch", g_variant_new("(u)", watch_active), G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, on_unregister_active_watch_reply, this);
    }
  TRACE_EXIT();
}

void
MutterInputMonitor::on_unregister_active_watch_reply(GObject *object, GAsyncResult *res, gpointer user_data)
{
  TRACE_ENTER("MutterInputMonitor::on_unregister_active_watch_reply");
  GError *error = nullptr;
  GDBusProxy *proxy = G_DBUS_PROXY(object);
  MutterInputMonitor *self = (MutterInputMonitor *)user_data;

  g_dbus_proxy_call_finish(proxy, res, &error);
  if (error)
    {
      TRACE_MSG("Error: " << error->message);
      g_clear_error(&error);
      return;
    }
  self->watch_active = 0;
  TRACE_EXIT();
}

bool
MutterInputMonitor::register_idle_watch()
{
  TRACE_ENTER("MutterInputMonitor::register_idle_watch");
  GError *error = nullptr;
  GVariant *reply = g_dbus_proxy_call_sync(idle_proxy, "AddIdleWatch", g_variant_new("(t)", 500), G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &error);

  if (error == nullptr)
    {
      g_variant_get(reply, "(u)", &watch_idle);
      g_variant_unref(reply);
    }
  else
    {
      TRACE_MSG("Error: " << error->message);
      g_error_free(error);
    }
  TRACE_EXIT();
  return error == nullptr;
}

bool
MutterInputMonitor::unregister_idle_watch()
{
  TRACE_ENTER("MutterInputMonitor::unregister_idle_watch");
  GError *error = nullptr;
  if (watch_idle != 0)
    {
      GVariant *result = g_dbus_proxy_call_sync(idle_proxy, "RemoveWatch", g_variant_new("(u)", watch_idle), G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &error);
      if (error == nullptr)
        {
          g_variant_unref(result);
          watch_idle = 0;
        }
      else
        {
          TRACE_MSG("Error: " << error->message);
          g_error_free(error);
        }
    }
  TRACE_EXIT();
  return error == nullptr;
}

void
MutterInputMonitor::terminate()
{
  unregister_idle_watch();
  unregister_active_watch();

  mutex.lock();
  abort = true;
  cond.notify_all();
  mutex.unlock();

  monitor_thread->join();
}

void
MutterInputMonitor::on_idle_monitor_signal(GDBusProxy *proxy, gchar *sender_name, gchar *signal_name, GVariant *parameters, gpointer user_data)
{
  (void)proxy;
  (void)sender_name;

  MutterInputMonitor *self = (MutterInputMonitor *)user_data;

  if (g_strcmp0(signal_name, "WatchFired") == 0)
    {
      guint handlerID;
      g_variant_get(parameters, "(u)", &handlerID);

      if (handlerID == self->watch_active)
        {
          self->unregister_active_watch_async();
          self->active = true;
        }
      else if (handlerID == self->watch_idle)
        {
          self->register_active_watch_async();
          self->active = false;
        }
    }
}

void
MutterInputMonitor::on_session_manager_property_changed(GDBusProxy *session, GVariant *changed, char **invalidated, gpointer user_data)
{
  TRACE_ENTER("MutterInputMonitor::on_session_manager_property_changed");
  (void) session;
  (void) invalidated;

  MutterInputMonitor *self = (MutterInputMonitor *)user_data;

  GVariant *v = g_variant_lookup_value(changed, "InhibitedActions", G_VARIANT_TYPE_UINT32);
  if (v != nullptr)
    {
      self->inhibited = g_variant_get_uint32(v) & GSM_INHIBITOR_FLAG_IDLE;
      TRACE_MSG("Inhibited:" << g_variant_get_uint32(v));
      g_variant_unref(v);
    }
  TRACE_EXIT();
}

void
MutterInputMonitor::run()
{
  TRACE_ENTER("MutterInputMonitor::run");

  {
    boost::mutex::scoped_lock lock(mutex);

    while (!abort)
      {
        bool local_active = active;

        if (inhibited)
          {
            GError *error = nullptr;
            guint64 idletime;
            GVariant *reply = g_dbus_proxy_call_sync(idle_proxy, "GetIdletime", nullptr, G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &error);
            if (error == nullptr)
              {

                g_variant_get(reply, "(t)", &idletime);
                g_variant_unref(reply);
                local_active = idletime < 1000;
              }
            else
              {
                TRACE_MSG("Error: " << error->message);
                g_error_free(error);
              }
          }

        if (local_active)
          {
            /* Notify the activity monitor */
            fire_action();
          }

        boost::system_time timeout = boost::get_system_time()+ boost::posix_time::milliseconds(1000);
        cond.timed_wait(lock, timeout);
      }
  }

  TRACE_EXIT();
}
