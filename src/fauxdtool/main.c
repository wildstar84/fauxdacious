/*
 * main.c
 * Copyright 2005-2013 George Averill, William Pitcock, Yoshiki Yazawa, and
 *                     John Lindgren
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "fauxdtool.h"

static char * which_instance;

const struct commandhandler handlers[] =
{
    {"<sep>", NULL, "Current song information", 0},
    {"instance", set_instance_tag, "to a specific instance (fauxdacious)", 1},
    {"current-song", get_current_song, "print formatted song title", 0},
    {"current-song-filename", get_current_song_filename, "print song file name", 0},
    {"current-song-length", get_current_song_length, "print song length", 0},
    {"current-song-length-seconds", get_current_song_length_seconds, "print song length in seconds", 0},
    {"current-song-length-frames", get_current_song_length_frames, "print song length in milliseconds", 0},
    {"current-song-output-length", get_current_song_output_length, "print playback time", 0},
    {"current-song-output-length-seconds", get_current_song_output_length_seconds, "print playback time in seconds", 0},
    {"current-song-output-length-frames", get_current_song_output_length_frames, "print playback time in milliseconds", 0},
    {"current-song-bitrate", get_current_song_bitrate, "print bitrate in bits per second", 0},
    {"current-song-bitrate-kbps", get_current_song_bitrate_kbps, "print bitrate in kilobits per second", 0},
    {"current-song-frequency", get_current_song_frequency, "print sampling rate in hertz", 0},
    {"current-song-frequency-khz", get_current_song_frequency_khz, "print sampling rate in kilohertz", 0},
    {"current-song-channels", get_current_song_channels, "print number of channels", 0},
    {"current-song-tuple-data", get_current_song_tuple_field_data, "print value of named field", 1},
    {"current-song-info", get_current_song_info, "print bitrate, sampling rate, and channels", 0},

    {"<sep>", NULL, "Playback commands", 0},
    {"playback-play", playback_play, "start/restart/unpause playback", 0},
    {"playback-pause", playback_pause, "pause/unpause playback", 0},
    {"playback-playpause", playback_playpause, "start/pause/unpause playback", 0},
    {"playback-stop", playback_stop, "stop playback", 0},
    {"playback-playing", playback_playing, "exit code = 0 if playing", 0},
    {"playback-paused", playback_paused, "exit code = 0 if paused", 0},
    {"playback-stopped", playback_stopped, "exit code = 0 if not playing", 0},
    {"playback-status", playback_status, "print status (playing/paused/stopped)", 0},
    {"playback-seek", playback_seek, "seek to given time", 1},
    {"playback-seek-relative", playback_seek_relative, "seek to relative time offset", 1},
    {"playback-record", playback_record, "toggle stream recording", 0},
    {"playback-recording", playback_recording, "exit code = 0 if recording", 0},
    {"playback-pausemute", playback_setpausemute, "set pause to just mute", 1},
    {"playback-doespausemute", playback_getpausemute, "exit code = 1 if pause set to just mute", 0},

    {"<sep>", NULL, "Playlist commands", 0},
    {"playlist-advance", playlist_advance, "skip to next song", 0},
    {"playlist-reverse", playlist_reverse, "skip to previous song", 0},
    {"playlist-addurl", playlist_add_url_string, "add URI at end of playlist", 1},
    {"playlist-insurl", playlist_ins_url_string, "insert URI at given position", 2},
    {"playlist-addurl-to-new-playlist", playlist_enqueue_to_temp, "open URI in \"Now Playing\" playlist", 1},
    {"playlist-delete", playlist_delete, "remove song at given position", 1},
    {"playlist-length", playlist_length, "print number of songs in playlist", 0},
    {"playlist-song", playlist_song, "print formatted title of given song", 1},
    {"playlist-song-filename", playlist_song_filename, "print file name of given song", 1},
    {"playlist-song-length", playlist_song_length, "print length of given song", 1},
    {"playlist-song-length-seconds", playlist_song_length_seconds, "print length of given song in seconds", 1},
    {"playlist-song-length-frames", playlist_song_length_frames, "print length of given song in milliseconds", 1},
    {"playlist-tuple-data", playlist_tuple_field_data, "print value of named field for given song", 2},
    {"playlist-display", playlist_display, "print all songs in playlist", 0},
    {"playlist-position", playlist_position, "print position of current song", 0},
    {"playlist-jump", playlist_jump, "skip to given song", 1},
    {"playlist-clear", playlist_clear, "clear playlist", 0},
    {"playlist-auto-advance-status", playlist_auto_advance_status, "query playlist auto-advance", 0},
    {"playlist-auto-advance-toggle", playlist_auto_advance_toggle, "toggle playlist auto-advance", 0},
    {"playlist-repeat-status", playlist_repeat_status, "query playlist repeat", 0},
    {"playlist-repeat-toggle", playlist_repeat_toggle, "toggle playlist repeat", 0},
    {"playlist-shuffle-status", playlist_shuffle_status, "query playlist shuffle", 0},
    {"playlist-shuffle-toggle", playlist_shuffle_toggle, "toggle playlist shuffle", 0},
    {"playlist-stop-after-status", playlist_stop_after_status, "query if stopping after current song", 0},
    {"playlist-stop-after-toggle", playlist_stop_after_toggle, "toggle if stopping after current song", 0},

    {"<sep>", NULL, "More playlist commands", 0},
    {"number-of-playlists", number_of_playlists, "print number of playlists", 0},
    {"current-playlist", current_playlist, "print number of current playlist", 0},
    {"play-current-playlist", play_current_playlist, "play/resume current playlist", 0},
    {"set-current-playlist", set_current_playlist, "make given playlist current", 1},
    {"current-playlist-name", playlist_title, "print current playlist title", 0},
    {"set-current-playlist-name", set_playlist_title, "set current playlist title", 1},
    {"new-playlist", new_playlist, "insert new playlist", 0},
    {"delete-current-playlist", delete_current_playlist, "remove current playlist", 0},

    {"<sep>", NULL, "Playlist queue commands", 0},
    {"playqueue-add", playqueue_add, "add given song to queue", 1},
    {"playqueue-remove", playqueue_remove, "remove given song from queue", 1},
    {"playqueue-is-queued", playqueue_is_queued, "exit code = 0 if given song is queued", 1},
    {"playqueue-get-queue-position", playqueue_get_queue_position, "print queue position of given song", 1},
    {"playqueue-get-list-position", playqueue_get_list_position, "print n-th queued song", 1},
    {"playqueue-length", playqueue_length, "print number of songs in queue", 0},
    {"playqueue-display", playqueue_display, "print all songs in queue", 0},
    {"playqueue-clear", playqueue_clear, "clear queue", 0},

    {"<sep>", NULL, "Volume control and equalizer", 0},
    {"get-volume", get_volume, "print current volume level in percent", 0},
    {"set-volume", set_volume, "set current volume level in percent", 1},
    {"set-volume-relative", set_volume_relative, "change current volume level by percent", 1},
    {"equalizer-activate", equalizer_active, "activate/deactivate equalizer", 1},
    {"equalizer-get", equalizer_get_eq, "print equalizer settings", 0},
    {"equalizer-set", equalizer_set_eq, "set equalizer settings", 11},
    {"equalizer-get-preamp", equalizer_get_eq_preamp, "print equalizer pre-amplification", 0},
    {"equalizer-set-preamp", equalizer_set_eq_preamp, "set equalizer pre-amplification", 1},
    {"equalizer-get-band", equalizer_get_eq_band, "print gain of given equalizer band", 1},
    {"equalizer-set-band", equalizer_set_eq_band, "set gain of given equalizer band", 2},

    {"<sep>", NULL, "Miscellaneous", 0},
    {"mainwin-show", mainwin_show, "show/hide Fauxdacious", 1},
    {"filebrowser-show", show_filebrowser, "show/hide Add Files window", 1},
    {"jumptofile-show", show_jtf_window, "show/hide Jump to Song window", 1},
    {"preferences-show", show_preferences_window, "show/hide Settings window", 1},
    {"about-show", show_about_window, "show/hide About window", 1},

    {"version", get_version, "print Fauxdacious version", 0},
    {"plugin-is-enabled", plugin_is_enabled, "exit code = 0 if plugin is enabled", 1},
    {"plugin-enable", plugin_enable, "enable/disable plugin", 2},
    {"shutdown", shutdown_audacious_server, "shut down Fauxdacious", 0},

    {"help", get_handlers_list, "print this help", 0},

    {NULL, NULL, NULL, 0}
};

ObjFauxdacious * dbus_proxy = NULL;
static GDBusConnection * connection = NULL;

static void fauxdtool_disconnect (void)
{
    g_object_unref (dbus_proxy);
    dbus_proxy = NULL;

    g_dbus_connection_close_sync (connection, NULL, NULL);
    connection = NULL;
}

static void fauxdtool_connect (void)
{
    GError * error = NULL;

    connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, & error);

    if (! connection)
    {
        fprintf (stderr, "D-Bus error: %s\n", error->message);
        g_error_free (error);
        exit (EXIT_FAILURE);
    }

    char instname [64];  /* ALLOW INSTANCE NAME UP TO 40 CHARS (TO AVOID CHANCE OF OVERFLOW!) */
    memset (instname, '\0', sizeof (instname));
    /* JWT:40 = 64 - len("org.atheme.fauxdacious-")+1 # IF THIS CHANGES, MUST CHANGE 40! */
    if (which_instance && which_instance[0] && sizeof (which_instance) <= 40)
        sprintf (instname, "org.atheme.fauxdacious-%s", which_instance);
    else
        strcpy (instname, "org.atheme.fauxdacious");

    dbus_proxy = obj_fauxdacious_proxy_new_sync (connection, 0,
            instname, "/org/atheme/fauxdacious", NULL, & error);

    if (! dbus_proxy)
    {
        fprintf (stderr, "D-Bus error: %s\n", error->message);
        g_error_free (error);
        g_dbus_connection_close_sync (connection, NULL, NULL);
        exit (EXIT_FAILURE);
    }

    atexit (fauxdtool_disconnect);
}

#ifdef _WIN32
static void print_utf8 (const char * str)
{
    HANDLE h = GetStdHandle (STD_OUTPUT_HANDLE);
    DWORD mode;

    if (h != INVALID_HANDLE_VALUE && GetConsoleMode (h, & mode))
    {
        // stdout is the console, which needs special handling to display
        // non-ASCII characters correctly (in 2018, the MS runtime *still*
        // doesn't handle UTF-8 reliably).
        glong n_converted;
        gunichar2 * utf16 = g_utf8_to_utf16 (str, -1, NULL, & n_converted, NULL);

        if (utf16)
        {
            DWORD n_written;
            WriteConsoleW (h, utf16, n_converted, & n_written, NULL);
            g_free (utf16);
        }
    }
    else
    {
        // fputs() works fine when stdout is redirected.
        fputs (str, stdout);
    }
}
#endif // _WIN32

int main (int argc, char * * argv)
{
    int i, j = 0, k = 0;

    setlocale (LC_CTYPE, "");

    g_type_init();

#ifdef _WIN32
    g_set_print_handler (print_utf8);
#endif

    /* JWT:MOVED TO AFTER ARGS PROCESSED!:  fauxdtool_connect (); */

    if (argc < 2)
    {
        fprintf (stderr, "Not enough parameters.  Try \"fauxdtool help\".\n");
        exit (EXIT_FAILURE);
    }

    for (j = 1; j < argc; j ++)
    {
        if ((! g_ascii_strcasecmp ("instance", argv[j]) 
                || ! g_ascii_strcasecmp ("--instance", argv[j])) 
                && g_ascii_strcasecmp ("<sep>", "instance"))
        {
            /* HANDLE "[--]instance <name>" ARGUMENTS: */
            int numargs = 2 < argc - j ? 3 : argc - j;
            set_instance_tag (numargs, & argv[j]);
            j += 1;
            if (j >= argc)
                break;
        }
        else if (argc >= 2 && argv[j][0] == '-' && argv[j][1] >= '1' && argv[j][1] <= '9' && ! argv[j][2])
        {
            /* HANDLE AUDACIOUS-ISH NUMERIC INSTANCE ARGUMENT (-#): */
            int numargs = 2 < argc - j ? 3 : argc - j;
            set_instance_tag (numargs, & argv[j-1]);
            j += 1;
            if (j >= argc)
                break;
        }
        else if (! g_ascii_strcasecmp ("help", argv[j]) ||  /* JWT:DON'T THROW DBUS ERROR WHEN NOT CONNECTED & JUST ASKING FOR HELP! */
                ! g_ascii_strcasecmp ("--help", argv[j]) || ! g_ascii_strcasecmp ("-h", argv[j]))
        {
            /* HANDLE EVERYTHING ELSE (EXCEPT [--]h[elp], WHICH IS TREATED SAME AS DEFAULT (noop)): */
            get_handlers_list (1, & argv[j]);
            exit (0);
        }
    }

    fauxdtool_connect ();

    for (j = 1; j < argc; j ++)
    {
        for (i = 0; handlers[i].name != NULL; i++)
        {
            if ((! g_ascii_strcasecmp (handlers[i].name, argv[j]) 
                    || ! g_ascii_strcasecmp (g_strconcat ("--", handlers[i].name, NULL), argv[j])) 
                    && g_ascii_strcasecmp ("<sep>", handlers[i].name))
            {
            	   if (! g_ascii_strcasecmp (handlers[i].name, "instance"))
            	       continue;
                /* int numargs = handlers[i].args + 1 < argc - j ? handlers[i].args + 1 : argc - j; */
                int numargs = MIN (handlers[i].args + 1, argc - j);
                handlers[i].handler (numargs, & argv[j]);
                j += handlers[i].args;
                k ++;

                if (j >= argc)
                    break;
            }
        }
    }

    if (k == 0)
    {
        fprintf (stderr, "Unknown command \"%s\".  Try \"fauxdtool help\".\n", argv[1]);
        exit (EXIT_FAILURE);
    }

    return 0;
}

void set_instance_tag (int argc, char * * argv)
{
	which_instance = (argv[1][0] == '-' && argv[1][1]) ? argv[1]+1 : argv[1];
}
