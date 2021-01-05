/*
 * main.c
 * Copyright 2007-2013 William Pitcock and John Lindgren
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <winbase.h>
#define SDL_MAIN_HANDLED
#endif

#ifdef USE_SDL2
#include <libfauxdcore/sdl_window.h>
#endif

#define AUD_GLIB_INTEGRATION
#include <libfauxdcore/audstrings.h>
#include <libfauxdcore/drct.h>
#include <libfauxdcore/hook.h>
#include <libfauxdcore/i18n.h>
#include <libfauxdcore/interface.h>
#include <libfauxdcore/mainloop.h>
#include <libfauxdcore/playlist.h>
#include <libfauxdcore/runtime.h>
#include <libfauxdcore/tuple.h>
#include <libfauxdcore/audstrings.h>
#include <libfauxdcore/vfs.h>

#ifdef USE_DBUS
#include "aud-dbus.h"
#endif

#include "main.h"
#include "util.h"

bool jwt_norepeat = false;  /* JWT:SAVE PRE-CONFIGURED STATES IF WE OR CMD-LINES ALTER (MAKE ONE-SHOT ONLY). */
bool resetEqState;          /* JWT:SAVE EQUALIZER STATE FOR RESET IF USER FORCES IT TEMPORARILY ON. */
bool resetRepeatToOn;       /* JWT:SAVE REPEAT STATUS FOR RESET IF STDIN-PIPING FORCES IT TEMPORARILY OFF. */
bool resetRecordEnabled;    /* JWT:SAVE RECORDING-ENABLED STATUS (IF -R FORCES IT ON) */

static struct {
    int help, version;
    int play, pause, play_pause, stop, fwd, rew;
    int enqueue, enqueue_to_temp;
    int mainwin, show_jump_box;
    int headless, quit_after_play;
    int verbose;
#if defined(USE_QT) && defined(USE_GTK)
    int gtk;
    int qt;
#endif
    int clearplaylist, newinstance, pauseismute, forcenoequalizer, forcenogainchg, deleteallplaylists,
            force_recording, outstd;  /* JWT:NEW COMMAND-LINE OPTIONS */
} options;

static bool initted = false;
static Index<PlaylistAddItem> filenames;
static int starting_gain = 0;           /* JWT:ADD OPTIONAL STARTING GAIN ADJUSTMENT */
String instancename = String ();        /* JWT:ADD OPTIONAL INSTANCE NAME */
String out_ext = String ();             /* JWT:ADD OPTIONAL STDOUT (FILEWRITER) EXTENSION FOR STDOUT */

static const struct {
    const char * long_arg;
    char short_arg;
    int * value;
    const char * desc;
} arg_map[] = {
    {"clear", 'c', & options.clearplaylist, N_("Clear Playlist")},
    {"delete", 'D', & options.deleteallplaylists, N_("DELETE all playlists!")},
    {"enqueue", 'e', & options.enqueue, N_("Add files to the playlist")},
    {"enqueue-to-temp", 'E', & options.enqueue_to_temp, N_("Add files to a temporary playlist")},
    {"fwd", 'f', & options.fwd, N_("Skip to next song")},
    {"gain", 'g', & options.forcenogainchg, N_("Fudge Additional Gain Adjustment")},  /* JWT:ADD OPTIONAL STARTING GAIN ADJUSTMENT */
#if defined(USE_QT) && defined(USE_GTK)
    {"gtk", 'G', &options.gtk, N_("Run in GTK mode (default)")},
#endif
    {"help", 'h', & options.help, N_("Show command-line help")},
    {"headless", 'H', & options.headless, N_("Start without a graphical interface")},
    {"show-jump-box", 'j', & options.show_jump_box, N_("Display the jump-to-song window")},
    {"show-main-window", 'm', & options.mainwin, N_("Display the main window")},
    {"new", 'n', & options.newinstance, N_("New Instance:  number, name, or unnamed (ie: -# | --new=name | -n)")}, 
    {"out", 'o', & options.outstd, N_("Output to stdout (extension)")},  /* JWT:FORCE PIPING TO STDOUT, (default "wav"). */
    {"play", 'p', & options.play, N_("Start playback")},
    {"pause-mute", 'P', & options.pauseismute, N_("Pause mutes instead of pausing")},  /* JWT:ADD PAUSEMUTE OPTION */
    {"quit-after-play", 'q', & options.quit_after_play, N_("Quit on playback stop")},
#if defined(USE_QT) && defined(USE_GTK)
    {"qt", 'Q', & options.qt, N_("Run in Qt mode")},
#endif
    {"rew", 'r', & options.rew, N_("Skip to previous song")},
    {"force_recording", 'R', & options.force_recording, N_("Start Recording")},
    {"stop", 's', & options.stop, N_("Stop playback")},
    {"play-pause", 't', & options.play_pause, N_("Pause if playing, play otherwise")},
    {"pause", 'u', & options.pause, N_("Pause playback")},
    {"version", 'v', & options.version, N_("Show version")},
    {"verbose", 'V', & options.verbose, N_("Print debugging messages (may be used twice)")},
    {"no-equalizer", 'z', & options.forcenoequalizer, N_("Force Equalizer Off")},
};

/* JWT:ADDED TO ALLOW A LIST OF FILES TO BE PIPED IN VIA STDIN *VERY EARLY* (BEFORE DBUS CAN REDIRECT
   TO ANY REMOTE (ALREADY-RUNNING) SESSION), OTHERWISE, STDIN IS LOST!  THIS PERMITS APPENDING
   (ENQUEUEING) ADDITIONAL FILES VIA STDIN TO AN ALREADY RUNNING SESSION!  THIS IS ACCOMPLISHED BY:
   EXAMPLE:  ls somefiles | fauxdacious -Dc -- & #THEN LATER: ls somemorefiles | fauxdacious -e --
*/
static void get_files_from_stdin ()
{
    VFSFile file;
    file = VFSFile ("stdin://", "r");
    if (file)
    {
        Index<char> text = file.read_all ();
        if (text.len ())
        {
            text.append (0);  /* null-terminate */
            Index<String> inlines = str_list_to_index ((const char *) text.begin (), "\n");
            int linecnt = inlines.len ();
            if (linecnt > 0)
            {
                String uri;
                CharPtr cur (g_get_current_dir ());
                for (int i = 0; i < linecnt; i ++)
                {
                    if (strstr (inlines[i], "://"))
                        uri = String (inlines[i]);
                    else if (g_path_is_absolute (inlines[i]))
                        uri = String (filename_to_uri (inlines[i]));
                    else
                        uri = String (filename_to_uri (filename_build ({cur, inlines[i]})));

                    if (uri)
                        filenames.append (uri);
                }
            }
        }
    }
};

static bool parse_options (int argc, char * * argv)
{
    CharPtr cur (g_get_current_dir ());

#ifdef _WIN32
    Index<String> args = get_argv_utf8 ();

    for (int n = 1; n < args.len (); n ++)
    {
        const char * arg = args[n];
#else
    for (int n = 1; n < argc; n ++)
    {
        const char * arg = argv[n];
#endif

        if (arg[0] != '-')  /* filename */
        {
            String uri;

            if (strstr (arg, "://"))  /* uri */
            {
                uri = String (arg);
                if (strstr (arg, "stdin://"))  /* we're stdin */
                {
                    if (! strncmp (arg, "stdin://--", 10))  /* we're stdin://-- (EARLY file load) */
                        get_files_from_stdin ();
                    else
                    {
                        if (strncmp (arg, "stdin://-.", 10))  /* we're NOT stdin://-.<ext> ("OLD Way"), so */
                            uri = String ("stdin://");  /* must be "stdin://-" ("NEW Way"): convert to "stdin://" */
                        jwt_norepeat = true;
                    }
                }
            }
            else if (g_path_is_absolute (arg))
                uri = String (filename_to_uri (arg));
            else
                uri = String (filename_to_uri (filename_build ({cur, arg})));

            filenames.append (uri);
        }
        else if (! arg[1])       /* "-" (standard input - "NEW Way") */
        {
            filenames.append (String ("stdin://"));
            jwt_norepeat = true;
        }
        else if (arg[1] == '.')  /* "-.<ext> (standard input w/extension) ("OLD Way") */
        {
            filenames.append (String (str_concat ({"stdin://", arg})));
            jwt_norepeat = true;
        }
        else if (arg[1] == '-')  /* --long option */
        {
            if (! arg[2])        /* "--" - Get list of files from stdin EARLY! */
                get_files_from_stdin ();
            else                 /* "--<option>[=value]" (long option) */
            {
                bool found = false;

                for (auto & arg_info : arg_map)
                {
                    /* JWT:REPLACED BY NEXT 12 TO ALLOW LONG OPTIONS TO INCLUDE ARGUMENT:  if (! strcmp (arg + 2, arg_info.long_arg)) */
                    if (! strncmp (arg + 2, arg_info.long_arg, strlen (arg_info.long_arg)))
                    {
                        (* arg_info.value) ++;
                        found = true;
                        const char * parmpos = strstr (arg + 2, "=");
                        if ( parmpos )
                        {
                            ++parmpos;
                            if (! strcmp (arg_info.long_arg, "new"))           /* JWT:ADD OPTIONAL INSTANCE NAME */
                                instancename = String (parmpos);
                            else if (! strcmp (arg_info.long_arg, "out"))      /* JWT:ADD OPTIONAL STDOUT OUTPUT */
                                out_ext = String (parmpos);
                            else if (! strcmp (arg_info.long_arg, "gain"))  /* JWT:ADD OPTIONAL STARTING GAIN ADJUSTMENT */
                                starting_gain = atoi (parmpos);
                        }
                        break;
                    }
                }

                if (! found)
                {
                    fprintf (stderr, _("Unknown option: %s\n"), arg);
                    return false;
                }
            }
        }
        else  /* -short option list (single letters and/or digit) */
        {
            for (int c = 1; arg[c]; c ++)  /* loop to check each character in the option string */
            {
                bool found = false;

                for (auto & arg_info : arg_map)  /* See if we're a valid option letter */
                {
                    if (arg[c] == arg_info.short_arg)  /* ("-<alpha>") valid option letter found */
                    {
                        (* arg_info.value) ++;
                        found = true;
                        break;
                    }
                }

                if (! found)   /* we're not a valid letter, see if we're a digit?: */
                {
                    if (arg[c] >= '0' && arg[c] <= '9')  /* ("-#") digit (instance number) found */
                    {
                        if (arg[c] > '0')  /* new (Audacious-style) instance number (0 == default: no new instance) */
                        {
                            instancename = String (str_copy (arg + c, 1));
                            options.newinstance = 1;
                        }
                    }
                    else
                    {
                        fprintf (stderr, _("Unknown option: -%c\n"), arg[c]);
                        return false;
                    }
                }
            }
        }
    }

    aud_set_headless_mode (options.headless);

    if (options.verbose >= 2)
        audlog::set_stderr_level (audlog::Debug);
    else if (options.verbose)
        audlog::set_stderr_level (audlog::Info);

    /* JWT:DON'T DO ANYTHING HERE THAT CALLS aud_set_<type> () AND FRIENDS - CAUSES "LEAK" ERRORS WHEN 
       REMOTE SESSION IS ALREADY RUNNING! */

#if defined(USE_QT) && defined(USE_GTK)
    if (options.qt && options.gtk)
        fprintf(stderr, "-G and -Q are mutually exclusive, ignoring\n");
    else if (options.qt)
        aud_set_mainloop_type (MainloopType::Qt);
    else
        aud_set_mainloop_type (MainloopType::GLib);
#endif

    return true;
}

static void print_help ()
{
    static const char pad[21] = "                    ";

    fprintf (stderr, "%s", _("Usage: fauxdacious [OPTION] ... [FILE] ...\n\n"));
    fprintf (stderr, "%s", _("FILE can be: local-file, url, -|--|-.ext (stdin).\n\n"));
    for (auto & arg_info : arg_map)
        fprintf (stderr, "  -%c, --%s%.*s%s\n", arg_info.short_arg,
         arg_info.long_arg, (int) (20 - strlen (arg_info.long_arg)), pad,
         _(arg_info.desc));

    fprintf (stderr, "\n");
}

#ifdef USE_DBUS
static void do_remote ()
{
    GDBusConnection * bus = nullptr;
    ObjFauxdacious * obj = nullptr;
    GError * error = nullptr;

    g_type_init ();

    aud_set_instancename (instancename);   /* TRY SETTING EARLY SINCE instancename GETS BLANKED?! */
    instancename = String ();  // JWT:PREVENT "LEAK" MESSAGES (FREE)!
    /* check whether the selected instance is running. */
    if (dbus_server_init (options.newinstance) != StartupType::Client)
        return;

    if (! (bus = g_bus_get_sync (G_BUS_TYPE_SESSION, nullptr, & error)) ||
            ! (obj = obj_fauxdacious_proxy_new_sync (bus, (GDBusProxyFlags) 0,
            dbus_server_name (), "/org/atheme/fauxdacious", nullptr, & error)))
    {
        AUDERR ("D-Bus error: %s\n", error->message);
        g_error_free (error);
        return;
    }

    AUDINFO ("Connected to remote session.\n");

    /* if no command line options, then present running instance */
    if (! (filenames.len () || options.play || options.pause ||
     options.play_pause || options.stop || options.rew || options.fwd ||
     options.show_jump_box || options.mainwin))
        options.mainwin = true;

    if (filenames.len ())
    {
        Index<const char *> list;

        for (auto & item : filenames)
            list.append (item.filename);

        list.append (nullptr);

        if (options.enqueue_to_temp)
            obj_fauxdacious_call_open_list_to_temp_sync (obj, list.begin (), nullptr, nullptr);
        else if (options.enqueue)
            obj_fauxdacious_call_add_list_sync (obj, list.begin (), nullptr, nullptr);
        else
            obj_fauxdacious_call_open_list_sync (obj, list.begin (), nullptr, nullptr);
    }

    if (options.play)
        obj_fauxdacious_call_play_sync (obj, nullptr, nullptr);
    if (options.pause)
        obj_fauxdacious_call_pause_sync (obj, nullptr, nullptr);
    if (options.play_pause)
        obj_fauxdacious_call_play_pause_sync (obj, nullptr, nullptr);
    if (options.stop)
        obj_fauxdacious_call_stop_sync (obj, nullptr, nullptr);
    if (options.rew)
        obj_fauxdacious_call_reverse_sync (obj, nullptr, nullptr);
    if (options.fwd)
        obj_fauxdacious_call_advance_sync (obj, nullptr, nullptr);
    if (options.show_jump_box)
        obj_fauxdacious_call_show_jtf_box_sync (obj, true, nullptr, nullptr);
    if (options.mainwin)
        obj_fauxdacious_call_show_main_win_sync (obj, true, nullptr, nullptr);

    const char * startup_id = getenv ("DESKTOP_STARTUP_ID");
    if (startup_id)
        obj_fauxdacious_call_startup_notify_sync (obj, startup_id, nullptr, nullptr);

    g_object_unref (obj);

    exit (EXIT_SUCCESS);
}
#endif

static void do_commands ()
{
    bool resume = aud_get_bool (nullptr, "resume_playback_on_startup");

    if (options.deleteallplaylists)  /* JWT */
    {
        const char *playlist_dir = aud_get_path (AudPath::PlaylistDir);
        if (strstr (playlist_dir, "playlists"))
        {   /* PREVENT POTENTIAL ACCIDENTAL DELETE AGONY IF playlist WAS SOMEHOW EMPTY!!! */
            AUDINFO ("-D2:DELETING ALL PLAYLISTS (DIR=%s)!", playlist_dir);
            int i;
            int playlist_count = aud_playlist_count ();
            for (i=playlist_count;i>=0;i--)
            {
                aud_playlist_delete (i);
            }
            /* JWT:ADDED TO REMOVE THE TEMPORARY TAG FILE CREATED BY URL-HELPER SCRIPT FOR YOUTUBE VIDEOS AND 
               THE YOUTUBEDL PLUGIN (TO KEEP IT FROM FILLING UP W/NOW USELESS ENTRIES): */
            if (aud_get_bool (nullptr, "user_tag_data"))
            {
                String cover_helper = aud_get_str ("audacious", "cover_helper");
                if (cover_helper[0])  // JWT:WE HAVE A PERL HELPER, SO WE'LL USE IT TO DELETE TEMP. COVER-ART FILES IT CREATED:
                {
                    AUDINFO ("----HELPER FOUND: WILL DO (%s)\n", (const char *) str_concat ({cover_helper, " DELETE COVERART ", 
                          aud_get_path (AudPath::UserDir)}));
#ifdef _WIN32
                    WinExec ((const char *) str_concat ({cover_helper, " DELETE COVERART ", 
                          aud_get_path (AudPath::UserDir)}), SW_HIDE);
#else
                    system ((const char *) str_concat ({cover_helper, " DELETE COVERART ", 
                          aud_get_path (AudPath::UserDir)}));
#endif
                }
                StringBuf tagdata_filename = filename_build ({aud_get_path (AudPath::UserDir), "tmp_tag_data"});
                if (! remove ((const char *) tagdata_filename))
                    AUDINFO ("..tmp_tag_data tag file found and removed.\n");
            }
        }
    }

    if (options.clearplaylist)   /* JWT */
    {
        int playlist_count = aud_playlist_count ();
        if (playlist_count > 0)
            aud_playlist_delete (playlist_count-1);
    }

    if (options.outstd)
    {
        if (options.headless)  // JWT:IF HEADLESS AND PIPING TO STDOUT, THEN QUIT WHEN DONE!
            options.quit_after_play = 1;
        if (out_ext && out_ext[0])
        {
            int outstd = aud_get_int ("filewriter", (const char *) str_concat ({"have_", (const char *) out_ext}));
            if (outstd > 0)
                aud_set_stdout_fmt (outstd);
            else
            {
                AUDERR ("w:%s is not a recognized format, using wav format.\n", (const char *) out_ext);
                aud_set_stdout_fmt (1);
            }
            out_ext = String ();  // JWT:PREVENT "LEAK" MESSAGES (FREE)!
        }
        else
            aud_set_stdout_fmt (1);
    }
    else if (options.force_recording)
    {
        resetRecordEnabled = aud_drct_get_record_enabled ();
        aud_drct_enable_record (true);
        if (aud_drct_get_record_enabled ())
            aud_set_bool (nullptr, "record", true);
        else
        {
            aud_set_bool (nullptr, "record", false);
            resetRecordEnabled = false;
            AUDERR ("e:Stream recording must be configured in Audio, -R ignored!\n");
        }
    }

    if (filenames.len ())
    {
        if (options.enqueue_to_temp)
        {
            aud_drct_pl_open_temp_list (std::move (filenames));
            resume = false;
        }
        else if (options.enqueue)
            aud_drct_pl_add_list (std::move (filenames), -1);
        else
        {
            aud_drct_pl_open_list (std::move (filenames));
            resume = false;
        }
    }

    if (resume)
        aud_resume ();

    if (options.play || options.play_pause)
    {
        if (! aud_drct_get_playing ())
            aud_drct_play ();
        else if (aud_drct_get_paused ())
            aud_drct_pause ();
    }

    if (options.pauseismute) /* JWT: ADDED 20100205 "-P" COMMAND-LINE OPTION TO ALLOW MUTING OF OUTPUT ON PAUSE (INPUT CONTINUES)! */
        aud_set_pausemute_mode (true);
    else
        aud_set_pausemute_mode (false);
}

static void do_commands_at_idle (void *)
{
    if (options.show_jump_box && ! options.headless)
        aud_ui_show_jump_to_song ();
    if (options.mainwin && ! options.headless)
        aud_ui_show (true);
    if (options.forcenoequalizer)  /* JWT:NEXT 5 TO ADD OPTIONAL STARTING GAIN ADJUSTMENT */
    {
        resetEqState = aud_get_bool (nullptr, "equalizer_active");
        aud_set_bool (nullptr, "equalizer_active", false);
    }
    if (jwt_norepeat)
    {
        resetRepeatToOn = aud_get_bool (nullptr, "repeat");
        aud_set_bool (nullptr, "repeat", false);
    }
    if (options.forcenogainchg)
        aud_set_fudge_gain (starting_gain);
}

static void main_cleanup ()
{
    if (initted)
    {
        /* Somebody was naughty and called exit() instead of aud_quit().
         * aud_cleanup() has not been called yet, so there's no point in running
         * leak checks.  Note that it's not safe to call aud_cleanup() from the
         * exit handler, since we don't know what context we're in (we could be
         * deep inside the call tree of some plugin, for example). */
        AUDWARN ("exit() called unexpectedly; skipping normal cleanup.\n");
        return;
    }

    filenames.clear ();
    aud_leak_check ();
}

static bool check_should_quit ()
{
    return options.quit_after_play && ! aud_drct_get_playing () &&
     ! aud_playlist_add_in_progress (-1);
}

static void maybe_quit ()
{
    if (check_should_quit ())
        aud_quit ();
}

int main (int argc, char * * argv)
{
    atexit (main_cleanup);

#ifdef _WIN32
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
#endif
#ifdef HAVE_SIGWAIT
    signals_init_one ();
#endif

    aud_init_i18n ();

    if (! parse_options (argc, argv))
    {
        print_help ();
        return EXIT_FAILURE;
    }

    if (options.help)
    {
        print_help ();
        return EXIT_SUCCESS;
    }

    if (options.version)
    {
        printf ("%s %s (%s)\n", _("Fauxdacious"), VERSION, BUILDSTAMP);
        return EXIT_SUCCESS;
    }

    if (options.outstd)
        aud_set_stdout_fmt (1);

#ifdef USE_DBUS
    do_remote (); /* may exit (calls dbus_server) */
#endif

    AUDINFO ("No remote session; starting up.\n");

#ifdef HAVE_SIGWAIT
    signals_init_two ();
#endif

    initted = true;

#ifdef USE_SDL2
    /* JWT:MUST INITIALIZE SDL2 BEFORE ANY GTK WINDOWS POPUP ELSE MAY GET SEGFAULT WHEN OPENING ONE! */
    bool sdl_initialized = false;  // TRUE IF SDL (VIDEO) IS SUCCESSFULLY INITIALIZED.
    SDL_SetMainReady ();
#endif

#if defined(USE_GTK)
#if defined(USE_QT)
    if (! options.qt)
#endif
    {
#ifdef USE_SDL2
        fauxd_set_sdl_window (nullptr);
        if (SDL_Init (SDL_INIT_VIDEO))  // GTK REQUIRES INIT HERE TO AVOID SEGFAULTS, QT PUKES ON EXIT IF SO!:
            AUDERR ("e:Failed to init SDL in main(): (no video playing): %s.\n", SDL_GetError ());
        else
        {
            sdl_initialized = true;
            Uint32 flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;
            if (aud_get_bool ("ffaudio", "allow_highdpi"))
                flags |= SDL_WINDOW_ALLOW_HIGHDPI;

            SDL_Window * sdl_window = SDL_CreateWindow ("Fauxdacious Video", SDL_WINDOWPOS_UNDEFINED,
                SDL_WINDOWPOS_UNDEFINED, 1, 1, flags);
            if (! sdl_window)
            {
                AUDERR ("Failed to create SDL window (no video playing): %s.\n", SDL_GetError ());
                return false;
            }
#if SDL_COMPILEDVERSION >= 2004
            SDL_SetHint (SDL_HINT_VIDEO_X11_NET_WM_PING, "0");
#endif
            fauxd_set_sdl_window (sdl_window);
        }
#endif
    }
#endif

    aud_init ();

    do_commands ();

    if (! check_should_quit ())
    {
        QueuedFunc at_idle_func;
        at_idle_func.queue (do_commands_at_idle, nullptr);

        hook_associate ("playback stop", (HookFunction) maybe_quit, nullptr);
        hook_associate ("playlist add complete", (HookFunction) maybe_quit, nullptr);
        hook_associate ("quit", (HookFunction) aud_quit, nullptr);

        aud_run ();

        hook_dissociate ("playback stop", (HookFunction) maybe_quit);
        hook_dissociate ("playlist add complete", (HookFunction) maybe_quit);
        hook_dissociate ("quit", (HookFunction) aud_quit);
    }

#ifdef USE_SDL2
    if (sdl_initialized)
        SDL_QuitSubSystem (SDL_INIT_VIDEO);  // SDL DOCS SAY SDL_Quit () SAFE, BUT SEGFAULTS HERE?!
#endif

    aud_set_bool (nullptr, "record", false);  // JWT:MAKE SURE RECORDING(DUBBING) IS OFF!

#ifdef USE_DBUS
    dbus_server_cleanup ();
#endif

    if (options.forcenoequalizer)
        aud_set_bool (nullptr, "equalizer_active", resetEqState);
    if (jwt_norepeat)
        aud_set_bool (nullptr, "repeat", resetRepeatToOn);
    if (options.outstd)
        aud_set_int (nullptr, "stdout_fmt", 0);
    if (options.force_recording)
        aud_drct_enable_record (resetRecordEnabled);

    aud_cleanup ();
    initted = false;

    return EXIT_SUCCESS;
}
