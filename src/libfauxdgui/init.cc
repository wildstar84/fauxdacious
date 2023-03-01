/*
 * init.c
 * Copyright 2010-2013 John Lindgren
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include <assert.h>
#include <stdlib.h>

#include <libfauxdcore/sdl_window.h>
#include <libfauxdcore/audstrings.h>
#include <libfauxdcore/hook.h>
#include <libfauxdcore/playlist.h>
#include <libfauxdcore/plugins.h>
#include <libfauxdcore/runtime.h>

#include "internal.h"
#include "libfauxdgui.h"
#include "libfauxdgui-gtk.h"

extern "C" {
#include "images.h"
}

static const char * const audgui_defaults[] = {
    "clear_song_fields", "TRUE",
    "close_dialog_add", "FALSE",
    "close_dialog_open", "TRUE",
    "close_jtf_dialog", "TRUE",
    "record", "FALSE",
    "remember_jtf_entry", "TRUE",
    nullptr
};

static const char * const window_names[AUDGUI_NUM_UNIQUE_WINDOWS] = {
    "about_win",
    "equalizer_win",
    "eq_preset_win",
    "filebrowser_win",
    nullptr, /* infopopup position is not saved */
    "info_win",
    "jump_to_time_win",
    "jump_to_track_win",
    "playlist_export_win",
    "playlist_import_win",
    "preset_browser_win",
    "queue_manager_win",
    "url_opener_win"
};

static int init_count = 0;

static GtkWidget * windows[AUDGUI_NUM_UNIQUE_WINDOWS];

static gboolean configure_cb (GtkWidget * window, GdkEventConfigure * event, const char * name)
{
    if (gtk_widget_get_visible (window))
    {
        int pos[4];
        gtk_window_get_position ((GtkWindow *) window, & pos[0], & pos[1]);
        gtk_window_get_size ((GtkWindow *) window, & pos[2], & pos[3]);

        pos[2] = audgui_to_portable_dpi (pos[2]);
        pos[3] = audgui_to_portable_dpi (pos[3]);

        aud_set_str ("audgui", name, int_array_to_str (pos, 4));
    }

    return false;
}

void audgui_show_unique_window (int id, GtkWidget * widget)
{
    g_return_if_fail (id >= 0 && id < AUDGUI_NUM_UNIQUE_WINDOWS);

    if (windows[id])
        gtk_widget_destroy (windows[id]);

    windows[id] = widget;
    g_signal_connect (widget, "destroy", (GCallback) gtk_widget_destroyed, & windows[id]);

    if (window_names[id])
    {
        String str = aud_get_str ("audgui", window_names[id]);
        int pos[4];

        if (str_to_int_array (str, pos, 4))
        {
            pos[2] = audgui_to_native_dpi (pos[2]);
            pos[3] = audgui_to_native_dpi (pos[3]);

            gtk_window_move ((GtkWindow *) widget, pos[0], pos[1]);
            gtk_window_set_default_size ((GtkWindow *) widget, pos[2], pos[3]);
        }

        g_signal_connect (widget, "configure-event", (GCallback) configure_cb,
         (void *) window_names[id]);
    }

    gtk_widget_show_all (widget);
}

bool audgui_reshow_unique_window (int id)
{
    g_return_val_if_fail (id >= 0 && id < AUDGUI_NUM_UNIQUE_WINDOWS, false);

    if (! windows[id])
        return false;

    gtk_window_present ((GtkWindow *) windows[id]);
    return true;
}

void audgui_hide_unique_window (int id)
{
    g_return_if_fail (id >= 0 && id < AUDGUI_NUM_UNIQUE_WINDOWS);

    if (windows[id])
        gtk_widget_destroy (windows[id]);
}

#ifdef _WIN32
/* On Windows, the default icon sizes are fixed.
 * Adjust them for varying screen resolutions. */
void adjust_icon_sizes ()
{
    struct Mapping {
        GtkIconSize size;
        const char * name;
    };

    static const Mapping mappings[] = {
        {GTK_ICON_SIZE_MENU, "gtk-menu"},
        {GTK_ICON_SIZE_SMALL_TOOLBAR, "gtk-small-toolbar"},
        {GTK_ICON_SIZE_LARGE_TOOLBAR, "gtk-large-toolbar"},
        {GTK_ICON_SIZE_BUTTON, "gtk-button"},
        {GTK_ICON_SIZE_DND, "gtk-dnd"},
        {GTK_ICON_SIZE_DIALOG, "gtk-dialog"}
    };

    StringBuf value;

    for (auto & m : mappings)
    {
        int width, height;
        if (gtk_icon_size_lookup (m.size, & width, & height))
        {
            width = audgui_to_native_dpi (width);
            height = audgui_to_native_dpi (height);

            const char * sep = value.len () ? ":" : "";
            str_append_printf (value, "%s%s=%d,%d", sep, m.name, width, height);
        }
    }

    GtkSettings * settings = gtk_settings_get_default ();
    g_object_set ((GObject *) settings, "gtk-icon-sizes", (const char *) value, nullptr);
}
#endif

static int get_icon_size (GtkIconSize size)
{
    int width, height;
    if (gtk_icon_size_lookup (size, & width, & height))
        return (width + height) / 2;

    return audgui_to_native_dpi (16);
}

static void load_fallback_icon (const char * icon, int size)
{
    StringBuf resource = str_concat ({"/org/fauxdacious/", icon, ".svg"});
    auto pixbuf = gdk_pixbuf_new_from_resource_at_scale (resource, size, size, true, nullptr);

    if (pixbuf)
    {
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        gtk_icon_theme_add_builtin_icon (icon, size, pixbuf);
        g_object_unref (pixbuf);
G_GNUC_END_IGNORE_DEPRECATIONS
    }
}

static void load_fallback_icons ()
{
    static const char * const all_icons[] = {
        "application-exit",
        "applications-graphics",
        "applications-internet",
        "applications-system",
        "appointment-new",
        "fauxdacious",
        "audio-card",
        "audio-volume-high",
        "audio-volume-low",
        "audio-volume-medium",
        "audio-volume-muted",
        "audio-x-generic", /* also used for fallback album art */
        "dialog-error",
        "dialog-information",
        "dialog-question",
        "dialog-warning",
        "document-new",
        "document-open-recent",
        "document-open",
        "document-save",
        "edit-clear",
        "edit-clear-all",
        "edit-copy",
        "edit-cut",
        "edit-delete",
        "edit-find",
        "edit-paste",
        "edit-select-all",
        "edit-undo",
        "face-smile",
        "folder-remote",
        "folder",
        "go-down",
        "go-jump",
        "go-next",
        "go-previous",
        "go-up",
        "help-about",
        "insert-text",
        "list-add",
        "list-remove",
        "media-optical",
        "media-playback-pause",
        "media-playback-start",
        "media-playback-stop",
        "media-playlist-repeat",
        "media-playlist-shuffle",
        "media-record",
        "media-skip-backward",
        "media-skip-forward",
        "multimedia-volume-control",
        "preferences-system",
        "process-stop",
        "system-run",
        "text-x-generic",
        "user-desktop",
        "user-home",
        "user-trash",
        "view-refresh",
        "view-sort-ascending",
        "view-sort-descending",
        "window-close"
    };

    static const char * const toolbar_icons[] = {
        "fauxdacious",
        "audio-volume-high",
        "audio-volume-low",
        "audio-volume-medium",
        "audio-volume-muted",
        "document-open",
        "edit-find",
        "list-add",
        "media-playback-pause",
        "media-playback-start",
        "media-playback-stop",
        "media-playlist-repeat",
        "media-playlist-shuffle",
        "media-record",
        "media-skip-backward",
        "media-skip-forward"
    };

    static const char * const dialog_icons[] = {
        "dialog-error",
        "dialog-information",
        "dialog-question",
        "dialog-warning"
    };

    /* keep this in sync with the list in prefs-window.cc */
    static const char * const category_icons[] = {
        "applications-graphics",
        "applications-internet",
        "applications-system",
        "fauxdacious", /* for window icons */
        "audio-volume-medium",
        "audio-x-generic", /* also used for fallback album art */
        "dialog-information",
        "preferences-system"
    };

    g_resources_register (images_get_resource ());

#ifdef _WIN32
    adjust_icon_sizes ();
#endif

    int menu_size = get_icon_size (GTK_ICON_SIZE_MENU);
    for (const char * icon : all_icons)
        load_fallback_icon (icon, menu_size);

    GtkIconSize icon_size;
    GtkSettings * settings = gtk_settings_get_default ();
    g_object_get (settings, "gtk-toolbar-icon-size", & icon_size, nullptr);

    int toolbar_size = get_icon_size (icon_size);
    for (const char * icon : toolbar_icons)
        load_fallback_icon (icon, toolbar_size);

    int dialog_size = get_icon_size (GTK_ICON_SIZE_DIALOG);
    for (const char * icon : dialog_icons)
        load_fallback_icon (icon, dialog_size);

    int category_size = audgui_to_native_dpi (48);
    for (const char * icon : category_icons)
        load_fallback_icon (icon, category_size);
}

static void playlist_set_playing_cb (void *, void *)
{
    audgui_pixbuf_uncache ();
}

static void playlist_position_cb (void * list, void *)
{
    if (aud::from_ptr<int> (list) == aud_playlist_get_playing ())
        audgui_pixbuf_uncache ();
}

EXPORT void audgui_init ()
{
    static bool icons_loaded = false;
    assert (aud_get_mainloop_type () == MainloopType::GLib);

    if (init_count ++)
        return;

    static char app_name[] = "fauxdacious";
    static char * app_args[] = {app_name, nullptr};
    int dummy_argc = 1;
    char * * dummy_argv = app_args;

    /* JWT:MUST INITIALIZE SDL WINDOW **BEFORE** GTK! */
#ifdef USE_SDL2
    if (! SDL_WasInit (SDL_INIT_VIDEO))
    {
        if (SDL_InitSubSystem (SDL_INIT_VIDEO))
            AUDERR ("e:Failed to init SDL (no video playing): %s.\n", SDL_GetError ());
        else
        {
            Uint32 flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;
            if (aud_get_bool ("ffaudio", "allow_highdpi"))
                flags |= SDL_WINDOW_ALLOW_HIGHDPI;

            SDL_Window * sdl_window = SDL_CreateWindow ("Fauxdacious Video", SDL_WINDOWPOS_UNDEFINED,
                SDL_WINDOWPOS_UNDEFINED, 1, 1, flags);
            if (! sdl_window)
                AUDERR ("Failed to create SDL window (no video playing): %s.\n", SDL_GetError ());
            else
            {
#if SDL_COMPILEDVERSION >= 2004
                SDL_SetHint (SDL_HINT_VIDEO_X11_NET_WM_PING, "0");
#endif
                fauxd_set_sdl_window (sdl_window);
            }
        }
    }
#endif

    gtk_init (& dummy_argc, & dummy_argv);

    if (! icons_loaded)
    {
        load_fallback_icons ();
        icons_loaded = true;
    }

    aud_config_set_defaults ("audgui", audgui_defaults);

    if (aud_get_bool ("gtkui", "equalizer_visible"))
        audgui_show_equalizer_window ();

    status_init ();

    hook_associate ("playlist set playing", playlist_set_playing_cb, nullptr);
    hook_associate ("playlist position", playlist_position_cb, nullptr);

#ifndef _WIN32
    gtk_window_set_default_icon_name ("fauxdacious");
#endif
}

EXPORT void audgui_cleanup ()
{
    if (-- init_count)
        return;

    bool equalizer_was_visible = aud_get_bool ("gtkui", "equalizer_visible");

    hook_dissociate ("playlist set playing", playlist_set_playing_cb);
    hook_dissociate ("playlist position", playlist_position_cb);

    status_cleanup ();

    for (int id = 0; id < AUDGUI_NUM_UNIQUE_WINDOWS; id ++)
        audgui_hide_unique_window (id);

    if (equalizer_was_visible)
        aud_set_bool ("gtkui", "equalizer_visible", true);

    audgui_hide_prefs_window ();
    audgui_infopopup_hide ();

    plugin_menu_cleanup ();
    plugin_prefs_cleanup ();

#ifdef USE_SDL2
    SDL_Window * sdl_window = fauxd_get_sdl_window ();
    if (sdl_window)
    {
        SDL_DestroyWindow (sdl_window);
        sdl_window = nullptr;
    }
    if (SDL_WasInit (SDL_INIT_VIDEO))
        SDL_QuitSubSystem (SDL_INIT_VIDEO);  // SDL DOCS SAY SDL_Quit () SAFE, BUT SEGFAULTS HERE?!
#endif
}
