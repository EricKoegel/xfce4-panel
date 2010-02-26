/*
 * Copyright (C) 2008-2009 Nick Schermer <nick@xfce.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <common/panel-private.h>
#include <common/panel-dbus.h>
#include <libxfce4util/libxfce4util.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4panel/libxfce4panel.h>

#include <panel/panel-dbus-service.h>
#include <panel/panel-application.h>
#include <panel/panel-preferences-dialog.h>
#include <panel/panel-item-dialog.h>
#include <panel/panel-module-factory.h>



static void      panel_dbus_service_finalize                   (GObject            *object);
static gboolean  panel_dbus_service_display_preferences_dialog (PanelDBusService   *service,
                                                                guint               active,
                                                                GError            **error);
static gboolean  panel_dbus_service_display_items_dialog       (PanelDBusService   *service,
                                                                guint               active,
                                                                GError            **error);
static gboolean  panel_dbus_service_save                       (PanelDBusService   *service,
                                                                GError            **error);
static gboolean  panel_dbus_service_add_new_item               (PanelDBusService   *service,
                                                                const gchar        *plugin_name,
                                                                gchar             **arguments,
                                                                GError            **error);
static gboolean  panel_dbus_service_plugin_event               (PanelDBusService  *service,
                                                                const gchar       *plugin_name,
                                                                const gchar       *name,
                                                                const GValue      *value,
                                                                GError           **error);
static gboolean  panel_dbus_service_terminate                  (PanelDBusService   *service,
                                                                gboolean            restart,
                                                                GError            **error);



/* include the dbus glue generated by dbus-binding-tool */
#include <panel/panel-dbus-service-infos.h>



struct _PanelDBusServiceClass
{
  GObjectClass __parent__;
};

struct _PanelDBusService
{
  GObject __parent__;

  /* the dbus connection */
  DBusGConnection *connection;
};



static gboolean dbus_exit_restart = FALSE;



G_DEFINE_TYPE (PanelDBusService, panel_dbus_service, G_TYPE_OBJECT)



static void
panel_dbus_service_class_init (PanelDBusServiceClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = panel_dbus_service_finalize;

  dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (klass),
      &dbus_glib_panel_dbus_service_object_info);
}



static void
panel_dbus_service_init (PanelDBusService *service)
{
  GError         *error = NULL;
  DBusConnection *connection;

  service->connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
  if (G_LIKELY (service->connection != NULL))
    {
      /* TODO handle error */
      connection = dbus_g_connection_get_connection (service->connection);
      dbus_bus_request_name (connection, PANEL_DBUS_NAME, 0, NULL);

      /* register this object */
      dbus_g_connection_register_g_object (service->connection,
                                           PANEL_DBUS_PATH,
                                           G_OBJECT (service));
    }
  else
    {
      /* print warning */
      g_warning ("Failed to connect to the D-BUS session bus: %s", error->message);

      /* cleanup */
      g_error_free (error);
    }
}



static void
panel_dbus_service_finalize (GObject *object)
{
  PanelDBusService *service = PANEL_DBUS_SERVICE (object);
  DBusConnection   *connection;

  if (G_LIKELY (service->connection != NULL))
    {
      /* release the org.xfce.Panel name */
      connection = dbus_g_connection_get_connection (service->connection);
      dbus_bus_release_name (connection, PANEL_DBUS_NAME, NULL);
      dbus_g_connection_flush (service->connection);

      /* release the connection */
      dbus_g_connection_unref (service->connection);
    }

  (*G_OBJECT_CLASS (panel_dbus_service_parent_class)->finalize) (object);
}



static gboolean
panel_dbus_service_display_preferences_dialog (PanelDBusService  *service,
                                               guint              active,
                                               GError           **error)
{
  PanelApplication *application;

  panel_return_val_if_fail (PANEL_IS_DBUS_SERVICE (service), FALSE);
  panel_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* show the preferences dialog */
  application = panel_application_get ();
  panel_preferences_dialog_show (panel_application_get_window (application, active));
  g_object_unref (G_OBJECT (application));

  return TRUE;
}



static gboolean
panel_dbus_service_display_items_dialog (PanelDBusService  *service,
                                         guint              active,
                                         GError           **error)
{
  PanelApplication *application;

  panel_return_val_if_fail (PANEL_IS_DBUS_SERVICE (service), FALSE);
  panel_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* show the items dialog */
  application = panel_application_get ();
  panel_application_window_select (application, (gint) active);
  panel_item_dialog_show (NULL);
  g_object_unref (G_OBJECT (application));

  return TRUE;
}



static gboolean
panel_dbus_service_save (PanelDBusService  *service,
                         GError           **error)
{
  PanelApplication *application;

  panel_return_val_if_fail (PANEL_IS_DBUS_SERVICE (service), FALSE);
  panel_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* save the configuration */
  application = panel_application_get ();
  panel_application_save (application, TRUE);
  g_object_unref (G_OBJECT (application));

  return TRUE;
}



static gboolean
panel_dbus_service_add_new_item (PanelDBusService  *service,
                                 const gchar       *plugin_name,
                                 gchar            **arguments,
                                 GError           **error)
{
  PanelApplication *application;

  panel_return_val_if_fail (PANEL_IS_DBUS_SERVICE (service), FALSE);
  panel_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  panel_return_val_if_fail (plugin_name != NULL, FALSE);

  application = panel_application_get ();

  /* save the configuration */
  if (arguments && *arguments != NULL)
    panel_application_add_new_item (application, plugin_name, arguments);
  else
    panel_application_add_new_item (application, plugin_name, NULL);

  g_object_unref (G_OBJECT (application));

  return TRUE;
}



static gboolean
panel_dbus_service_plugin_event (PanelDBusService  *service,
                                 const gchar       *plugin_name,
                                 const gchar       *name,
                                 const GValue      *value,
                                 GError           **error)
{
  GSList             *plugins, *li;
  PanelModuleFactory *factory;

  panel_return_val_if_fail (PANEL_IS_DBUS_SERVICE (service), FALSE);
  panel_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  panel_return_val_if_fail (plugin_name != NULL, FALSE);
  panel_return_val_if_fail (name != NULL, FALSE);
  panel_return_val_if_fail (G_IS_VALUE (value), FALSE);

  factory = panel_module_factory_get ();

  plugins = panel_module_factory_get_plugins (factory, plugin_name);

  for (li = plugins; li != NULL; li = li->next)
    {
      panel_return_val_if_fail (XFCE_IS_PANEL_PLUGIN_PROVIDER (li->data), FALSE);
      if (xfce_panel_plugin_provider_remote_event (li->data, name, value))
        break;
    }

  g_slist_free (plugins);

  g_object_unref (G_OBJECT (factory));

  return TRUE;
}



static gboolean
panel_dbus_service_terminate (PanelDBusService  *service,
                              gboolean           restart,
                              GError           **error)
{
  panel_return_val_if_fail (PANEL_IS_DBUS_SERVICE (service), FALSE);
  panel_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  panel_dbus_service_exit_panel (restart);

  return TRUE;
}



PanelDBusService *
panel_dbus_service_get (void)
{
  static PanelDBusService *service = NULL;

  if (G_LIKELY (service))
    {
      g_object_ref (G_OBJECT (service));
    }
  else
    {
      service = g_object_new (PANEL_TYPE_DBUS_SERVICE, NULL);
      g_object_add_weak_pointer (G_OBJECT (service), (gpointer) &service);
    }

  return service;
}



void
panel_dbus_service_exit_panel (gboolean restart)
{
  XfceSMClient *sm_client;

  sm_client = xfce_sm_client_get ();
  xfce_sm_client_set_restart_style (sm_client, XFCE_SM_CLIENT_RESTART_NORMAL);

  dbus_exit_restart = !!restart;

  gtk_main_quit ();
}



gboolean
panel_dbus_service_get_restart (void)
{
  return dbus_exit_restart;
}
