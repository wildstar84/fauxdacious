/*
 * preset-browser.c
 * Copyright 2014-2015 John Lindgren
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

#define AUD_GLIB_INTEGRATION
#include "internal.h"
#include "libfauxdgui.h"
#include "preset-browser.h"

#include <string.h>
#include <gtk/gtk.h>

#include <libfauxdcore/audstrings.h>
#include <libfauxdcore/drct.h>
#include <libfauxdcore/equalizer.h>
#include <libfauxdcore/i18n.h>
#include <libfauxdcore/vfs.h>
#include <libfauxdcore/playlist.h>
#include <libfauxdcore/runtime.h>

typedef void (* PresetAction) (const char * filename, const EqualizerPreset * preset);

static void browser_response (GtkWidget * dialog, int response, void * data)
{
    if (response == GTK_RESPONSE_ACCEPT)
    {
        CharPtr filename (gtk_file_chooser_get_uri ((GtkFileChooser *) dialog));
        auto preset = (const EqualizerPreset *)
                g_object_get_data ((GObject *) dialog, "eq-preset");

        ((PresetAction) data) (filename, preset);
    }

    gtk_widget_destroy (dialog);
}

static void set_default_preset_dir ()
{
    aud_set_str (nullptr, "_preset_dir", aud_get_path (AudPath::UserDir));
    if (aud_get_bool (nullptr, "try_local_preset_files"))
    {
        int current_playlist = aud_playlist_get_playing ();
        if (current_playlist >= 0)
        {
            int current_song = aud_playlist_get_position (current_playlist);
            if (current_song >= 0)
            {
                String filename = aud_playlist_entry_get_filename (current_playlist, current_song);

                StringBuf scheme = uri_get_scheme ((const char *) filename);
                if (aud_get_bool (nullptr, "try_local_preset_files") && ! strcmp (scheme, "file"))
                {
                    StringBuf path = filename_get_parent ((const char *) uri_to_filename (filename));
                    aud_set_str (nullptr, "_preset_dir", (const char *) path);
                }
            }
        }
    }
}

static void show_preset_browser (const char * title, gboolean save,
        const char * default_filename, PresetAction callback,
        const EqualizerPreset * preset)
{
    GtkFileFilter * filter;

    GtkWidget * browser = gtk_file_chooser_dialog_new (title, nullptr, save ?
     GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN, _("Cancel"),
     GTK_RESPONSE_CANCEL, save ? _("Save") : _("Load"), GTK_RESPONSE_ACCEPT,
     nullptr);
    filter = gtk_file_filter_new ();
    gtk_file_filter_set_name (filter, _("Preset/Eq Files"));
    gtk_file_filter_add_pattern (filter, _("*.preset"));
    gtk_file_filter_add_pattern (filter, _("*.eqf"));
    gtk_file_filter_add_pattern (filter, _("*.q1"));
    gtk_file_chooser_add_filter ((GtkFileChooser *) browser, filter);
    String set_preset_dir = aud_get_str (nullptr, "_preset_dir");
    if (! set_preset_dir || ! set_preset_dir[0])
        set_preset_dir = String (aud_get_path (AudPath::UserDir));
    gtk_file_chooser_set_current_folder_uri ((GtkFileChooser *) browser,
            filename_to_uri (set_preset_dir));
    gtk_file_chooser_set_local_only ((GtkFileChooser *) browser, false);

    if (default_filename)
        gtk_file_chooser_set_current_name ((GtkFileChooser *) browser, default_filename);

    if (preset)
        g_object_set_data_full ((GObject *) browser, "eq-preset",
         new EqualizerPreset (* preset), aud::delete_obj<EqualizerPreset>);

    g_signal_connect (browser, "response", (GCallback) browser_response, (void *) callback);

    audgui_show_unique_window (AUDGUI_PRESET_BROWSER_WINDOW, browser);
}

static void do_load_file (const char * filename, const EqualizerPreset *)
{
    Index<EqualizerPreset> presets;
    presets.append ();

    VFSFile file (filename, "r");
    if (! file || ! aud_load_preset_file (presets[0], file))
        return;

    audgui_import_eq_presets (presets);
}

void eq_preset_load_file ()
{
    set_default_preset_dir ();
    show_preset_browser (_("Load Preset File - Fauxdacious"), false, nullptr, do_load_file, nullptr);
}

static void do_load_eqf (const char * filename, const EqualizerPreset *)
{
    VFSFile file (filename, "r");
    if (! file)
        return;

    audgui_import_eq_presets (aud_import_winamp_presets (file));
}

void eq_preset_load_eqf ()
{
    set_default_preset_dir ();
    show_preset_browser (_("Load EQF File - Fauxdacious"), false, nullptr, do_load_eqf, nullptr);
}

static void do_save_file (const char * filename, const EqualizerPreset * preset)
{
    // JWT:USE CURRENT INSTEAD OF FAIL:  g_return_if_fail (preset);
    EqualizerPreset current_preset;
    if (! preset)
    {
        aud_eq_update_preset (current_preset);
        preset = & current_preset;
    }

    VFSFile file (filename, "w");
    if (file)
    {
        aud_save_preset_file (* preset, file);

        // JWT:IF [Auto] IS ON && FILENAME SAVED == SONG AUTO (DEFAULT) PATH/SONGNAME.preset, LIGHT UP THE 
        // [PRESET] BUTTON INDICATING SONG-SPECIFIC EQUALIZATION IS IN EFFECT!

        if (aud_get_bool (nullptr, "equalizer_autoload") 
                && ! strcmp((const char *) str_decode_percent (aud_get_str(nullptr, "_eq_last_preset_filename"),-1), 
                (const char *) str_decode_percent (filename)))
        {
            aud_set_bool(nullptr, "equalizer_songauto", true);
            aud_set_str(nullptr, "_eq_last_preset_filename", "");
        }
    }
}

void eq_preset_save_file (const EqualizerPreset * preset)
{
    String filename;
    aud_set_str(nullptr, "_eq_last_preset_filename", "");
    aud_set_str (nullptr, "_preset_dir", aud_get_path (AudPath::UserDir));
    int current_playlist = aud_playlist_get_playing ();
    if (preset)  // AUDACIOUS 3.10+ SAYS SAVE USING SELECTED PRESET NAME, IF ONE SELECTED!:
    {
        set_default_preset_dir ();

        StringBuf name = str_concat ({preset->name, ".preset"});
        show_preset_browser (_("Save Preset File - Fauxdacious"), true, name, do_save_file, preset);

        return;
    }
    else if (current_playlist >= 0)  // JWT:NONE SELECTED, CALCULATE THE EXPORT NAME:
    {
        int current_song = aud_playlist_get_position (current_playlist);
        if (current_song >= 0)
        {
            if (aud_get_bool (nullptr, "try_local_preset_files") && aud_get_bool (nullptr, "_save_as_dirdefault"))
            {
                String eqpreset_dir_default_file = aud_get_str (nullptr, "eqpreset_dir_default_file");
                if (eqpreset_dir_default_file && eqpreset_dir_default_file[0])
                {
                    filename = aud_playlist_entry_get_filename (current_playlist, current_song);
                    StringBuf scheme = uri_get_scheme (filename);
                    if (! strcmp (scheme, "file"))  // JWT:WE'RE A "FILE" AND SAVE TO LOCAL DIR, AND SAVE AS DIR-PRESET:
                    {
                        StringBuf path = filename_get_parent (uri_to_filename (filename));
                        aud_set_str (nullptr, "_preset_dir", path);
                        String preset_file_namepart = eqpreset_dir_default_file;
                        aud_set_str (nullptr, "_eq_last_preset_filename", String (filename_to_uri
                                (str_concat ({aud_get_str (nullptr, "_preset_dir"), "/",
                                preset_file_namepart}))));
                        show_preset_browser (_("Save Preset File - Fauxdacious"), true,
                                preset_file_namepart, do_save_file, preset);

                        return;
                    }
                }
            }
            const char * slash;
            const char * base;
            filename = aud_playlist_entry_get_filename (current_playlist, current_song);
            const char * dross = aud_get_bool (nullptr, "eqpreset_nameonly") ? strstr (filename, "?") : nullptr;
            int ln = -1;
            StringBuf scheme = uri_get_scheme (filename);
            if (aud_get_bool (nullptr, "eqpreset_use_url_sitename")
                    && strcmp (scheme, "file") && strcmp (scheme, "stdin")
                    && strcmp (scheme, "cdda") && strcmp (scheme, "dvd"))
            {
                /* WE'RE A URL AND USER WANTS TO SAVE PRESETFILE FOR THE BASE SITE NAME, IE. "www.site.com": */
                slash = strstr (filename, "//");
                if (slash)
                {
                    slash+=2;
                    const char * endbase = strstr (slash, "/");
                    ln = endbase ? endbase - slash : -1;
                    String urlbase = String (str_copy (slash, ln));
                    auto split = str_list_to_index (slash, "?&#:/");
                    for (auto & str : split)
                    {
                        urlbase = String (str_copy (str));
                        break;
                    }
                    String preset_file_namepart = String (str_concat ({urlbase, ".preset"}));
                    aud_set_str (nullptr, "_eq_last_preset_filename", String (filename_to_uri
                            (str_concat ({aud_get_path (AudPath::UserDir), "/",
                            preset_file_namepart}))));
                    show_preset_browser (_("Save Preset File - Fauxdacious"), true, preset_file_namepart,
                            do_save_file, preset);

                    return;
                }
            }
            /* JWT: EXTRACT JUST THE "NAME" PART (URLs MAY END W/"/") TO USE TO NAME THE EQ. FILE: */
            slash = filename ? strrchr (filename, '/') : nullptr;
            if (slash && dross && slash > dross)
            {
                slash = dross;
                while (slash > filename && slash[0] != '/')
                {
                    --slash;
                }
                if (slash[0] != '/')
                    slash = nullptr;
            }
            base = slash ? slash + 1 : nullptr;
            if (slash && (!base || base[0] == '\0'))  // FILENAME (URL) ENDS IN A "/"!
            {
                do
                {
                    --slash;
                    ++ln;
                } while (slash && slash > filename && slash[0] != '/');
                base = slash ? slash + 1 : (const char *)filename;
                if (ln > 0)
                {
                    String preset_file_namepart = String (str_concat ({str_encode_percent (base, ln), ".preset"}));
                    aud_set_str (nullptr, "_eq_last_preset_filename", String (filename_to_uri
                            (str_concat ({aud_get_path (AudPath::UserDir), "/",
                            preset_file_namepart}))));
                    show_preset_browser (_("Save Preset File - Fauxdacious"), true, preset_file_namepart,
                            do_save_file, preset);

                    return;
                }
            }
            else if (base && base[0] != '\0' && strncmp (base, "-.", 2))
            {
                const char * iscue = dross ? dross : strstr (filename, "?");
                StringBuf scheme = uri_get_scheme (filename);
                if (aud_get_bool (nullptr, "try_local_preset_files") && ! strcmp (scheme, "file"))
                {
                    StringBuf path = filename_get_parent (uri_to_filename (filename));
                    aud_set_str (nullptr, "_preset_dir", (const char *) path);
                }
                if (iscue && base[0] != '?')  // WE'RE A CUE-SHEET FILE:
                {
                    /* JWT:SONGS FROM CUE FILES HAVE A TRAILING "?<cue#>" THAT'S NOT ON THE FILENAME IN output.cc
                        SO WE HAVE TO STRIP IT OFF THE "filename" HERE, BUT ONLY IF WE'RE A "file://..." SCHEME,
                        LEST WE WHACK OFF A URL LIKE "https://www.youtube.com/watch?t=4&v=BaW_jenozKc"!
                        THE DRAWBACK W/THIS IS THAT ALL CUES OF THE SAME BASE FILE NAME WILL HAVE THE SAME
                        EQ. PRESET, BUT THE ALTERNATIVE IS THAT EQ. PRESETS WON'T WORK AT ALL FOR CUE-BASED
                        FILES, SINCE WE DON'T SEEM TO HAVE THE <cue#> IN output.cc for output_open_audio()!
                    */
                    if (dross || ! strcmp (scheme, "file"))
                    {
                        int ln = iscue - base;
                        String preset_file_namepart = String (str_concat ({str_encode_percent (base, ln), ".preset"}));
                        aud_set_str (nullptr, "_eq_last_preset_filename", String (filename_to_uri
                                (str_concat ({aud_get_str (nullptr, "_preset_dir"), "/",
                                preset_file_namepart}))));
                        show_preset_browser (_("Save Preset File - Fauxdacious"), true,
                                preset_file_namepart, do_save_file, preset);

                        return;
                    }
                }
                String preset_file_namepart = String (str_concat ({str_encode_percent (base, -1), ".preset"}));
                if (! strcmp (scheme, "cdda") || ! strcmp (scheme, "dvd"))
                {
                    String playingdiskid = aud_get_str (nullptr, "playingdiskid");
                    if (playingdiskid[0])
                        preset_file_namepart = String (str_concat ({playingdiskid, ".preset"}));
                }
                aud_set_str (nullptr, "_eq_last_preset_filename", String (filename_to_uri
                        (str_concat ({aud_get_str (nullptr, "_preset_dir"), "/",
                        preset_file_namepart}))));
                show_preset_browser (_("Save Preset File - Fauxdacious"), true, preset_file_namepart,
                        do_save_file, preset);

                return;
            }
        }
    }

    StringBuf name = preset ? str_concat ({preset->name, ".preset"})
            : str_copy("<NAMEME!>.preset");
    show_preset_browser (_("Save Preset File - Fauxdacious"), true, name, do_save_file, preset);
}

static void do_save_eqf (const char * filename, const EqualizerPreset * preset)
{
    g_return_if_fail (preset);

    VFSFile file (filename, "w");
    if (! file)
        return;

    aud_export_winamp_preset (* preset, file);
}

void eq_preset_save_eqf (const EqualizerPreset * preset)
{
    set_default_preset_dir ();

    StringBuf name = preset ? str_concat ({preset->name, ".eqf"})
            : str_copy("<NAMEME!>.eqf");
    show_preset_browser (_("Save EQF File - Fauxdacious"), true, name, do_save_eqf, preset);
}
