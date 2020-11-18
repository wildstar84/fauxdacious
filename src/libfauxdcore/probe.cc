/*
 * probe.c
 * Copyright 2009-2013 John Lindgren
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

#include "internal.h"
#include "probe.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>  /* for GKeyFile */
extern "C" {
#include <sys/stat.h>
}

#include "audstrings.h"
#include "i18n.h"
#include "playlist.h"
#include "plugin.h"
#include "plugins-internal.h"
#include "runtime.h"

bool open_input_file (const char * filename, const char * mode,
 InputPlugin * ip, VFSFile & file, String * error)
{
    /* no need to open a handle for custom URI schemes */
    if (ip && ip->input_info.keys[InputKey::Scheme])
        return true;

    /* already open? */
    if (file && (file.fseek (0, VFS_SEEK_SET) == 0 || ! strncmp (filename, "stdin://", 8)))
        return true;

    file = VFSFile (filename, mode);
    if (! file && error)
        * error = String (file.error ());

    return (bool) file;
}

InputPlugin * load_input_plugin (PluginHandle * decoder, String * error)
{
    auto ip = (InputPlugin *) aud_plugin_get_header (decoder);
    if (! ip && error)
        * error = String (_("Error loading plugin"));

    return ip;
}

/* figure out some basic info without opening the file */
int probe_by_filename (const char * filename)
{
    int flags = 0;
    auto & list = aud_plugin_list (PluginType::Input);

    StringBuf scheme = uri_get_scheme (filename);
    StringBuf ext = uri_get_extension (filename);

    for (PluginHandle * plugin : list)
    {
        if (! aud_plugin_get_enabled (plugin))
            continue;

        if ((scheme && input_plugin_has_key (plugin, InputKey::Scheme, scheme)) ||
            (ext && input_plugin_has_key (plugin, InputKey::Ext, ext)))
        {
            flags |= PROBE_FLAG_HAS_DECODER;
            if (input_plugin_has_subtunes (plugin))
                flags |= PROBE_FLAG_MIGHT_HAVE_SUBTUNES;
        }
    }

    return flags;
}

EXPORT PluginHandle * aud_file_find_decoder (const char * filename, bool fast,
 VFSFile & file, String * error)
{
    AUDINFO ("%s %s.\n", fast ? "Fast-probing" : "Probing", filename);

    auto & list = aud_plugin_list (PluginType::Input);

    StringBuf scheme = uri_get_scheme (filename);
    StringBuf ext = uri_get_extension (filename);
    Index<PluginHandle *> ext_matches;

    for (PluginHandle * plugin : list)
    {
        if (! aud_plugin_get_enabled (plugin))
            continue;

        if (scheme && input_plugin_has_key (plugin, InputKey::Scheme, scheme))
        {
            AUDINFO ("Matched %s by URI scheme.\n", aud_plugin_get_name (plugin));
            return plugin;
        }

        if (ext && input_plugin_has_key (plugin, InputKey::Ext, ext))
            ext_matches.append (plugin);
    }

    if (ext_matches.len () == 1 || (ext_matches.len () > 1 && ! strncmp (filename, "stdin://", 8)))
    {
        AUDINFO ("Matched %s by extension.\n", aud_plugin_get_name (ext_matches[0]));
        if (! strncmp (filename, "stdin://", 8) && ! open_input_file (filename, "r", nullptr, file, error))
            AUDINFO ("w:STDIN Open failed, see if plugin will still handle stdin://-.ext the old Fauxdacious way?...\n");

        return ext_matches[0];
    }

    AUDINFO ("Matched %d plugins by extension.\n", ext_matches.len ());

    /* JWT:SPECIAL CHECK FOR YOUTUBE STREAMS: CHECK URL FOR EMBEDDED MIME-TYPE (SINCE BY-CONTENT SOMETIMES FAILS): */
    const char * urlmime = strstr(filename, "&mime=video%2F");
    if (urlmime)
    {
        urlmime += 14;
        const char * end = urlmime;
        while (end[0] && end[0] != '&')
        {
            ++end;
        }
        ext = str_printf ("%.*s", (int)(end - urlmime), urlmime);
        for (PluginHandle * plugin : list)
        {
            if (! aud_plugin_get_enabled (plugin))
                continue;

            if (ext && input_plugin_has_key (plugin, InputKey::Ext, ext))
                ext_matches.append (plugin);
        }
        if (ext_matches.len () == 1)
        {
            AUDINFO ("Matched %s by in-url mimetype.\n", aud_plugin_get_name (ext_matches[0]));
            return ext_matches[0];
        }
    }

    if (fast && ! ext_matches.len ())
        return nullptr;

    AUDDBG ("Opening %s.\n", filename);

    if (! open_input_file (filename, "r", nullptr, file, error))
    {
        AUDINFO ("Open failed.\n");
        return nullptr;
    }

    String mime = file.get_metadata ("content-type");

    if (mime)
    {
        for (PluginHandle * plugin : (ext_matches.len () ? ext_matches : list))
        {
            if (! aud_plugin_get_enabled (plugin))
                continue;

            if (input_plugin_has_key (plugin, InputKey::MIME, mime))
            {
                AUDINFO ("Matched %s by MIME type %s.\n",
                 aud_plugin_get_name (plugin), (const char *) mime);
                return plugin;
            }
        }
    }

    file.set_limit_to_buffer (true);

    for (PluginHandle * plugin : (ext_matches.len () ? ext_matches : list))
    {
        if (! aud_plugin_get_enabled (plugin))
            continue;

        AUDINFO ("Trying %s.\n", aud_plugin_get_name (plugin));

        auto ip = (InputPlugin *) aud_plugin_get_header (plugin);
        if (! ip)
            continue;

        if (ip->is_our_file (filename, file))
        {
            AUDINFO ("Matched %s by content.\n", aud_plugin_get_name (plugin));
            file.set_limit_to_buffer (false);
            return plugin;
        }

        if (file.fseek (0, VFS_SEEK_SET) != 0)
        {
            if (error)
                * error = String (_("Seek error"));

            AUDINFO ("Seek failed.\n");
            return nullptr;
        }
    }

    /* JWT:SECTION ADDED TO TREAT stdin ("-") AS A TEXT PLAYLIST (LIST OF ENTRIES TO PLAY) IF NO 
       OTHER MEDIA INPUT PLUGIN RECOGNIZES IT, THIS WAY WE CAN PIPE A LIST OF ENTRIES INTO 
       FAUXDACIOUS VIA stdin NOW *WITHOUT* HAVING TO APPEND THE ".m3u" EXTENSION, IE. "-.m3u".
       IF IT'S NOT A VALID TEXT LIST, NOTHING WILL BE ADDED AND IT WILL FAIL SAME AS BEFORE
       ("File format not recognized" ERROR POPUP).
    */
    if (! strcmp (filename, N_("stdin://")))
        for (PluginHandle * plugin : aud_plugin_list (PluginType::Playlist))
        {
            if (aud_plugin_get_enabled (plugin) && playlist_plugin_has_ext (plugin, "txt"))
            {
                String title;
                auto pp = (PlaylistPlugin *) aud_plugin_get_header (plugin);
                if (! pp)
                    break;

                Index<PlaylistAddItem> incoming_items;

                if (pp->load (filename, file, title, incoming_items))
                {
                    if (incoming_items.len () > 0)
                    {
                        aud_playlist_entry_insert_filtered (aud_playlist_get_active (), -1, std::move (incoming_items), 
                                nullptr, nullptr, true);
                        error = nullptr;
                    }
                }
            }
        }
    if (error)
        * error = String (_("File format not recognized"));

    AUDINFO ("No plugins matched.\n");
    return nullptr;
}

/* JWT: NEW FUNCTION TO FETCH TAG METADATA FROM USER-CREATED TEXT FILE: */
EXPORT int aud_read_tag_from_tagfile (const char * song_filename, const char * tagdata_filename, Tuple & tuple)
{
    GKeyFile * rcfile = g_key_file_new ();
    StringBuf filename = (! strncmp (tagdata_filename, "file://", 7))
            ? uri_to_filename (tagdata_filename)
            : filename_build ({aud_get_path (AudPath::UserDir), tagdata_filename});

    //JWT: filename NOW DOES NOT HAVE file:// PREFIX AND IS THE TAG DATA FILE (LOCAL OR GLOBAL):
    if (! g_key_file_load_from_file (rcfile, (const char *) filename, G_KEY_FILE_NONE, nullptr))
    {
        g_key_file_free (rcfile);

        return 0;
    }
    char * precedence = g_key_file_get_string (rcfile, song_filename, "Precedence", nullptr);
    if (! precedence)
    {
    	   g_free (precedence);
        g_key_file_free (rcfile);

        return 0;
    }

    const char * instr;
    instr = g_key_file_get_string (rcfile, song_filename, "Title", nullptr);
    if (instr)
        tuple.set_str (Tuple::Title, String (instr));
    else
        tuple.unset (Tuple::Title);
    instr = g_key_file_get_string (rcfile, song_filename, "Artist", nullptr);
    if (instr)
        tuple.set_str (Tuple::Artist, instr);
    else
        tuple.unset (Tuple::Artist);
    instr = g_key_file_get_string (rcfile, song_filename, "Album", nullptr);
    if (instr)
        tuple.set_str (Tuple::Album, instr);
    else
        tuple.unset (Tuple::Album);
    instr = g_key_file_get_string (rcfile, song_filename, "AlbumArtist", nullptr);
    if (instr)
        tuple.set_str (Tuple::AlbumArtist, instr);
    else
        tuple.unset (Tuple::AlbumArtist);
    instr = g_key_file_get_string (rcfile, song_filename, "Comment", nullptr);
    if (instr)
        tuple.set_str (Tuple::Comment, instr);
    else
        tuple.unset (Tuple::Comment);
    instr = g_key_file_get_string (rcfile, song_filename, "Composer", nullptr);
    if (instr)
        tuple.set_str (Tuple::Composer, instr);
    else
        tuple.unset (Tuple::Composer);
    instr = g_key_file_get_string (rcfile, song_filename, "Performer", nullptr);
    if (instr)
        tuple.set_str (Tuple::Performer, instr);
    else
        tuple.unset (Tuple::Performer);
    instr = g_key_file_get_string (rcfile, song_filename, "Genre", nullptr);
    if (instr)
        tuple.set_str (Tuple::Genre, instr);
    else
        tuple.unset (Tuple::Genre);
    instr = g_key_file_get_string (rcfile, song_filename, "Year", nullptr);
    if (instr && instr[0])
        tuple.set_int (Tuple::Year, atoi (instr));
    else
        tuple.unset (Tuple::Year);
    instr = g_key_file_get_string (rcfile, song_filename, "Track", nullptr);
    if (instr && instr[0])
        tuple.set_int (Tuple::Track, atoi (instr));
    else
        tuple.unset (Tuple::Track);
    instr = g_key_file_get_string (rcfile, song_filename, "Lyrics", nullptr);
    if (instr)
        tuple.set_str (Tuple::Lyrics, instr);
    else
        tuple.unset (Tuple::Lyrics);

    g_key_file_free (rcfile);

    if (strstr (precedence, "ONLY"))
    {
        g_free (precedence);
        return 3;
    }
    else if (strstr (precedence, "OVERRIDE"))
    {
        g_free (precedence);
        return 2;
    }
    else if (strstr (precedence, "NONE"))  /* SKIP, AS IF ERROR */
    {
        g_free (precedence);
        return 0;
    }
    g_free (precedence);  /* "DEFAULT" (or ANYTHING ELSE) */
    return 1;
}

EXPORT bool aud_file_read_tag (const char * filename, PluginHandle * decoder,
 VFSFile & file, Tuple & tuple, Index<char> * image, String * error)
{
    auto ip = load_input_plugin (decoder, error);
    if (! ip)
        return false;

    if (! open_input_file (filename, "r", ip, file, error))
        return false;

    Tuple tuples[4];   // THE TUPLES FROM EACH OF THE POTENTIAL TAG FILES.
    Tuple song_tuple;  // THE TUPLE FROM THE ENTRY'S EMBEDDED ID3, ETC. TAGS.
    Tuple * override_tuple = nullptr; // POINTER TO TUPLE IF HIGHEST PRIORITY TAG FILE W/"OVERRIDE" SET, IF ANY.
    int highest_mode = 0;    // HIGHEST PRECEDENCE FOUND IN TAG FILES (0=NONE FOUND, 1=DEFAULT, 2=OVERRIDE, 3=ONLY.
    int from[4] = {0,0,0,0}; // PRECEDENCE FOUND IN EACH TAG FILE.
    bool usrtag = aud_get_bool (nullptr, "user_tag_data"); // ARE WE ALLOWED TO LOOK FOR TAG FILES?
    String individual_tag_file = String ("");  // ./song_filename.tag (local files only)
    String dir_tag_file = String ("");         // ./user_tag_data.tag (directory-wide tag file - local files only)
    String filename_only = String ("");        // "song_filename" (no path or extension - local files only)

    song_tuple.set_filename (filename);
    if (usrtag && ! strncmp (filename, "file://", 7))
    {
        StringBuf tag_fid = uri_to_filename (filename);
        StringBuf path = filename_get_parent (tag_fid);
        filename_only = String (filename_get_base (tag_fid));
        dir_tag_file = String (filename_to_uri (str_concat ({path, "/user_tag_data.tag"})));
        individual_tag_file = String (filename_to_uri (str_concat ({path, "/", filename_only, ".tag"})));
    }
    /* JWT:blacklist stdin - read_tag does seekeys. :(  if (ip->read_tag (filename, file, song_tuple, image)) */
    /* NOTE:TRY EACH CASE, *STOPPING* WHEN ONE RETURNS >2 ("ONLY")!:
       (IF NONE ARE "ONLY", WE TRY ALL CASES, THEN FIND THE FIRST TAG FILE (IF ANY) IN LOOP JUST BELOW - 
       THIS IS SO WE ALSO GET THE ENTRY'S EMBEDDED TAG DATA AS THE STARTING DEFAULT (UNNEEDED IF "ONLY" FOUND)).
       WHEN SUCCESSFUL FETCHING FROM TAG FILE, IT RETURNS TRUE ONLY IF THE "PRECEDENCE" == "ONLY" (3),
       OTHERWISE, WE KEEP PROCESSING THE CONDITIONS UNTIL AT LAST WE FETCH (ANY UNSET) TAGS FROM THE FILE ITSELF. */
    bool fileorNOTstdin = file || strncmp (filename, "stdin://", 8);  // JWT:IF stdin, MUST HAVE OPEN FILEHANDLE!

    if ((usrtag && (from[0] = aud_read_tag_from_tagfile (filename, "tmp_tag_data", tuples[0])) > 2)
            || (individual_tag_file[0] && (from[1] = aud_read_tag_from_tagfile (filename_only, individual_tag_file, tuples[1])) > 2)
            || (dir_tag_file[0] && (from[2] = aud_read_tag_from_tagfile (filename_only, dir_tag_file, tuples[2])) > 2)
            || (usrtag && (from[3] = aud_read_tag_from_tagfile (filename, "user_tag_data", tuples[3])) > 2)
            || (fileorNOTstdin && ip->read_tag (filename, file, song_tuple, image)))
    {
        for (int i=0;i<4;i++)
        {
            if (from[i] >= 1)  /* HIGH TO LOW PRIORITY, STOP AS SOON AS WE FIND ENTRY IN A MATCHING TAG FILE! */
            {
                override_tuple = & tuples[i];
                highest_mode = from[i];
                break;
            }
        }

        // cleanly replace existing tuple from file's ID3
        song_tuple.set_state (Tuple::Valid);

        if (highest_mode > 2 && override_tuple)  // 3 - (ONLY) - GOT ALL DATA FROM FILE, ONLY USE THAT (RETAIN START TIME & LENGTH, IF SET BY read_tag())!
        {
            int startTime = tuple.is_set (Tuple::StartTime) ? tuple.get_int (Tuple::StartTime) : -1;
            int tupleLength = tuple.is_set (Tuple::Length) ? tuple.get_int (Tuple::Length) : -1;
            override_tuple->set_filename (filename);
            override_tuple->set_state (Tuple::Valid);
            tuple = std::move (* override_tuple);
            if (startTime > 0)  // JWT:DON'T CLOBBER START-TIME OR LENGTH IF ALREADY SET!:
            {
                tuple.set_int (Tuple::StartTime, startTime);
                if (tupleLength > 0)
                    tuple.set_int (Tuple::Length, tupleLength);
            }
        }
        else if (highest_mode > 0 && override_tuple)  // (1 OR 2) TAG FILE(S) FOUND (ALL DEFAULT OR OVERRIDE):
        {
            int i_tfld;
            const char * tfld;
            tuple = std::move (song_tuple);  // NOTE: THIS *MOVES*, NOT COPIES song_tuple!!! START OUT WITH ALL THE read_tag() DATA.
            if (highest_mode == 2)  // 2 - (OVERRIDE) - REPLACE ONLY TAG FIELDS THAT ARE ALSO FOUND IN THE PRIMARY TAG FILE:
            {
                tfld = (const char *) override_tuple->get_str (Tuple::Title);
                if (tfld)
                    tuple.set_str (Tuple::Title, tfld);  // OVERRIDE TITLE WITH TITLE FROM THE PRIMARY TAG FILE...
                tfld = (const char *) override_tuple->get_str (Tuple::Artist);
                if (tfld)
                    tuple.set_str (Tuple::Artist, tfld);
                tfld = (const char *) override_tuple->get_str (Tuple::Album);
                if (tfld)
                    tuple.set_str (Tuple::Album, tfld);
                tfld = (const char *) override_tuple->get_str (Tuple::AlbumArtist);
                if (tfld)
                    tuple.set_str (Tuple::AlbumArtist, tfld);
                tfld = (const char *) override_tuple->get_str (Tuple::Comment);
                if (tfld)
                    tuple.set_str (Tuple::Comment, tfld);
                tfld = (const char *) override_tuple->get_str (Tuple::Composer);
                if (tfld)
                    tuple.set_str (Tuple::Composer, tfld);
                tfld = (const char *) override_tuple->get_str (Tuple::Performer);
                if (tfld)
                    tuple.set_str (Tuple::Performer, tfld);
                tfld = (const char *) override_tuple->get_str (Tuple::Genre);
                if (tfld)
                    tuple.set_str (Tuple::Genre, tfld);
                i_tfld = override_tuple->get_int (Tuple::Year);
                if (i_tfld && i_tfld >= 0)
                    tuple.set_int (Tuple::Year, i_tfld);
                i_tfld = override_tuple->get_int (Tuple::Track);
                if (i_tfld && i_tfld >= 0)
                    tuple.set_int (Tuple::Track, i_tfld);
                i_tfld = override_tuple->is_set (Tuple::Length) ? override_tuple->get_int (Tuple::Length) : -1;
                if (i_tfld && i_tfld >= 0)
                    tuple.set_int (Tuple::Length, i_tfld);
                tfld = (const char *) override_tuple->get_str (Tuple::Lyrics);
                if (tfld)
                    tuple.set_str (Tuple::Lyrics, tfld);
            }
            else  // 1 - (DEFAULT) - REPLACE ONLY EMPTY(MISSING) TAG FIELDS WITH THE HIGHEST PRIORITY DEFAULT FOUND:
            {
                tfld = (const char *) tuple.get_str (Tuple::Title);
                if (! tfld)
                {
                    tfld = (const char *) override_tuple->get_str (Tuple::Title);
                    if (tfld)
                        tuple.set_str (Tuple::Title, tfld);
                }
                tfld = (const char *) tuple.get_str (Tuple::Artist);
                if (! tfld)
                {
                    tfld = (const char *) override_tuple->get_str (Tuple::Artist);
                    if (tfld)
                        tuple.set_str (Tuple::Artist, tfld);
                }
                tfld = (const char *) tuple.get_str (Tuple::Album);
                if (! tfld)
                {
                    tfld = (const char *) override_tuple->get_str (Tuple::Album);
                    if (tfld)
                        tuple.set_str (Tuple::Album, tfld);
                }
                tfld = (const char *) tuple.get_str (Tuple::AlbumArtist);
                if (! tfld)
                {
                    tfld = (const char *) override_tuple->get_str (Tuple::AlbumArtist);
                    if (tfld)
                        tuple.set_str (Tuple::AlbumArtist, tfld);
                }
                tfld = (const char *) tuple.get_str (Tuple::Comment);
                if (! tfld)
                {
                    tfld = (const char *) override_tuple->get_str (Tuple::Comment);
                    if (tfld)
                        tuple.set_str (Tuple::Comment, tfld);
                }
                tfld = (const char *) tuple.get_str (Tuple::Composer);
                if (! tfld)
                {
                    tfld = (const char *) override_tuple->get_str (Tuple::Composer);
                    if (tfld)
                        tuple.set_str (Tuple::Composer, tfld);
                }
                tfld = (const char *) tuple.get_str (Tuple::Performer);
                if (! tfld)
                {
                    tfld = (const char *) override_tuple->get_str (Tuple::Performer);
                    if (tfld)
                        tuple.set_str (Tuple::Performer, tfld);
                }
                tfld = (const char *) tuple.get_str (Tuple::Genre);
                if (! tfld)
                {
                    tfld = (const char *) override_tuple->get_str (Tuple::Genre);
                    if (tfld)
                        tuple.set_str (Tuple::Genre, tfld);
                }
                i_tfld = tuple.get_int (Tuple::Year);
                if (i_tfld < 0)
                {
                    i_tfld = override_tuple->get_int (Tuple::Year);
                    if (i_tfld >= 0)
                        tuple.set_int (Tuple::Year, i_tfld);
                }
                i_tfld = tuple.get_int (Tuple::Track);
                if (i_tfld < 0)
                {
                    i_tfld = override_tuple->get_int (Tuple::Track);
                    if (i_tfld >= 0)
                        tuple.set_int (Tuple::Track, i_tfld);
                }
                i_tfld = tuple.is_set (Tuple::Length) ? tuple.get_int (Tuple::Length) : -1;
                if (i_tfld < 0)
                {
                    i_tfld = override_tuple->is_set (Tuple::Length) ? override_tuple->get_int (Tuple::Length) : -1;
                    if (i_tfld >= 0)
                        tuple.set_int (Tuple::Length, i_tfld);
                }
                tfld = (const char *) tuple.get_str (Tuple::Lyrics);
                if (! tfld)
                {
                    tfld = (const char *) override_tuple->get_str (Tuple::Lyrics);
                    if (tfld)
                        tuple.set_str (Tuple::Lyrics, tfld);
                }
            }
        }
        else  // NO TAG FILE DATA FOUND, ALL WE HAVE IS WHAT'S EMBEDDED IN THE PLAYING FILE ITSELF:
        {
            int startTime = tuple.is_set (Tuple::StartTime) ? tuple.get_int (Tuple::StartTime) : -1;
            int tupleLength = tuple.is_set (Tuple::Length) ? tuple.get_int (Tuple::Length) : -1;
            tuple = std::move (song_tuple);
            if (startTime > 0)  // JWT:DON'T CLOBBER START-TIME OR LENGTH IF ALREADY SET!:
            {
                tuple.set_int (Tuple::StartTime, startTime);
                if (tupleLength > 0)
                    tuple.set_int (Tuple::Length, tupleLength);
            }
        }

        filename_only = String ("");
        dir_tag_file = String ("");
        individual_tag_file = String ("");
        return true;
    }

    if (error)
        * error = String (_("Error reading metadata"));

    filename_only = String ("");
    dir_tag_file = String ("");
    individual_tag_file = String ("");
    return false;
}

EXPORT bool aud_file_can_write_tuple (const char * filename, PluginHandle * decoder)
{
    return input_plugin_can_write_tuple (decoder);
}

/* JWT: NEW FUNCTION TO WRITE TAG METADATA TO USER-CREATED TAG FILE: */
EXPORT int aud_write_tag_to_tagfile (const char * song_filename, const Tuple & tuple, const char * tagdata_filename)
{
    GKeyFile * rcfile = g_key_file_new ();
    String local_tag_fid = String ("");
    String song_key = String (song_filename);
    bool willuseLocalTagfile = false;

    if (! strncmp (tagdata_filename, "file://", 7) && ! strncmp (song_filename, "file://", 7))
    {
        /* TAGFILE IS A SPECIFIED LOCAL FILE (NOT "{user|tmp}_tag_data"): */
        StringBuf tag_fid = uri_to_filename (tagdata_filename);
        StringBuf song_fid = uri_to_filename (song_filename);
        local_tag_fid = String (filename_normalize (uri_to_filename (tagdata_filename)));
        song_key = String (filename_get_base (song_fid));
        willuseLocalTagfile = true;
    }

    StringBuf filename = willuseLocalTagfile ? str_copy (local_tag_fid)
            : filename_build ({aud_get_path (AudPath::UserDir), tagdata_filename});

    //JWT: filename NOW DOES NOT HAVE file:// PREFIX AND IS THE TAG DATA FILE (LOCAL OR GLOBAL):
    if (! g_key_file_load_from_file (rcfile, filename, 
            (GKeyFileFlags)(G_KEY_FILE_KEEP_COMMENTS), nullptr))
        AUDDBG ("w:aud_write_tag_to_tagfile: error opening key file (%s), assuming we're creating a new one.\n",
            (const char *) filename);

    const char * song_key_const = (const char *) song_key;
    char * precedence = g_key_file_get_string (rcfile, song_key_const, "Precedence", nullptr);
    if (! precedence)
        g_key_file_set_string (rcfile, song_key_const, "Precedence", "DEFAULT");

    g_free (precedence);

    const char * tfld;
    int i_tfld;

    tfld = (const char *) tuple.get_str (Tuple::Title);
    if (tfld)
        g_key_file_set_string (rcfile, song_key_const, "Title", tfld);
    tfld = (const char *) tuple.get_str (Tuple::Artist);
    if (tfld)
        g_key_file_set_string (rcfile, song_key_const, "Artist", tfld);
    tfld = (const char *) tuple.get_str (Tuple::Album);
    if (tfld)
        g_key_file_set_string (rcfile, song_key_const, "Album", tfld);
    tfld = (const char *) tuple.get_str (Tuple::AlbumArtist);
    if (tfld)
        g_key_file_set_string (rcfile, song_key_const, "AlbumArtist", tfld);
    tfld = (const char *) tuple.get_str (Tuple::Comment);
    if (tfld)
        g_key_file_set_string (rcfile, song_key_const, "Comment", tfld);
    tfld = (const char *) tuple.get_str (Tuple::Composer);
    if (tfld)
        g_key_file_set_string (rcfile, song_key_const, "Composer", tfld);
    tfld = (const char *) tuple.get_str (Tuple::Performer);
    if (tfld)
        g_key_file_set_string (rcfile, song_key_const, "Performer", tfld);
    tfld = (const char *) tuple.get_str (Tuple::Genre);
    if (tfld)
        g_key_file_set_string (rcfile, song_key_const, "Genre", tfld);
    i_tfld = tuple.get_int (Tuple::Year);
    if (i_tfld)
        g_key_file_set_double (rcfile, song_key_const, "Year", i_tfld);
    i_tfld = tuple.get_int (Tuple::Track);
    if (i_tfld)
        g_key_file_set_double (rcfile, song_key_const, "Track", i_tfld);
    tfld = (const char *) tuple.get_str (Tuple::Lyrics);
    if (tfld)
        g_key_file_set_string (rcfile, song_key_const, "Lyrics", tfld);

    size_t len;
    char * data = g_key_file_to_data (rcfile, & len, nullptr);

    bool success = g_file_set_contents (filename, data, len, nullptr);

    g_key_file_free (rcfile);
    g_free (data);

    return success;
}

EXPORT bool aud_file_write_tuple (const char * filename,
 PluginHandle * decoder, const Tuple & tuple)
{
    bool success = true;

    if (!strcmp (filename, "-") || strstr (filename, "://-."))  /* JWT:NO SONG-INFO SAVING ON STDIN!!! */
        return false;

    auto ip = (InputPlugin *) aud_plugin_get_header (decoder);
    if (! ip)
        success = false;

    if (success)
    {
        String cue_suffix = tuple.get_str (Tuple::Suffix);
        String actual_filename = String (filename);
        if (cue_suffix && cue_suffix[0] && strstr_nocase ((const char *) cue_suffix, "cue"))
        {
            /* JWT:MUST USE ACTUAL AUDIO FILE FOR CUESHEETS! */
            String audio_file = tuple.get_str (Tuple::AudioFile);
            if (! audio_file || ! audio_file[0])
                return false;  // PUNT!

            actual_filename = audio_file;
        }
        if (! strncmp (actual_filename, "cdda://", 7) || ! strncmp (actual_filename, "dvd://", 6))  // FILE IS A DISK:
        {
            String diskID = aud_get_str (nullptr, "playingdiskid");
            String tag_file = strstr ((const char *) diskID, "tmp_tag_data")
                    ? diskID : String (str_concat ({diskID, ".tag"}));

            success = aud_write_tag_to_tagfile (actual_filename, tuple, tag_file);
            if (success)
            {
                int track;
                if (! (strncmp (filename, "cdda://?", 8) || sscanf (filename + 8, "%d", &track) != 1))
                {
                    aud_set_int (nullptr, "_disktagrefresh", track);
                    aud_playlist_rescan_file (actual_filename);
                }
                else if (! (strncmp (filename, "dvd://?", 7) || sscanf (filename + 7, "%d", &track) != 1))
                {
                    aud_set_int (nullptr, "_disktagrefresh", track+1); // OFF-BY-ONE B/C THERE'S A TRACK ZERO & ZERO==NO REFRESH!
                    aud_playlist_rescan_file (actual_filename);
                }
            }

            return success;
        }
        else if (! strncmp (actual_filename, "file://", 7) && ! aud_get_bool (nullptr, "record"))  // FILE IS A FILE:
        {
            VFSFile file;

            if (! open_input_file (actual_filename, "r+", ip, file))
                success = false;

            if (success)
            {
                if (! file)  /* JWT:ADDED TO PREVENT SEGFAULT - <input_plugins>.write_tuple () EXPECTS AN OPEN FILE!!! */
                    file = VFSFile (actual_filename, "r+");
                success = file ? ip->write_tuple (actual_filename, file, tuple) : false;
            }

            if (success && file && file.fflush () != 0)
                success = false;

            if (success && aud_get_bool (nullptr, "user_tag_data") && ! aud_get_bool (nullptr, "_user_tag_skipthistime"))
            {
                /* JWT:IF COMMENT IS AN ART IMAGE FILE, FORCE A WRITE TO TAG FILE ALSO (OTHERWISE, ART WON'T
                    BE PICKED UP AS TUPLE.COMMENT CAN ONLY BE CHECKED IN TAG FILES, NOT THE MP3 ITSELF)!
                    NOTE:  WE NOW SKIP THIS IF WE WERE ABLE TO WRITE THE IMAGE INTO AN ID3V24 TAG!
                */
                String TupleComment = tuple.get_str (Tuple::Comment);
                if (TupleComment && TupleComment[0] && ! strncmp ((const char *) TupleComment, "file://", 7))
                    success = false;
            }
            aud_set_bool (nullptr, "_user_tag_skipthistime", false);
        }
        else
            success = false;
    }
    /* JWT:IF CAN'T SAVE TAGS TO FILE (IE. STREAM, RECORDING), TRY SAVING TO USER'S CONFIG: */
    if (! success && aud_get_bool (nullptr, "user_tag_data"))
    {
        if (! strncmp (filename, "file://", 7))
        {
            int user_tag_data_options = aud_get_int (nullptr, "user_tag_data_options");
            if (user_tag_data_options == 2)  //use Individual tag file (filename.tag):
            {
                StringBuf tag_fid = uri_to_filename (filename);
                StringBuf path = filename_get_parent (tag_fid);
                success = aud_write_tag_to_tagfile (filename, tuple,
                        (const char *) filename_to_uri (str_concat ({path, "/", filename_get_base (tag_fid),
                        ".tag"})));
            }
            else if (user_tag_data_options == 1)  //use Directory-specific tag file (./user_tag_data.tag):
            {
                StringBuf tag_fid = uri_to_filename (filename);
                StringBuf path = filename_get_parent (tag_fid);
                success = aud_write_tag_to_tagfile (filename, tuple,
                        (const char *) filename_to_uri (str_concat ({path, "/user_tag_data.tag"})));
            }
        }
        if (! success)  //use Global tag file (ie. for streams/URLs):
            success = aud_write_tag_to_tagfile (filename, tuple, "user_tag_data");
    }

    if (success)
        aud_playlist_rescan_file (filename);

    return success;
}

/* JWT: NEW FUNCTION TO REMOVE A TITLE FROM THE GLOBAL (NOT LOCAL) TAG FILES: */
EXPORT bool aud_delete_tag_from_tagfile (const char * song_filename, const char * tagdata_filename)
{
    GKeyFile * rcfile = g_key_file_new ();
    StringBuf filename = filename_build ({aud_get_path (AudPath::UserDir), tagdata_filename});

    if (! g_key_file_load_from_file (rcfile, filename, 
            (GKeyFileFlags)(G_KEY_FILE_KEEP_COMMENTS), nullptr))
    {
        g_key_file_free (rcfile);
        return false;
    }

    bool success = g_key_file_remove_group (rcfile, song_filename, nullptr);

    if (success)
    {
        size_t len;
        char * data = g_key_file_to_data (rcfile, & len, nullptr);
        success = g_file_set_contents (filename, data, len, nullptr);
        g_key_file_free (rcfile);
        g_free (data);
    }
    else
        g_key_file_free (rcfile);
    return true;
}

EXPORT bool aud_custom_infowin (const char * filename, PluginHandle * decoder)
{
    // blacklist stdin
    if (! strncmp (filename, "stdin://", 8))
        return false;

    // In hindsight, a flag should have been added indicating whether a
    // plugin provides a custom info window or not.  Currently, only two
    // plugins do so.  Since custom info windows are deprecated anyway,
    // check for those two plugins explicitly and in all other cases,
    // don't open the input file to prevent freezing the UI.
    const char * base = aud_plugin_get_basename (decoder);
    if (strcmp (base, "amidi-plug") && strcmp (base, "vtx"))
        return false;

    auto ip = (InputPlugin *) aud_plugin_get_header (decoder);
    if (! ip)
        return false;

    VFSFile file;
    if (! open_input_file (filename, "r", ip, file))
        return false;

    return ip->file_info_box (filename, file);
}
