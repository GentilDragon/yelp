/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Copyright (C) 2009-2020 Shaun McCance <shaunm@gnome.org>
 * Copyright (C) 2014 Igalia S.L.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Shaun McCance <shaunm@gnome.org>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <adwaita.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <math.h>
#include <webkit/webkit.h>

#include "yelp-debug.h"
#include "yelp-docbook-document.h"
#include "yelp-error.h"
#include "yelp-marshal.h"
#include "yelp-settings.h"
#include "yelp-types.h"
#include "yelp-view.h"
#include "yelp-uri-builder.h"

static void        yelp_view_dispose                 (GObject            *object);
static void        yelp_view_finalize                (GObject            *object);
static void        yelp_view_get_property            (GObject            *object,
                                                      guint               prop_id,
                                                      GValue             *value,
                                                      GParamSpec         *pspec);
static void        yelp_view_set_property            (GObject            *object,
                                                      guint               prop_id,
                                                      const GValue       *value,
                                                      GParamSpec         *pspec);
static void        yelp_view_resolve_uri             (YelpView           *view,
                                                      YelpUri            *uri);

static gboolean    view_external_uri                 (YelpView           *view,
                                                      YelpUri            *uri);
static void        view_install_uri                  (YelpView           *view,
                                                      const gchar        *uri);
static void        popup_open_link                   (GSimpleAction      *action,
                                                      GVariant           *parameter,
                                                      gpointer            user_data);
static void        popup_open_link_new               (GSimpleAction      *action,
                                                      GVariant           *parameter,
                                                      gpointer            user_data);
static void        popup_copy_link                   (GSimpleAction      *action,
                                                      GVariant           *parameter,
                                                      gpointer            user_data);
static void        popup_save_image                  (GSimpleAction      *action,
                                                      GVariant           *parameter,
                                                      gpointer            user_data);
static void        popup_send_image                  (GSimpleAction      *action,
                                                      GVariant           *parameter,
                                                      gpointer            user_data);
static void        popup_copy_code                   (GSimpleAction      *action,
                                                      GVariant           *parameter,
                                                      gpointer            user_data);
static void        popup_save_code                   (GSimpleAction      *action,
                                                      GVariant           *parameter,
                                                      gpointer            user_data);
static void        popup_copy_clipboard              (GSimpleAction      *action,
                                                      GVariant           *parameter,
                                                      gpointer            user_data);
static gboolean    view_populate_context_menu        (YelpView            *view,
                                                      WebKitContextMenu   *context_menu,
                                                      WebKitHitTestResult *hit_test_result,
                                                      gpointer             user_data);
static gboolean    view_script_dialog                (YelpView           *view,
                                                      WebKitScriptDialog *dialog,
                                                      gpointer            data);
static gboolean    view_policy_decision_requested    (YelpView                *view,
                                                      WebKitPolicyDecision    *decision,
                                                      WebKitPolicyDecisionType type,
                                                      gpointer                 user_data);
static void        view_print_action                 (GAction            *action,
                                                      GVariant           *parameter,
                                                      YelpView           *view);
static void        view_history_action               (GAction            *action,
                                                      GVariant           *parameter,
                                                      YelpView           *view);
static void        view_history_changed              (YelpView           *view);
static void        view_navigation_action            (GAction            *action,
                                                      GVariant           *parameter,
                                                      YelpView           *view);
static void        yelp_view_history_action          (GAction            *action,
                                                      GVariant           *parameter,
                                                      YelpView           *view);

static void yelp_view_show_history_popup(YelpView *view);

static void hist_button_init(GtkWidget *button);

static void        view_clear_load                   (YelpView           *view);
static void        view_load_page                    (YelpView           *view);
static void        view_show_error_page              (YelpView           *view,
                                                      GError             *error);

static void        settings_set_fonts                (YelpSettings       *settings,
                                                      gpointer            user_data);
static void        settings_set_scale                (YelpSettings       *settings,
                                                      GParamSpec         *spec,
                                                      gpointer            user_data);
static void        settings_show_text_cursor         (YelpSettings       *settings);

static void        uri_resolved                      (YelpUri            *uri,
                                                      YelpView           *view);
static void        yelp_view_button_press            (GtkGestureClick    *event,
                                                      gint                n_press,
                                                      gdouble             x,
                                                      gdouble             y,
                                                      YelpView           *view);
static void        yelp_view_register_custom_schemes (void);
static void        view_load_failed                  (WebKitWebView      *web_view,
                                                      WebKitLoadEvent     load_event,
                                                      gchar              *failing_uri,
                                                      GError             *error,
                                                      gpointer            user_data);
static void        view_load_status_changed          (WebKitWebView      *view,
                                                      WebKitLoadEvent     load_event,
                                                      gpointer            user_data);
static void        yelp_view_register_extensions     (void);

static gchar *nautilus_sendto = NULL;

enum {
    PROP_0,
    PROP_URI,
    PROP_STATE,
    PROP_PAGE_ID,
    PROP_ROOT_TITLE,
    PROP_PAGE_TITLE,
    PROP_PAGE_DESC,
    PROP_PAGE_ICON
};

enum {
    NEW_VIEW_REQUESTED,
    EXTERNAL_URI,
    LOADED,
    LAST_SIGNAL
};
static gint signals[LAST_SIGNAL] = { 0 };

static WebKitSettings *
yelp_view_get_global_settings (void)
{
    static WebKitSettings *websettings = NULL;

    if (!websettings)
        websettings = webkit_settings_new_with_settings ("default-charset", "utf-8",
#if WEBKIT_CHECK_VERSION(2, 23, 4)
                                                         "enable-back-forward-navigation-gestures", TRUE,
#endif
                                                         NULL);

    return websettings;
}

typedef struct _RequestAsyncData RequestAsyncData;
struct _RequestAsyncData {
    WebKitURISchemeRequest *request;
    GFile *resource_file;
    gchar *page_id;
    gboolean finished;
};

static RequestAsyncData *
request_async_data_new (WebKitURISchemeRequest *request, gchar *page_id)
{
    RequestAsyncData *data;

    data = g_slice_new0 (RequestAsyncData);
    data->request = g_object_ref (request);
    data->page_id = g_strdup (page_id);
    data->finished = FALSE;
    return data;
}

static void
request_async_data_free (RequestAsyncData *data)
{
    g_object_unref (data->request);
    g_clear_pointer (&data->page_id, g_free);
    g_slice_free (RequestAsyncData, data);
}

typedef struct _YelpViewPrivate YelpViewPrivate;
struct _YelpViewPrivate {
    YelpUri       *uri;
    YelpUri       *resolve_uri;
    gulong         uri_resolved;
    YelpDocument  *document;
    GCancellable  *cancellable;
    gulong         fonts_changed;

    gchar         *popup_link_uri;
    gchar         *popup_link_text;
    gchar         *popup_image_uri;
    gchar         *popup_code_text;
    gchar         *popup_code_title;

    GtkGesture    *gesture_click;

    YelpViewState  state;
    YelpViewState  prevstate;

    gchar         *page_id;
    gchar         *root_title;
    gchar         *page_title;
    gchar         *page_desc;
    gchar         *page_icon;

    GSimpleAction  *print_action;
    GSimpleAction  *back_action;
    GSimpleAction  *forward_action;
    GSimpleAction  *prev_action;
    GSimpleAction  *next_action;
    GSimpleAction  *history_action;

    GSimpleActionGroup *popup_actions;

    GtkWidget      *history_scrim;
    GtkWidget      *history_panel;
    guint           history_close_timeout_id;

    gboolean        resolve_uri_on_policy_decision;
    gboolean        load_page_after_resolved;
};

#define TARGET_TYPE_URI_LIST     "text/uri-list"
enum {
    TARGET_URI_LIST
};

G_DEFINE_TYPE_WITH_PRIVATE (YelpView, yelp_view, WEBKIT_TYPE_WEB_VIEW)

static void
yelp_view_init (YelpView *view)
{
    static const GActionEntry popup_action_entries[] = {
      { "CopyCode", popup_copy_code, NULL, NULL, NULL },
      { "CopyLink", popup_copy_link, NULL, NULL, NULL },
      { "OpenLink", popup_open_link, NULL, NULL, NULL },
      { "OpenLinkNew", popup_open_link_new, NULL, NULL, NULL },
      { "SendEmail", popup_open_link, NULL, NULL, NULL },
      { "InstallPackages", popup_open_link, NULL, NULL, NULL },
      { "SaveCode", popup_save_code, NULL, NULL, NULL },
      { "SaveMedia", popup_save_image, NULL, NULL, NULL },
      { "SendMedia", popup_send_image, NULL, NULL, NULL },
      { "CopyText", popup_copy_clipboard, NULL, NULL, NULL }
    };

    YelpViewPrivate *priv = yelp_view_get_instance_private (view);

    priv->cancellable = NULL;

    priv->prevstate = priv->state = YELP_VIEW_STATE_BLANK;

    priv->resolve_uri_on_policy_decision = TRUE;
    priv->history_close_timeout_id = 0;
    g_signal_connect (view, "decide-policy",
                      G_CALLBACK (view_policy_decision_requested), NULL);
    g_signal_connect (view, "load-changed",
                      G_CALLBACK (view_load_status_changed), NULL);
    g_signal_connect (view, "load-failed",
                      G_CALLBACK (view_load_failed), NULL);
    /* Disabled to allow WebKit default context menu */

    /* g_signal_connect (view, "context-menu",
                      G_CALLBACK (view_populate_context_menu), NULL); */
    g_signal_connect (view, "script-dialog",
                      G_CALLBACK (view_script_dialog), NULL);

    priv->gesture_click = gtk_gesture_click_new ();
    g_signal_connect (priv->gesture_click, "pressed", G_CALLBACK (yelp_view_button_press), view);
    gtk_widget_add_controller (GTK_WIDGET (view), GTK_EVENT_CONTROLLER (priv->gesture_click));


    priv->popup_actions = g_simple_action_group_new ();
    g_action_map_add_action_entries (G_ACTION_MAP (priv->popup_actions),
                                     popup_action_entries,
                                     G_N_ELEMENTS (popup_action_entries),
                                     view);

    priv->print_action = g_simple_action_new ("yelp-view-print", NULL);
    g_signal_connect (priv->print_action,
                      "activate",
                      G_CALLBACK (view_print_action),
                      view);

    priv->back_action = g_simple_action_new ("yelp-view-go-back", NULL);
    g_simple_action_set_enabled (priv->back_action, FALSE);
    g_signal_connect (priv->back_action,
                      "activate",
                      G_CALLBACK (view_history_action),
                      view);

    priv->forward_action = g_simple_action_new ("yelp-view-go-forward", NULL);
    g_simple_action_set_enabled (priv->forward_action, FALSE);
    g_signal_connect (priv->forward_action,
                      "activate",
                      G_CALLBACK (view_history_action),
                      view);

    priv->prev_action = g_simple_action_new ("yelp-view-go-previous", NULL);
    g_simple_action_set_enabled (priv->prev_action, FALSE);
    g_signal_connect (priv->prev_action,
                      "activate",
                      G_CALLBACK (view_navigation_action),
                      view);

    priv->next_action = g_simple_action_new ("yelp-view-go-next", NULL);
    g_simple_action_set_enabled (priv->next_action, FALSE);
    g_signal_connect (priv->next_action,
                      "activate",
                      G_CALLBACK (view_navigation_action),
                      view);

    priv->history_action = g_simple_action_new ("yelp-view-history", NULL);
    g_simple_action_set_enabled (priv->history_action, FALSE);
    g_signal_connect (priv->history_action,
                      "activate",
                      G_CALLBACK (yelp_view_history_action),
                      view);
}

static void
yelp_view_constructed (GObject *object)
{
    YelpView *view = YELP_VIEW (object);
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    YelpSettings *settings = yelp_settings_get_default ();

    G_OBJECT_CLASS (yelp_view_parent_class)->constructed (object);

    g_signal_connect_swapped (webkit_web_view_get_back_forward_list (WEBKIT_WEB_VIEW (view)),
                              "changed",
                              G_CALLBACK (view_history_changed),
                              view);
    view_history_changed (view);

    priv->fonts_changed = g_signal_connect (settings,
                                            "fonts-changed",
                                            G_CALLBACK (settings_set_fonts),
                                            view);

    g_signal_connect (settings,
                      "notify::zoom-level",
                      G_CALLBACK (settings_set_scale),
                      view);

    double zoom_level = yelp_settings_get_zoom_level (settings);
    webkit_web_view_set_zoom_level (WEBKIT_WEB_VIEW (view), zoom_level);

    settings_set_fonts (settings, view);
}

static void
yelp_view_dispose (GObject *object)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (YELP_VIEW (object));

    view_clear_load (YELP_VIEW (object));

    if (priv->fonts_changed > 0) {
        g_signal_handler_disconnect (yelp_settings_get_default (),
                                     priv->fonts_changed);
        priv->fonts_changed = 0;
    }

    if (priv->print_action) {
        g_object_unref (priv->print_action);
        priv->print_action = NULL;
    }

    if (priv->back_action) {
        g_object_unref (priv->back_action);
        priv->back_action = NULL;
    }

    if (priv->forward_action) {
        g_object_unref (priv->forward_action);
        priv->forward_action = NULL;
    }

    if (priv->prev_action) {
        g_object_unref (priv->prev_action);
        priv->prev_action = NULL;
    }

    if (priv->next_action) {
        g_object_unref (priv->next_action);
        priv->next_action = NULL;
    }
    if (priv->history_action) {
        g_object_unref (priv->history_action);
        priv->history_action = NULL;
    }

    if (priv->document) {
        g_object_unref (priv->document);
        priv->document = NULL;
    }

    if (priv->popup_actions) {
        g_object_unref (priv->popup_actions);
        priv->popup_actions = NULL;
    }

    G_OBJECT_CLASS (yelp_view_parent_class)->dispose (object);
}

static void
yelp_view_finalize (GObject *object)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (YELP_VIEW (object));

    g_free (priv->popup_link_uri);
    g_free (priv->popup_link_text);
    g_free (priv->popup_image_uri);
    g_free (priv->popup_code_text);
    g_free (priv->popup_code_title);

    g_free (priv->page_id);
    g_free (priv->root_title);
    g_free (priv->page_title);
    g_free (priv->page_desc);
    g_free (priv->page_icon);

    

    G_OBJECT_CLASS (yelp_view_parent_class)->finalize (object);
}

static void
yelp_view_button_press (GtkGestureClick *event,
                        gint             n_press,
                        gdouble          x,
                        gdouble          y,
                        YelpView        *view)
{
    /* Handle typical back/forward mouse buttons. */
    switch (gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (event))) {
        case 8:
            webkit_web_view_go_back (WEBKIT_WEB_VIEW (view));
            break;

        case 9:
            webkit_web_view_go_forward (WEBKIT_WEB_VIEW (view));
            break;

        default:
            break;
    }
}

static void
yelp_view_class_init (YelpViewClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    YelpSettings *settings = yelp_settings_get_default ();

    nautilus_sendto = g_find_program_in_path ("nautilus-sendto");

    g_signal_connect (settings,
                      "notify::show-text-cursor",
                      G_CALLBACK (settings_show_text_cursor),
                      NULL);
    settings_show_text_cursor (settings);

    yelp_view_register_extensions ();
    yelp_view_register_custom_schemes ();

    klass->external_uri = view_external_uri;

    object_class->constructed = yelp_view_constructed;
    object_class->dispose = yelp_view_dispose;
    object_class->finalize = yelp_view_finalize;
    object_class->get_property = yelp_view_get_property;
    object_class->set_property = yelp_view_set_property;

    signals[NEW_VIEW_REQUESTED] =
	g_signal_new ("new-view-requested",
		      G_TYPE_FROM_CLASS (klass),
		      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL,
		      g_cclosure_marshal_VOID__OBJECT,
		      G_TYPE_NONE, 1, YELP_TYPE_URI);

    signals[EXTERNAL_URI] =
	g_signal_new ("external-uri",
		      G_TYPE_FROM_CLASS (klass),
		      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (YelpViewClass, external_uri),
                      g_signal_accumulator_true_handled, NULL,
		      yelp_marshal_BOOLEAN__OBJECT,
		      G_TYPE_BOOLEAN, 1, YELP_TYPE_URI);

    signals[LOADED] =
        g_signal_new ("loaded",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE, 0);

    g_object_class_install_property (object_class,
                                     PROP_URI,
                                     g_param_spec_object ("yelp-uri",
							  "Yelp URI",
							  "A YelpUri with the current location",
                                                          YELP_TYPE_URI,
							  G_PARAM_READWRITE | G_PARAM_STATIC_NAME |
							  G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));

    g_object_class_install_property (object_class,
                                     PROP_STATE,
                                     g_param_spec_enum ("state",
                                                        "Loading State",
                                                        "The loading state of the view",
                                                        YELP_TYPE_VIEW_STATE,
                                                        YELP_VIEW_STATE_BLANK,
                                                        G_PARAM_READWRITE | G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));

    g_object_class_install_property (object_class,
                                     PROP_PAGE_ID,
                                     g_param_spec_string ("page-id",
                                                          "Page ID",
                                                          "The ID of the root page of the page being viewed",
                                                          NULL,
                                                          G_PARAM_READABLE |
                                                          G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));

    g_object_class_install_property (object_class,
                                     PROP_ROOT_TITLE,
                                     g_param_spec_string ("root-title",
                                                          "Root Title",
                                                          "The title of the root page of the page being viewed",
                                                          NULL,
                                                          G_PARAM_READABLE |
                                                          G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));

    g_object_class_install_property (object_class,
                                     PROP_PAGE_TITLE,
                                     g_param_spec_string ("page-title",
                                                          "Page Title",
                                                          "The title of the page being viewed",
                                                          NULL,
                                                          G_PARAM_READABLE |
                                                          G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));

    g_object_class_install_property (object_class,
                                     PROP_PAGE_DESC,
                                     g_param_spec_string ("page-desc",
                                                          "Page Description",
                                                          "The description of the page being viewed",
                                                          NULL,
                                                          G_PARAM_READABLE |
                                                          G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));

    g_object_class_install_property (object_class,
                                     PROP_PAGE_ICON,
                                     g_param_spec_string ("page-icon",
                                                          "Page Icon",
                                                          "The icon of the page being viewed",
                                                          NULL,
                                                          G_PARAM_READABLE |
                                                          G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));
}

static void
yelp_view_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (YELP_VIEW (object));

    switch (prop_id)
        {
        case PROP_URI:
            g_value_set_object (value, priv->uri);
            break;
        case PROP_PAGE_ID:
            g_value_set_string (value, priv->page_id);
            break;
        case PROP_ROOT_TITLE:
            g_value_set_string (value, priv->root_title);
            break;
        case PROP_PAGE_TITLE:
            g_value_set_string (value, priv->page_title);
            break;
        case PROP_PAGE_DESC:
            g_value_set_string (value, priv->page_desc);
            break;
        case PROP_PAGE_ICON:
            if (priv->page_icon)
                g_value_set_string (value, priv->page_icon);
            else
                g_value_set_string (value, "yelp-page-symbolic");
            break;
        case PROP_STATE:
            g_value_set_enum (value, priv->state);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
yelp_view_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
    YelpUri *uri;
    YelpViewPrivate *priv = yelp_view_get_instance_private (YELP_VIEW (object));

    switch (prop_id)
        {
        case PROP_URI:
            uri = g_value_get_object (value);
            yelp_view_load_uri (YELP_VIEW (object), uri);
            g_object_unref (uri);
            break;
        case PROP_STATE:
            priv->prevstate = priv->state;
            priv->state = g_value_get_enum (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

/******************************************************************************/

GtkWidget *
yelp_view_new (void)
{
    return GTK_WIDGET (g_object_new (YELP_TYPE_VIEW,
                       "settings", yelp_view_get_global_settings (), NULL));
}

void
yelp_view_load (YelpView    *view,
                const gchar *uri)
{
    YelpUri *yuri = yelp_uri_new (uri);
    yelp_view_load_uri (view, yuri);
    g_object_unref (yuri);
}

void
yelp_view_load_uri (YelpView *view,
                    YelpUri  *uri)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);

    g_object_set (view, "state", YELP_VIEW_STATE_LOADING, NULL);

    g_simple_action_set_enabled (priv->prev_action, FALSE);
    g_simple_action_set_enabled (priv->next_action, FALSE);

    priv->load_page_after_resolved = TRUE;
    yelp_view_resolve_uri (view, uri);
}

void
yelp_view_load_document (YelpView     *view,
                         YelpUri      *uri,
                         YelpDocument *document)
{
    GParamSpec *spec;
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);

    g_return_if_fail (yelp_uri_is_resolved (uri));

    g_object_set (view, "state", YELP_VIEW_STATE_LOADING, NULL);

    g_object_ref (uri);
    view_clear_load (view);
    priv->uri = uri;
    spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                         "yelp-uri");
    g_signal_emit_by_name (view, "notify::yelp-uri", spec);
    g_object_ref (document);
    if (priv->document)
        g_object_unref (document);
    priv->document = document;

    view_load_page (view);
}

YelpDocument *
yelp_view_get_document (YelpView *view)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    return priv->document;
}

void
yelp_view_register_actions (YelpView   *view,
                            GActionMap *map)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    g_action_map_add_action (map, G_ACTION (priv->print_action));
    g_action_map_add_action (map, G_ACTION (priv->back_action));
    g_action_map_add_action (map, G_ACTION (priv->forward_action));
    g_action_map_add_action (map, G_ACTION (priv->prev_action));
    g_action_map_add_action (map, G_ACTION (priv->next_action));
    g_action_map_add_action (map, G_ACTION (priv->history_action));
}

/******************************************************************************/

static void
yelp_view_resolve_uri (YelpView *view,
                       YelpUri  *uri)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);

    if (yelp_uri_is_resolved (uri)) {
        uri_resolved (uri, view);
        return;
    }

    if (priv->resolve_uri != NULL) {
        if (priv->uri_resolved != 0) {
            g_signal_handler_disconnect (priv->resolve_uri, priv->uri_resolved);
            priv->uri_resolved = 0;
        }
        g_object_unref (priv->resolve_uri);
    }
    priv->resolve_uri = g_object_ref (uri);
    priv->uri_resolved = g_signal_connect (uri, "resolved",
                                           G_CALLBACK (uri_resolved),
                                           view);
    yelp_uri_resolve (uri);
}

static void
document_callback (YelpDocument       *document,
                   YelpDocumentSignal  signal,
                   RequestAsyncData   *data,
                   GError             *error)
{
    const gchar *contents;
    gchar *mime_type;
    GInputStream *stream;
    int content_length;

    if (signal == YELP_DOCUMENT_SIGNAL_INFO)
        return;

    if (data->finished) {
        /* If this is set, this callback is because YelpDocument refreshed
           the page, such as when a file changes. WebKit doesn't like us
           to reuse the request object, so we do a fresh load.
         */
        YelpView *view = YELP_VIEW (webkit_uri_scheme_request_get_web_view (data->request));
        YelpViewPrivate *priv = yelp_view_get_instance_private (view);
        yelp_view_load_uri (view, priv->uri);
        return;
    }

    if (signal == YELP_DOCUMENT_SIGNAL_ERROR) {
        data->finished = TRUE;
        webkit_uri_scheme_request_finish_error (data->request, error);
        return;
    }

    mime_type = yelp_document_get_mime_type (document, data->page_id);

    contents = yelp_document_read_contents (document, data->page_id);

    if (contents) {
        content_length = strlen (contents);

        stream = g_memory_input_stream_new_from_data (g_strdup (contents), content_length, g_free);
        yelp_document_finish_read (document, contents);

        data->finished = TRUE;
        webkit_uri_scheme_request_finish (data->request,
                                          stream,
                                          content_length,
                                          mime_type);
        g_free (mime_type);
        g_object_unref (stream);
    }
}

static void
help_cb_uri_resolved (YelpUri                *uri,
                      WebKitURISchemeRequest *request)
{
    YelpDocument *document;

    if ((document = yelp_document_get_for_uri (uri))) {
        RequestAsyncData *data;
        gchar * page_id;

        page_id = yelp_uri_get_page_id (uri);
        data = request_async_data_new (request, page_id);
        g_free (page_id);

        yelp_document_request_page (document,
                                    data->page_id,
                                    NULL,
                                    (YelpDocumentCallback) document_callback,
                                    data,
                                    (GDestroyNotify) request_async_data_free);
        g_object_unref (document);

    } else {
        YelpUriDocumentType doctype;
        GError *error;
        gchar *struri;

        doctype = yelp_uri_get_document_type (uri);
        if (doctype == YELP_URI_DOCUMENT_TYPE_NOT_FOUND) {
            struri = yelp_uri_get_canonical_uri (uri);
            if (struri) {
                error = g_error_new (YELP_ERROR, YELP_ERROR_NOT_FOUND,
                                     _("The URI ‘%s’ does not point to a valid page."),
                                     struri);
                g_free (struri);
            }
            else {
                error = g_error_new (YELP_ERROR, YELP_ERROR_NOT_FOUND,
                                     _("The URI does not point to a valid page."));
            }
        } else if (doctype == YELP_URI_DOCUMENT_TYPE_ERROR) {
            struri = yelp_uri_get_canonical_uri (uri);
            error = g_error_new (YELP_ERROR, YELP_ERROR_PROCESSING,
                                 _("The URI ‘%s’ could not be parsed."),
                                 struri);
            g_free (struri);
        } else {
            error = g_error_new (YELP_ERROR, YELP_ERROR_UNKNOWN,
                                     _("Unknown Error."));
        }

        webkit_uri_scheme_request_finish_error (request, error);
        g_error_free (error);
    }
}

static void
help_uri_scheme_request_cb  (WebKitURISchemeRequest *request,
                             gpointer                user_data)
{
    YelpUri *uri;
    gchar *uri_str;

    uri_str = build_yelp_uri (webkit_uri_scheme_request_get_uri (request));

    uri = yelp_uri_new (uri_str);
    g_free (uri_str);

    g_signal_connect (uri, "resolved", G_CALLBACK (help_cb_uri_resolved), request);
    yelp_uri_resolve (uri);

    g_object_unref (uri);
}

static const gchar *help_schemes[] = { "help", "ghelp", "gnome-help", "help-list", "info", "man", NULL };

static void
yelp_view_register_custom_schemes (void)
{
    WebKitWebContext *context = webkit_web_context_get_default ();
    WebKitSecurityManager *sec_manager = webkit_web_context_get_security_manager (context);
    gint i;
    gchar *network_scheme;

    for (i = 0; help_schemes[i] != NULL; i++) {
        network_scheme = build_network_scheme (help_schemes[i]);

        webkit_web_context_register_uri_scheme (context, network_scheme,
            (WebKitURISchemeRequestCallback) help_uri_scheme_request_cb,
            NULL, NULL);

        webkit_security_manager_register_uri_scheme_as_local (sec_manager, network_scheme);

        g_free (network_scheme);
    }
}

/******************************************************************************/

YelpUri *
yelp_view_get_active_link_uri (YelpView *view)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    YelpUri *uri;

    uri = yelp_uri_new_relative (priv->uri, priv->popup_link_uri);

    return uri;
}

gchar *
yelp_view_get_active_link_text (YelpView *view)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    return g_strdup (priv->popup_link_text);
}

/******************************************************************************/

static void
yelp_view_register_extensions (void)
{
    WebKitWebContext *context = webkit_web_context_get_default ();

    webkit_web_context_set_web_process_extensions_directory (context, YELP_WEB_PROCESS_EXTENSIONS_DIR);
}

/******************************************************************************/

static gboolean
view_external_uri (YelpView *view,
                   YelpUri  *uri)
{
    gchar *struri = yelp_uri_get_canonical_uri (uri);
    gchar *uri_scheme;
    GAppInfo *app_info = NULL;

    uri_scheme = g_uri_parse_scheme (struri);
    if (uri_scheme && *uri_scheme)
      app_info = g_app_info_get_default_for_uri_scheme (uri_scheme);
    g_free (uri_scheme);

    if (app_info)
      {
        if (!strstr (g_app_info_get_executable (app_info), "yelp") && !strstr (struri, "%3C") && !strstr (struri, "%3E"))
          {
            GList l;

            l.data = struri;
            l.next = l.prev = NULL;
            g_app_info_launch_uris (app_info, &l, NULL, NULL);
          }

        g_object_unref (app_info);
      }
    g_free (struri);
    return TRUE;
}

typedef struct _YelpInstallInfo YelpInstallInfo;
struct _YelpInstallInfo {
    YelpView *view;
    gchar *uri;
};

static void
yelp_install_info_free (YelpInstallInfo *info)
{
    g_object_unref (info->view);
    if (info->uri)
        g_free (info->uri);
    g_free (info);
}

static void
view_install_installed (GDBusConnection *connection,
                        GAsyncResult    *res,
                        YelpInstallInfo *info)
{
    GError *error = NULL;
    g_dbus_connection_call_finish (connection, res, &error);
    if (error) {
        const gchar *err = NULL;
        if (error->domain == G_DBUS_ERROR) {
            if (error->code == G_DBUS_ERROR_SERVICE_UNKNOWN)
                err = _("You do not have PackageKit. Package install links require PackageKit.");
            else
                err = error->message;
        }
        if (err != NULL) {
            AdwDialog *dialog = adw_alert_dialog_new (_("Package Install Link Error"), err);
            adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "close", _("_Close"));
            adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "close");
            adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "close");
            adw_dialog_present (dialog, GTK_WIDGET (gtk_widget_get_root (GTK_WIDGET (info->view))));
        }
        g_error_free (error);
    }
    else if (info->uri) {
        gchar *struri, *docuri;
        YelpViewPrivate *priv = yelp_view_get_instance_private (info->view);
        docuri = yelp_uri_get_document_uri (priv->uri);
        if (g_str_equal (docuri, info->uri)) {
            struri = yelp_uri_get_canonical_uri (priv->uri);
            yelp_view_load (info->view, struri);
            g_free (struri);
        }
        g_free (docuri);
    }

    yelp_install_info_free (info);
}

static void
view_install_uri (YelpView    *view,
                  const gchar *uri)
{
    GDBusConnection *connection;
    GError *error = NULL;
    gboolean help = FALSE, ghelp = FALSE;
    GVariantBuilder *strv;
    YelpInstallInfo *info;
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    /* do not free */
    const gchar *pkg = NULL, *confirm_search;

    if (g_str_has_prefix (uri, "install-help:")) {
        help = TRUE;
        pkg = (gchar *) uri + 13;
    }
    else if (g_str_has_prefix (uri, "install-ghelp:")) {
        ghelp = TRUE;
        pkg = (gchar *) uri + 14;
    }
    else if (g_str_has_prefix (uri, "install:")) {
        pkg = (gchar *) uri + 8;
    }

    connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
    if (connection == NULL) {
        g_warning ("Unable to connect to dbus: %s", error->message);
        g_error_free (error);
        return;
    }

    info = g_new0 (YelpInstallInfo, 1);
    info->view = g_object_ref (view);

    if (priv->state == YELP_VIEW_STATE_ERROR)
        confirm_search = "hide-confirm-search";
    else
        confirm_search = "";

    if (help || ghelp) {
        const gchar * const *datadirs = g_get_system_data_dirs ();
        gint datadirs_i;
        gchar *docbook, *fname;
        docbook = g_strconcat (pkg, ".xml", NULL);
        strv = g_variant_builder_new (G_VARIANT_TYPE ("as"));
        for (datadirs_i = 0; datadirs[datadirs_i] != NULL; datadirs_i++) {
            if (ghelp) {
                fname = g_build_filename (datadirs[datadirs_i], "gnome", "help",
                                          pkg, "C", "index.page", NULL);
                g_variant_builder_add (strv, "s", fname);
                g_free (fname);
                fname = g_build_filename (datadirs[datadirs_i], "gnome", "help",
                                          pkg, "C", docbook, NULL);
                g_variant_builder_add (strv, "s", fname);
                g_free (fname);
            }
            else {
                fname = g_build_filename (datadirs[datadirs_i], "help", "C",
                                          pkg, "index.page", NULL);
                g_variant_builder_add (strv, "s", fname);
                g_free (fname);
                fname = g_build_filename (datadirs[datadirs_i], "help", "C",
                                          pkg, "index.docbook", NULL);
                g_variant_builder_add (strv, "s", fname);
                g_free (fname);
            }
        }
        g_free (docbook);
        info->uri = g_strconcat (ghelp ? "ghelp:" : "help:", pkg, NULL);
        g_dbus_connection_call (connection,
                                "org.freedesktop.PackageKit",
                                "/org/freedesktop/PackageKit",
                                "org.freedesktop.PackageKit.Modify2",
                                "InstallProvideFiles",
                                g_variant_new ("(ass)", strv, confirm_search),
                                NULL,
                                G_DBUS_CALL_FLAGS_NONE,
                                G_MAXINT, NULL,
                                (GAsyncReadyCallback) view_install_installed,
                                info);
        g_variant_builder_unref (strv);
    }
    else {
        gchar **pkgs;
        gint i;
        strv = g_variant_builder_new (G_VARIANT_TYPE ("as"));
        pkgs = g_strsplit (pkg, ",", 0);
        for (i = 0; pkgs[i]; i++)
            g_variant_builder_add (strv, "s", pkgs[i]);
        g_strfreev (pkgs);
        g_dbus_connection_call (connection,
                                "org.freedesktop.PackageKit",
                                "/org/freedesktop/PackageKit",
                                "org.freedesktop.PackageKit.Modify2",
                                "InstallPackageNames",
                                g_variant_new ("(uass)", strv, confirm_search),
                                NULL,
                                G_DBUS_CALL_FLAGS_NONE,
                                G_MAXINT, NULL,
                                (GAsyncReadyCallback) view_install_installed,
                                info);
        g_variant_builder_unref (strv);
    }

    g_object_unref (connection);
}

static void
popup_open_link (GSimpleAction  *action,
                 GVariant       *parameter,
                 gpointer        user_data)
{
    YelpView *view = (YelpView *) user_data;
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    YelpUri *uri;

    uri = yelp_uri_new_relative (priv->uri, priv->popup_link_uri);

    yelp_view_load_uri (view, uri);
    g_object_unref (uri);

    g_free (priv->popup_link_uri);
    priv->popup_link_uri = NULL;

    g_free (priv->popup_link_text);
    priv->popup_link_text = NULL;
}

static void
popup_open_link_new (GSimpleAction  *action,
                     GVariant       *parameter,
                     gpointer        user_data)
{
    YelpView *view = (YelpView *) user_data;
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    YelpUri *uri;

    uri = yelp_uri_new_relative (priv->uri, priv->popup_link_uri);

    g_free (priv->popup_link_uri);
    priv->popup_link_uri = NULL;

    g_free (priv->popup_link_text);
    priv->popup_link_text = NULL;

    g_signal_emit (view, signals[NEW_VIEW_REQUESTED], 0, uri);
    g_object_unref (uri);
}

static void
popup_copy_link (GSimpleAction  *action,
                 GVariant       *parameter,
                 gpointer        user_data)
{
    YelpView *view = (YelpView *) user_data;
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    gdk_clipboard_set_text (gtk_widget_get_clipboard (GTK_WIDGET (view)),
                            priv->popup_link_uri);
}

typedef struct _YelpSaveData YelpSaveData;
struct _YelpSaveData {
    GFile     *orig;
    GFile     *dest;
    YelpView  *view;
    GtkWindow *window;
};

static void
file_copied (GFile        *file,
             GAsyncResult *res,
             YelpSaveData *data)
{
    GError *error = NULL;
    if (!g_file_copy_finish (file, res, &error)) {
        AdwDialog *dialog = adw_alert_dialog_new (_("Failed to Save Image"), error->message);
        adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "close", _("_Close"));
        adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "close");
        adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "close");
        adw_dialog_present (dialog, gtk_widget_get_visible (GTK_WIDGET (data->window)) ? GTK_WIDGET (data->window) : NULL);
    }
    g_object_unref (data->orig);
    g_object_unref (data->dest);
    g_object_unref (data->view);
    g_object_unref (data->window);
}

static void
save_image_callback (GtkFileDialog *dialog,
                     GAsyncResult *res,
                     YelpSaveData *data)
{
    GError *error = NULL;

    data->dest = gtk_file_dialog_save_finish (dialog, res, &error);
    if (!error) {
        g_file_copy_async (data->orig, data->dest,
                           G_FILE_COPY_OVERWRITE,
                           G_PRIORITY_DEFAULT,
                           NULL, NULL, NULL,
                           (GAsyncReadyCallback) file_copied,
                           data);
    } else {
        g_object_unref (data->orig);
        g_object_unref (data->view);
        g_object_unref (data->window);
        g_free (data);
        g_error_free (error);
    }
}

static void
popup_save_image (GSimpleAction  *action,
                  GVariant       *parameter,
                  gpointer        user_data)
{
    YelpView *view = (YelpView *) user_data;
    YelpSaveData *data;
    GtkWindow *window;
    GtkFileDialog *dialog;
    gchar *basename;
    const gchar *save_folder;
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);

    window = GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (view)));

    data = g_new0 (YelpSaveData, 1);
    data->orig = g_file_new_for_uri (priv->popup_image_uri);
    data->view = g_object_ref (view);
    data->window = g_object_ref (GTK_WINDOW (window));
    g_free (priv->popup_image_uri);
    priv->popup_image_uri = NULL;

    dialog = gtk_file_dialog_new ();
    gtk_file_dialog_set_title (dialog, _("Save Image"));
    basename = g_file_get_basename (data->orig);
    gtk_file_dialog_set_initial_name (dialog, basename);
    g_free (basename);

    save_folder = g_get_user_special_dir (G_USER_DIRECTORY_PICTURES);
    if (save_folder)
        gtk_file_dialog_set_initial_folder (dialog, g_file_new_for_path (save_folder));

    gtk_file_dialog_save (dialog, window, NULL, (GAsyncReadyCallback) save_image_callback, data);
}

static void
popup_send_image (GSimpleAction  *action,
                  GVariant       *parameter,
                  gpointer        user_data)
{
    YelpView *view = (YelpView *) user_data;
    gchar *command;
    GAppInfo *app;
    GAppLaunchContext *context;
    GError *error = NULL;
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);

    command = g_strdup_printf ("%s %s", nautilus_sendto, priv->popup_image_uri);
    context = (GAppLaunchContext *) gdk_display_get_app_launch_context (gtk_widget_get_display (GTK_WIDGET (view)));

    app = g_app_info_create_from_commandline (command, NULL, 0, &error);
    if (app) {
        g_app_info_launch (app, NULL, context, &error);
        g_object_unref (app);
    }

    if (error) {
        g_debug ("Could not launch nautilus-sendto: %s", error->message);
        g_error_free (error);
    }

    g_object_unref (context);
    g_free (command);
    g_free (priv->popup_image_uri);
    priv->popup_image_uri = NULL;
}

static void
popup_copy_code (GSimpleAction  *action,
                 GVariant       *parameter,
                 gpointer        user_data)
{
    YelpView *view = (YelpView *) user_data;
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    GdkClipboard *clipboard;

    if (!priv->popup_code_text)
        return;

    clipboard = gtk_widget_get_clipboard (GTK_WIDGET (view));
    gdk_clipboard_set_text (clipboard, priv->popup_code_text);
}

static void
save_code_callback (GtkFileDialog *dialog,
                    GAsyncResult  *res,
                    YelpView     *view)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    GtkWidget *window = GTK_WIDGET (gtk_widget_get_root (GTK_WIDGET (view)));
    GError *error = NULL;
    GFile *file = gtk_file_dialog_save_finish (dialog, res, &error);
    GFileOutputStream *stream = g_file_replace (file, NULL, FALSE,
                                                G_FILE_CREATE_NONE,
                                                NULL,
                                                &error);

    if (stream == NULL) {
        AdwDialog *dialog = adw_alert_dialog_new (_("Failed to Save Code"), error->message);
        adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "close", _("_Close"));
        adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "close");
        adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "close");
        adw_dialog_present (dialog, window);
        g_error_free (error);
    } else {
        /* FIXME: we should do this async */
        GDataOutputStream *datastream = g_data_output_stream_new (G_OUTPUT_STREAM (stream));
        if (!g_data_output_stream_put_string (datastream, priv->popup_code_text, NULL, &error)) {
            AdwDialog *dialog = adw_alert_dialog_new (_("Failed to Save Code"), error->message);
            adw_alert_dialog_add_response (ADW_ALERT_DIALOG (dialog), "close", _("_Close"));
            adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "close");
            adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "close");
            adw_dialog_present (dialog, window);
            g_error_free (error);
        }
        g_object_unref (datastream);
    }
    g_object_unref (file);
}

static void
popup_save_code (GSimpleAction  *action,
                 GVariant       *parameter,
                 gpointer        user_data)
{
    YelpView *view = (YelpView *) user_data;
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    GtkFileDialog *dialog;
    GtkWindow *window;
    const gchar *save_folder;

    if (!priv->popup_code_text)
        return;

    if (!g_str_has_suffix (priv->popup_code_text, "\n")) {
        gchar *tmp = g_strconcat (priv->popup_code_text, "\n", NULL);
        g_free (priv->popup_code_text);
        priv->popup_code_text = tmp;
    }

    window = GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (view)));

    dialog = gtk_file_dialog_new ();
    gtk_file_dialog_set_title (dialog, _("Save Code"));
    if (priv->popup_code_title)
        gtk_file_dialog_set_initial_name (dialog, priv->popup_code_title);

    save_folder = g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS);
    if (save_folder)
        gtk_file_dialog_set_initial_folder (dialog, g_file_new_for_path (save_folder));

    gtk_file_dialog_save (dialog, window, NULL, (GAsyncReadyCallback) save_code_callback, view);
}

static void
popup_copy_clipboard (GSimpleAction  *action,
                      GVariant       *parameter,
                      gpointer        user_data)
{
    YelpView *view = (YelpView *) user_data;
    webkit_web_view_execute_editing_command (WEBKIT_WEB_VIEW (view), WEBKIT_EDITING_COMMAND_COPY);
}

static gboolean
view_populate_context_menu (YelpView            *view,
                            WebKitContextMenu   *context_menu,
                            WebKitHitTestResult *hit_test_result,
                            gpointer             user_data)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    WebKitContextMenuItem *item;
    GAction *action;
    GVariant *dom_info_variant;
    GVariantDict dom_info_dict;

    webkit_context_menu_remove_all (context_menu);

    /* We extract the info about the dom tree that we build in the web process extension.*/
    dom_info_variant = webkit_context_menu_get_user_data (context_menu);
    if (dom_info_variant)
      g_variant_dict_init (&dom_info_dict, dom_info_variant);

    if (webkit_hit_test_result_context_is_link (WEBKIT_HIT_TEST_RESULT (hit_test_result))) {
        const gchar *uri = webkit_hit_test_result_get_link_uri (hit_test_result);
        g_free (priv->popup_link_uri);

        priv->popup_link_uri = build_yelp_uri (uri);

        g_clear_pointer (&priv->popup_link_text, g_free);
        if (dom_info_variant)
          g_variant_dict_lookup (&dom_info_dict, "link-title", "s",
                                     &(priv->popup_link_text));

        if (!priv->popup_link_text)
            priv->popup_link_text = g_strdup (uri);

        if (g_str_has_prefix (priv->popup_link_uri, "mailto:")) {
            gchar *label = g_strdup_printf (_("Send email to %s"),
                                            priv->popup_link_uri + 7);
            action = g_action_map_lookup_action (G_ACTION_MAP (priv->popup_actions),
              "SendEmail");
            item = webkit_context_menu_item_new_from_gaction (action, label, NULL);
            webkit_context_menu_append (context_menu, item);
            g_free (label);
        }
        else if (g_str_has_prefix (priv->popup_link_uri, "install:")) {
            action = g_action_map_lookup_action (G_ACTION_MAP (priv->popup_actions),
              "InstallPackages");
            item = webkit_context_menu_item_new_from_gaction (action,
              _("_Install Packages"), NULL);
            webkit_context_menu_append (context_menu, item);
        }
        else {
            action = g_action_map_lookup_action (G_ACTION_MAP (priv->popup_actions),
              "OpenLink");
            item = webkit_context_menu_item_new_from_gaction (action,
              _("_Open Link"), NULL);
            webkit_context_menu_append (context_menu, item);

            if (g_str_has_prefix (priv->popup_link_uri, "http://") ||
                g_str_has_prefix (priv->popup_link_uri, "https://")) {
                action = g_action_map_lookup_action (G_ACTION_MAP (priv->popup_actions),
                  "CopyLink");
                item = webkit_context_menu_item_new_from_gaction (action,
                  _("_Copy Link Location"), NULL);
                webkit_context_menu_append (context_menu, item);
            }
            else {
                action = g_action_map_lookup_action (G_ACTION_MAP (priv->popup_actions),
                  "OpenLinkNew");
                item = webkit_context_menu_item_new_from_gaction (action,
                  _("Open Link in New _Window"), NULL);
                webkit_context_menu_append (context_menu, item);
            }
        }
    }

    if (webkit_hit_test_result_context_is_image (hit_test_result) ||
        webkit_hit_test_result_context_is_media (hit_test_result)) {
        /* This doesn't currently work for video with automatic controls,
         * because WebKit puts the hit test on the div with the controls.
         */
        gboolean image = webkit_hit_test_result_context_is_image (hit_test_result);
        const gchar *uri = image ? webkit_hit_test_result_get_image_uri (hit_test_result) :
          webkit_hit_test_result_get_media_uri (hit_test_result);
        gchar *yelp_uri;
        g_free (priv->popup_image_uri);

        yelp_uri = build_yelp_uri (uri);

        if (g_str_has_prefix (yelp_uri, "help:") ||
            g_str_has_prefix (yelp_uri, "ghelp:") ||
            g_str_has_prefix (yelp_uri, "gnome-help:")) {
            gchar *image_uri = strstr (yelp_uri, "/");

            if (image_uri) {
                image_uri[0] = '\0';
                image_uri++;
            }

            if (image_uri && image_uri[0] != '\0')
                priv->popup_image_uri = yelp_uri_locate_file_uri (priv->uri, image_uri);
            else
                priv->popup_image_uri = NULL;
        }
        else {
            priv->popup_image_uri = yelp_uri;
        }

        g_free (yelp_uri);

        item = webkit_context_menu_item_new_separator ();
        webkit_context_menu_append (context_menu, item);

        action = g_action_map_lookup_action (G_ACTION_MAP (priv->popup_actions),
                                      "SaveMedia");

        item = webkit_context_menu_item_new_from_gaction (action,
                              image ? _("_Save Image As…") :
                                      _("_Save Video As…"),
                              NULL);
        webkit_context_menu_append (context_menu, item);

        if (nautilus_sendto) {
            action = g_action_map_lookup_action (G_ACTION_MAP (priv->popup_actions),
                                                  "SendMedia");

            item = webkit_context_menu_item_new_from_gaction (action,
                                      image ? _("S_end Image To…") :
                                              _("S_end Video To…"),
                                      NULL);
            webkit_context_menu_append (context_menu, item);
        }
    }

    if (webkit_hit_test_result_context_is_selection (hit_test_result)) {
        item = webkit_context_menu_item_new_separator ();
        webkit_context_menu_append (context_menu, item);

        action = g_action_map_lookup_action (G_ACTION_MAP (priv->popup_actions),
                                              "CopyText");
        item = webkit_context_menu_item_new_from_gaction (action,
                                              _("_Copy Text"), NULL);
        webkit_context_menu_append (context_menu, item);
    }

    g_clear_pointer (&priv->popup_code_title, g_free);
    if (dom_info_variant)
      g_variant_dict_lookup (&dom_info_dict, "code-title",
            "s", &(priv->popup_code_title));

    g_clear_pointer (&priv->popup_code_text, g_free);
    if (dom_info_variant)
      g_variant_dict_lookup (&dom_info_dict, "code-text",
        "s", &(priv->popup_code_text));

    if (priv->popup_code_text) {
        item = webkit_context_menu_item_new_separator ();
        webkit_context_menu_append (context_menu, item);

        action = g_action_map_lookup_action (G_ACTION_MAP (priv->popup_actions),
                                              "CopyCode");
        item = webkit_context_menu_item_new_from_gaction (action,
                                              _("C_opy Code Block"), NULL);
        webkit_context_menu_append (context_menu, item);

        action = g_action_map_lookup_action (G_ACTION_MAP (priv->popup_actions),
                                              "SaveCode");
        item = webkit_context_menu_item_new_from_gaction (action,
                                              _("Save Code _Block As…"), NULL);
        webkit_context_menu_append (context_menu, item);
    }

    return FALSE;
}

static gboolean
view_script_dialog (YelpView           *view,
                    WebKitScriptDialog *dialog,
                    gpointer            data)
{
    WebKitScriptDialogType type = webkit_script_dialog_get_dialog_type (dialog);

    if (type != WEBKIT_SCRIPT_DIALOG_ALERT)
      return FALSE;

    printf ("\n\n===ALERT===\n%s\n\n", webkit_script_dialog_get_message (dialog));
    return TRUE;
}

static gboolean
view_policy_decision_requested (YelpView                *view,
                                WebKitPolicyDecision    *decision,
                                WebKitPolicyDecisionType type,
                                gpointer                 user_data)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    WebKitNavigationAction *action;
    WebKitURIRequest *request;
    gchar *fixed_uri;
    YelpUri *uri;

    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION)
        return FALSE;

    if (!priv->resolve_uri_on_policy_decision) {
        priv->resolve_uri_on_policy_decision = TRUE;
        return FALSE;
    }

    action = webkit_navigation_policy_decision_get_navigation_action (WEBKIT_NAVIGATION_POLICY_DECISION (decision));
    request = webkit_navigation_action_get_request (action);
    fixed_uri = build_yelp_uri (webkit_uri_request_get_uri (request));
    if (webkit_navigation_action_get_navigation_type (action) == WEBKIT_NAVIGATION_TYPE_BACK_FORWARD) {
        uri = yelp_uri_new (fixed_uri);
        priv->load_page_after_resolved = FALSE;
        yelp_view_resolve_uri (view, uri);
        g_object_unref (uri);
        g_free (fixed_uri);

        return FALSE;
    }

    webkit_policy_decision_ignore (decision);

    uri = yelp_uri_new_relative (priv->uri, fixed_uri);
    yelp_view_load_uri ((YelpView *) view, uri);
    g_object_unref (uri);
    g_free (fixed_uri);

    return TRUE;
}

static void
view_load_status_changed (WebKitWebView   *view,
                          WebKitLoadEvent  load_event,
                          gpointer         user_data)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (YELP_VIEW (view));

    if (priv->state == YELP_VIEW_STATE_ERROR)
        return;

    switch (load_event) {
    case WEBKIT_LOAD_COMMITTED: {
        gchar *real_id;
        GParamSpec *spec;

        real_id = yelp_document_get_page_id (priv->document, priv->page_id);
        if (priv->page_id && real_id && g_str_equal (real_id, priv->page_id)) {
            g_free (real_id);
        }
        else {
            g_free (priv->page_id);
            priv->page_id = real_id;
            spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                                 "page-id");
            g_signal_emit_by_name (view, "notify::page-id", spec);
        }

        g_free (priv->root_title);
        g_free (priv->page_title);
        g_free (priv->page_desc);
        g_free (priv->page_icon);

        priv->root_title = yelp_document_get_root_title (priv->document, priv->page_id);
        priv->page_title = yelp_document_get_page_title (priv->document, priv->page_id);
        priv->page_desc = yelp_document_get_page_desc (priv->document, priv->page_id);
        priv->page_icon = yelp_document_get_page_icon (priv->document, priv->page_id);

        spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                             "root-title");
        g_signal_emit_by_name (view, "notify::root-title", spec);

        spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                             "page-title");
        g_signal_emit_by_name (view, "notify::page-title", spec);

        spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                             "page-desc");
        g_signal_emit_by_name (view, "notify::page-desc", spec);

        spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                             "page-icon");
        g_signal_emit_by_name (view, "notify::page-icon", spec);
        break;
    }
    case WEBKIT_LOAD_FINISHED:
        g_object_set (view, "state", YELP_VIEW_STATE_LOADED, NULL);

        /* If the document didn't give us a page title, get it from WebKit.
         * We let the main loop run through so that WebKit gets the title
         * set so that we can send notify::page-title before loaded. It
         * simplifies things if YelpView consumers can assume the title
         * is set before loaded is triggered.
         */
        if (priv->page_title == NULL) {
            GParamSpec *spec;
            priv->page_title = g_strdup (webkit_web_view_get_title (view));
            spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                                 "page-title");
            g_signal_emit_by_name (view, "notify::page-title", spec);
        }

        g_signal_emit (view, signals[LOADED], 0);
        view_history_changed (YELP_VIEW (view));

        break;
    case WEBKIT_LOAD_STARTED:
    case WEBKIT_LOAD_REDIRECTED:
    default:
        break;
    }
}

static void
view_load_failed (WebKitWebView  *view,
                  WebKitLoadEvent load_event,
                  gchar          *failing_uri,
                  GError         *error,
                  gpointer        user_data)
{
    view_show_error_page (YELP_VIEW (view), error);
}

static void
view_print_action (GAction *action, GVariant *parameter, YelpView *view)
{
    GtkWindow *window;
    WebKitPrintOperation *print_operation;
    GtkPrintSettings *settings;
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);

    window = GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (view)));

    print_operation = webkit_print_operation_new (WEBKIT_WEB_VIEW (view));

    settings = gtk_print_settings_new ();
    gtk_print_settings_set (settings,
                            GTK_PRINT_SETTINGS_OUTPUT_BASENAME,
                            priv->page_title);
    webkit_print_operation_set_print_settings (print_operation, settings);

    webkit_print_operation_run_dialog (print_operation, window);
    g_object_unref (print_operation);
    g_object_unref (settings);
}

static void
view_history_action (GAction   *action,
                     GVariant  *parameter,
                     YelpView  *view)
{
    if (g_str_equal (g_action_get_name (action), "yelp-view-go-back"))
        webkit_web_view_go_back (WEBKIT_WEB_VIEW (view));
    else
        webkit_web_view_go_forward (WEBKIT_WEB_VIEW (view));
}

static void
view_history_changed (YelpView *view)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    WebKitWebView *web_view = WEBKIT_WEB_VIEW (view);

    g_simple_action_set_enabled (priv->back_action, webkit_web_view_can_go_back (web_view));
    g_simple_action_set_enabled (priv->forward_action, webkit_web_view_can_go_forward (web_view));
    {
        WebKitBackForwardList *list = webkit_web_view_get_back_forward_list (web_view);
        GList *back = webkit_back_forward_list_get_back_list (list);
        GList *forward = webkit_back_forward_list_get_forward_list (list);
        gboolean has_history = (back != NULL) || (forward != NULL);
        g_simple_action_set_enabled (priv->history_action, has_history);
        if (back) g_list_free (back);
        if (forward) g_list_free (forward);
    }
}

static void
view_navigation_action (GAction  *action,
                        GVariant *parameter,
                        YelpView *view)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    gchar *page_id, *new_id, *xref;
    YelpUri *new_uri;

    page_id = yelp_uri_get_page_id (priv->uri);

    if (g_str_equal (g_action_get_name (action), "yelp-view-go-previous"))
        new_id = yelp_document_get_prev_id (priv->document, page_id);
    else
        new_id = yelp_document_get_next_id (priv->document, page_id);

    /* Just in case we screwed up somewhere */
    if (new_id == NULL) {
        g_simple_action_set_enabled (G_SIMPLE_ACTION (action), FALSE);
        return;
    }

    xref = g_strconcat ("xref:", new_id, NULL);
    new_uri = yelp_uri_new_relative (priv->uri, xref);
    yelp_view_load_uri (view, new_uri);

    g_free (xref);
    g_free (new_id);
    g_object_unref (new_uri);
}

static void
history_close_panel_now (YelpView *view)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);

    if (priv->history_close_timeout_id) {
        g_source_remove (priv->history_close_timeout_id);
        priv->history_close_timeout_id = 0;
    }

    if (priv->history_panel) {
        gtk_widget_unparent (priv->history_panel);
        g_clear_object (&priv->history_panel);
    }

    if (priv->history_scrim) {
        gtk_widget_unparent (priv->history_scrim);
        g_clear_object (&priv->history_scrim);
    }
}

static gboolean
history_close_panel_done (gpointer user_data)
{
    YelpView *view = YELP_VIEW (user_data);
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);

    priv->history_close_timeout_id = 0;
    history_close_panel_now (view);
    g_object_unref (view);

    return G_SOURCE_REMOVE;
}

static void
history_close_panel (YelpView *view)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);

    if (priv->history_close_timeout_id) {
        g_source_remove (priv->history_close_timeout_id);
        priv->history_close_timeout_id = 0;
    }

    if (priv->history_panel)
        gtk_widget_add_css_class (priv->history_panel, "yelp-history-panel-closing");

    if (priv->history_scrim)
        gtk_widget_add_css_class (priv->history_scrim, "yelp-history-overlay-closing");

    if (priv->history_panel || priv->history_scrim)
        priv->history_close_timeout_id = g_timeout_add (250, history_close_panel_done, g_object_ref (view));
}

static void
history_scrim_pressed (GtkGesture *gesture,
                       guint       n_press,
                       double      x,
                       double      y,
                       gpointer    user_data)
{
    YelpView *view = YELP_VIEW (user_data);
    history_close_panel (view);
}

static gboolean
history_key_pressed (GtkEventControllerKey *controller,
                     guint                  keyval,
                     guint                  keycode,
                     GdkModifierType        state,
                     gpointer               user_data)
{
    if (keyval == GDK_KEY_Escape) {
        YelpView *view = YELP_VIEW (user_data);
        history_close_panel (view);
        return TRUE;
    }
    return FALSE;
}

static GtkWidget *
history_create_panel(YelpView *view,
                     GtkWidget **out_scrim)
{
    GtkWidget *overlay = gtk_widget_get_parent (GTK_WIDGET (view));
    if (!GTK_IS_OVERLAY (overlay))
        return NULL;

    GtkWidget *scrim = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class (scrim, "yelp-history-overlay");
    gtk_widget_set_hexpand (scrim, TRUE);
    gtk_widget_set_vexpand (scrim, TRUE);
    gtk_widget_set_visible (scrim, TRUE);
    gtk_widget_set_can_focus (scrim, TRUE);

    GtkGesture *gesture = gtk_gesture_click_new ();
    g_signal_connect (gesture, "pressed", G_CALLBACK (history_scrim_pressed), view);
    gtk_widget_add_controller (scrim, GTK_EVENT_CONTROLLER (gesture));

    GtkEventController *key_controller = gtk_event_controller_key_new ();
    g_signal_connect (key_controller, "key-pressed", G_CALLBACK (history_key_pressed), view);
    gtk_widget_add_controller (scrim, key_controller);

    gtk_overlay_add_overlay (GTK_OVERLAY (overlay), scrim);
    gtk_widget_grab_focus (scrim);

    GtkWidget *panel = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class (panel, "yelp-history-panel");
    gtk_widget_set_hexpand (panel, FALSE);
    gtk_widget_set_vexpand (panel, FALSE);
    gtk_widget_set_halign (panel, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (panel, GTK_ALIGN_CENTER);
    gtk_widget_set_visible (panel, TRUE);
    //gtk_widget_set_size_request (panel, 420, 520);

    gtk_overlay_add_overlay (GTK_OVERLAY (overlay), panel);

    *out_scrim = scrim;
    return panel;
}

static void
yelp_view_history_action (GAction  *action,
                         GVariant *parameter,
                         YelpView *view)
{
    yelp_view_show_history_popup (view);
}

typedef struct _HistButtonData HistButtonData;
struct _HistButtonData {
    YelpView *view;
    WebKitBackForwardListItem *item;
};

static void
hist_button_data_free (gpointer data)
{
    HistButtonData *d = (HistButtonData *) data;
    g_object_unref (d->view);
    g_object_unref (d->item);
    g_free (data);
}

static void
history_row_activated (GtkListBox   *box,
                       GtkListBoxRow *row,
                       gpointer       user_data)
{
    YelpView *view = YELP_VIEW (user_data);
    HistButtonData *d = g_object_get_data (G_OBJECT (row), "hist-data");
    if (!d || !d->view || !d->item)
        return;
        
    webkit_web_view_go_to_back_forward_list_item (WEBKIT_WEB_VIEW (d->view), d->item);
    history_close_panel (view);
}

static GtkWidget *
create_history_row (YelpView                  *view,
                    WebKitBackForwardListItem *item)
{
    const gchar *title = webkit_back_forward_list_item_get_title (item);
    const gchar *uri = webkit_back_forward_list_item_get_uri (item);

    GtkWidget *row = gtk_list_box_row_new ();
    GtkWidget *action = adw_action_row_new ();
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (action), title ? title : uri);
    adw_action_row_set_subtitle (ADW_ACTION_ROW (action), uri);
    gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), action);

    HistButtonData *d = g_new0 (HistButtonData, 1);
    d->view = g_object_ref (view);
    d->item = g_object_ref (item);

    g_object_set_data_full (G_OBJECT (row), "hist-data", d, hist_button_data_free);

    return row;
}

static void
yelp_view_show_history_popup (YelpView *view)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    GList *l;

    history_close_panel_now (view);

    WebKitBackForwardList *history_list = webkit_web_view_get_back_forward_list (WEBKIT_WEB_VIEW (view));
    GList *back = webkit_back_forward_list_get_back_list (history_list);
    GList *forward = webkit_back_forward_list_get_forward_list (history_list);

    GtkWidget *panel = history_create_panel (view, &priv->history_scrim);
    if (!panel)
        return;

    GtkWidget *list_box = gtk_list_box_new ();
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (list_box), GTK_SELECTION_NONE);
    gtk_list_box_set_activate_on_single_click (GTK_LIST_BOX (list_box), TRUE);
    g_signal_connect (list_box, "row-activated", G_CALLBACK (history_row_activated), view);
    gtk_widget_add_css_class (GTK_WIDGET (list_box), "boxed-list");
    gtk_widget_set_can_focus (list_box, TRUE);

    GtkEventController *key_controller = gtk_event_controller_key_new ();
    g_signal_connect (key_controller, "key-pressed", G_CALLBACK (history_key_pressed), view);
    gtk_widget_add_controller (list_box, key_controller);

    GtkWidget *scroll = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                    GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request (scroll, 400, 500);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), list_box);
    gtk_box_append (GTK_BOX (panel), scroll);
    gtk_widget_grab_focus (list_box);

    for (l = back; l != NULL; l = l->next) {
        WebKitBackForwardListItem *it = WEBKIT_BACK_FORWARD_LIST_ITEM (l->data);
        GtkWidget *row = create_history_row (view, it);
        gtk_list_box_append (GTK_LIST_BOX (list_box), GTK_WIDGET (row));
    }

    if (back && forward) {
        GtkWidget *sep = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
        GtkWidget *sep_row = gtk_list_box_row_new ();
        gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (sep_row), FALSE);
        gtk_list_box_row_set_selectable (GTK_LIST_BOX_ROW (sep_row), FALSE);
        gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (sep_row), sep);
        gtk_list_box_append (GTK_LIST_BOX (list_box), sep_row);
    }

    for (l = forward; l != NULL; l = l->next) {
        WebKitBackForwardListItem *it = WEBKIT_BACK_FORWARD_LIST_ITEM (l->data);
        GtkWidget *row = create_history_row (view, it);
        gtk_list_box_append (GTK_LIST_BOX (list_box), GTK_WIDGET (row));
    }

    if (back)
        g_list_free (back);
    if (forward)
        g_list_free (forward);

    priv->history_panel = panel;
}


static void
view_clear_load (YelpView *view)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);

    if (priv->resolve_uri != NULL) {
        if (priv->uri_resolved != 0) {
            g_signal_handler_disconnect (priv->resolve_uri, priv->uri_resolved);
            priv->uri_resolved = 0;
        }
        g_object_unref (priv->resolve_uri);
    }
    priv->resolve_uri = NULL;

    if (priv->uri) {
        g_object_unref (priv->uri);
        priv->uri = NULL;
    }

    if (priv->cancellable) {
        g_cancellable_cancel (priv->cancellable);
        priv->cancellable = NULL;
    }
}

static gchar*
fix_docbook_uri (YelpUri *docbook_uri, YelpDocument* document)
{
    GUri *uri;
    gchar *retval, *canonical;
    const gchar *fragment;

    canonical = yelp_uri_get_canonical_uri (docbook_uri);
    uri = g_uri_parse (canonical, G_URI_FLAGS_ENCODED, NULL);
    g_free (canonical);

    /* We don't have actual page and frag IDs for DocBook. We just map IDs
       of block elements.  The result is that we get xref:someid#someid.
       If someid is really the page ID, we just drop the frag reference.
       Otherwise, normal page views scroll past the link trail.
    */
    fragment = g_uri_get_fragment (uri);
    if (fragment && YELP_IS_DOCBOOK_DOCUMENT (document)) {
        gchar *page_id = yelp_uri_get_page_id (docbook_uri);
        gchar *real_id = yelp_document_get_page_id (document, page_id);

        if (g_str_equal (real_id, fragment)) {
            GUri *modified;

            modified = g_uri_build (g_uri_get_flags (uri),
                                    g_uri_get_scheme (uri),
                                    g_uri_get_userinfo (uri),
                                    g_uri_get_host (uri),
                                    g_uri_get_port (uri),
                                    g_uri_get_path (uri),
                                    g_uri_get_query (uri),
                                    NULL);
            g_uri_unref (uri);
            uri = modified;
        }

        g_free (real_id);
        g_free (page_id);
    }

    retval = g_uri_to_string (uri);
    g_uri_unref (uri);

    return retval;
}

static void
view_load_page (YelpView *view)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    gchar *uri_str, *tmp_uri;

    g_return_if_fail (priv->cancellable == NULL);

    if (priv->document == NULL) {
        GError *error;
        gchar *docuri;
        /* FIXME: and if priv->uri is NULL? */
        docuri = yelp_uri_get_document_uri (priv->uri);
        /* FIXME: CANT_READ isn't right */
        if (docuri) {
            error = g_error_new (YELP_ERROR, YELP_ERROR_CANT_READ,
                                 _("Could not load a document for ‘%s’"),
                                 docuri);
            g_free (docuri);
        }
        else {
            error = g_error_new (YELP_ERROR, YELP_ERROR_CANT_READ,
                                 _("Could not load a document"));
        }
        view_show_error_page (view, error);
        g_error_free (error);
        return;
    }

    uri_str = yelp_uri_get_canonical_uri (priv->uri);

    if (YELP_IS_DOCBOOK_DOCUMENT (priv->document)){
      tmp_uri = uri_str;
      uri_str = fix_docbook_uri (priv->uri, priv->document);
      g_free (tmp_uri);
    }

    tmp_uri = uri_str;
    uri_str = build_network_uri (uri_str);
    g_free (tmp_uri);

    priv->resolve_uri_on_policy_decision = FALSE;
    webkit_web_view_load_uri (WEBKIT_WEB_VIEW (view), uri_str);

    g_free (uri_str);
}

static void
view_show_error_page (YelpView *view,
                      GError   *error)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    YelpSettings *settings = yelp_settings_get_default ();
    GtkIconTheme *icontheme;
    GtkIconPaintable *icon;
	GBytes *html_file = NULL;
	GString *page = g_string_new (NULL);
    gchar *title = NULL, *content_beg, *content_end;
    gchar *icon_filename = "";
    GParamSpec *spec;
    gboolean doc404 = FALSE;

    if (priv->uri && yelp_uri_get_document_type (priv->uri) == YELP_URI_DOCUMENT_TYPE_NOT_FOUND)
        doc404 = TRUE;
    if (error->domain == YELP_ERROR)
        switch (error->code) {
        case YELP_ERROR_NOT_FOUND:
            if (doc404)
                title = _("Document Not Found");
            else
                title = _("Page Not Found");
            break;
        case YELP_ERROR_CANT_READ:
            title = _("Cannot Read");
            break;
        default:
            break;
        }
    if (title == NULL)
        title = _("Unknown Error");

    content_beg = g_markup_printf_escaped ("<p>%s</p>", error->message);
    content_end = NULL;
    if (doc404) {
        gchar *struri = yelp_uri_get_document_uri (priv->uri);
        /* do not free */
        const gchar *pkg = NULL, *scheme = NULL;
        if (g_str_has_prefix (struri, "help:")) {
            scheme = "help";
            pkg = struri + 5;
        }
        else if (g_str_has_prefix (struri, "ghelp:")) {
            scheme = "ghelp";
            pkg = struri + 6;
        }
        g_free (struri);
    }

    icontheme = gtk_icon_theme_get_for_display (gtk_root_get_display (gtk_widget_get_root (GTK_WIDGET (view))));
	if (doc404)
    	icon = gtk_icon_theme_lookup_icon (icontheme, "dialog-question-symbolic", NULL, 128, 1, GTK_TEXT_DIR_NONE, 0);
	else
    	icon = gtk_icon_theme_lookup_icon (icontheme, "computer-fail-symbolic", NULL, 128, 1, GTK_TEXT_DIR_NONE, 0);

    if (icon != NULL) {
        GFile *iconfile = gtk_icon_paintable_get_file (icon);
        if (iconfile) {
            icon_filename = g_file_get_uri (iconfile);
            g_object_unref (iconfile);
        }
        g_object_unref (icon);
    }

    html_file = g_resources_lookup_data ("/org/gnome/yelp/resources/error.html", 0, NULL);

    g_string_printf (page,
                     g_bytes_get_data (html_file, NULL),
                     icon_filename,
                     title,
                     content_beg,
                     (content_end != NULL) ? content_end : "");

    g_object_set (view, "state", YELP_VIEW_STATE_ERROR, NULL);

    if (doc404) {
        g_free (priv->root_title);
        priv->root_title = g_strdup (title);
        spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                             "root-title");
        g_signal_emit_by_name (view, "notify::root-title", spec);
        g_free (priv->page_id);
        priv->page_id = g_strdup ("index");
        spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                             "page-id");
        g_signal_emit_by_name (view, "notify::page-id", spec);
    }

    g_free (priv->page_title);
    priv->page_title = g_strdup (title);
    spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                         "page-title");
    g_signal_emit_by_name (view, "notify::page-title", spec);

    g_free (priv->page_desc);
    priv->page_desc = NULL;
    if (priv->uri)
        priv->page_desc = yelp_uri_get_canonical_uri (priv->uri);
    spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                         "page-desc");
    g_signal_emit_by_name (view, "notify::page-desc", spec);

    g_free (priv->page_icon);
    priv->page_icon = g_strdup ("dialog-warning");
    spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                         "page-icon");
    g_signal_emit_by_name (view, "notify::page-icon", spec);

    g_signal_emit (view, signals[LOADED], 0);

    priv->resolve_uri_on_policy_decision = FALSE;
    webkit_web_view_load_html  (WEBKIT_WEB_VIEW (view),
                                page->str,
                                "file:///error/");
    g_free (content_beg);
    if (content_end != NULL)
        g_free (content_end);
}

static void
settings_set_scale (YelpSettings *settings,
                    GParamSpec   *spec,
                    gpointer      user_data)
{
    YelpView *view = user_data;
    double zoom_level = yelp_settings_get_zoom_level (settings);
    webkit_web_view_set_zoom_level (WEBKIT_WEB_VIEW (view), zoom_level);
}

static void
settings_set_fonts (YelpSettings *settings,
                    gpointer      user_data)
{
    YelpView *view;
    gchar *family;
    gint size;

    view = (YelpView *) user_data;

    family = yelp_settings_get_font_family (settings,
                                            YELP_SETTINGS_FONT_VARIABLE);
    size = yelp_settings_get_font_size (settings,
                                        YELP_SETTINGS_FONT_VARIABLE);
    g_object_set (webkit_web_view_get_settings (WEBKIT_WEB_VIEW (view)),
                  "default-font-family", family,
                  "sans-serif-font-family", family,
                  "default-font-size", webkit_settings_font_size_to_pixels (size),
                  NULL);
    g_free (family);

    family = yelp_settings_get_font_family (settings,
                                            YELP_SETTINGS_FONT_FIXED);
    size = yelp_settings_get_font_size (settings,
                                        YELP_SETTINGS_FONT_FIXED);
    g_object_set (webkit_web_view_get_settings (WEBKIT_WEB_VIEW (view)),
                  "monospace-font-family", family,
                  "default-monospace-font-size", webkit_settings_font_size_to_pixels (size),
                  NULL);
    g_free (family);
}

static void
settings_show_text_cursor (YelpSettings *settings)
{
    webkit_settings_set_enable_caret_browsing (yelp_view_get_global_settings (),
                                               yelp_settings_get_show_text_cursor (settings));
}

/******************************************************************************/

static void
uri_resolved (YelpUri  *uri,
              YelpView *view)
{
    YelpViewPrivate *priv = yelp_view_get_instance_private (view);
    YelpUriDocumentType doctype;
    YelpDocument *document;
    GError *error = NULL;
    gchar *struri;
    GParamSpec *spec;

    doctype = yelp_uri_get_document_type (uri);

    if (doctype != YELP_URI_DOCUMENT_TYPE_EXTERNAL) {
        g_object_ref (uri);
        view_clear_load (view);
        priv->uri = uri;
    }

    if (doctype == YELP_URI_DOCUMENT_TYPE_EXTERNAL) {
        g_object_set (view, "state", priv->prevstate, NULL);
        struri = yelp_uri_get_canonical_uri (uri);
        if (g_str_has_prefix (struri, "install:") ||
            g_str_has_prefix (struri, "install-ghelp:") ||
            g_str_has_prefix (struri, "install-help:")) {
            view_install_uri (view, struri);
        }
        else {
            gboolean result;
            g_signal_emit (view, signals[EXTERNAL_URI], 0, uri, &result);
        }
        g_free (struri);
        return;
    }
    else if (doctype == YELP_URI_DOCUMENT_TYPE_NOT_FOUND) {
        struri = yelp_uri_get_canonical_uri (uri);
        if (struri != NULL) {
            error = g_error_new (YELP_ERROR, YELP_ERROR_NOT_FOUND,
                                 _("The URI ‘%s’ does not point to a valid page."),
                                 struri);
            g_free (struri);
        }
        else {
            error = g_error_new (YELP_ERROR, YELP_ERROR_NOT_FOUND,
                                 _("The URI does not point to a valid page."));
        }
    }
    else if (doctype == YELP_URI_DOCUMENT_TYPE_ERROR) {
        struri = yelp_uri_get_canonical_uri (uri);
        error = g_error_new (YELP_ERROR, YELP_ERROR_PROCESSING,
                             _("The URI ‘%s’ could not be parsed."),
                             struri);
        g_free (struri);
    }

    spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                         "yelp-uri");
    g_signal_emit_by_name (view, "notify::yelp-uri", spec);

    g_free (priv->page_id);
    priv->page_id = NULL;
    if (priv->uri != NULL)
        priv->page_id = yelp_uri_get_page_id (priv->uri);
    spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                         "page-id");
    g_signal_emit_by_name (view, "notify::page-id", spec);

    g_free (priv->root_title);
    g_free (priv->page_title);
    g_free (priv->page_desc);
    g_free (priv->page_icon);
    priv->root_title = NULL;
    priv->page_title = NULL;
    priv->page_desc = NULL;
    priv->page_icon = NULL;

    spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                         "root-title");
    g_signal_emit_by_name (view, "notify::root-title", spec);

    spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                         "page-title");
    g_signal_emit_by_name (view, "notify::page-title", spec);

    spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                         "page-desc");
    g_signal_emit_by_name (view, "notify::page-desc", spec);

    spec = g_object_class_find_property ((GObjectClass *) YELP_VIEW_GET_CLASS (view),
                                         "page-icon");
    g_signal_emit_by_name (view, "notify::page-icon", spec);

    if (error == NULL) {
        document = yelp_document_get_for_uri (uri);
        if (priv->document)
            g_object_unref (priv->document);
        priv->document = document;

        if (priv->load_page_after_resolved)
            view_load_page (view);
    } else {
        if (priv->document != NULL) {
            g_object_unref (priv->document);
            priv->document = NULL;
        }
        view_show_error_page (view, error);
        g_error_free (error);
    }
}
