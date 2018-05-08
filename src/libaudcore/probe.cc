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
    if (file && file.fseek (0, VFS_SEEK_SET) == 0)
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
                        aud_playlist_entry_insert_batch (aud_playlist_get_active (), 0, std::move (incoming_items), true);
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

    //JWT:TAG FILENAME NEVER HAS "file://" IN FRONT OF IT IF SO, IT'S STRIPPED OFF!:
    if (! g_key_file_load_from_file (rcfile, (const char *)filename, G_KEY_FILE_NONE, nullptr))
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

    g_key_file_free (rcfile);

    if (strstr (precedence, "ONLY"))
    {
        g_free (precedence);
        return -1;
    }
    else if (strstr (precedence, "OVERRIDE"))
    {
        g_free (precedence);
        return 1;
    }
    g_free (precedence);
    return 2;
}

EXPORT bool aud_file_read_tag (const char * filename, PluginHandle * decoder,
 VFSFile & file, Tuple & tuple, Index<char> * image, String * error)
{
    auto ip = load_input_plugin (decoder, error);
    if (! ip)
        return false;

    if (! open_input_file (filename, "r", ip, file, error))
        return false;

    Tuple file_tuple, loclfile_tuple, new_tuple;
    Tuple * which_tuple;
    int fromtempfile = 0;
    int fromfile = 0;
    int fromlocalfile = 0;
    bool usrtag = aud_get_bool (nullptr, "user_tag_data");
    String local_tag_file = String ("");
    String filename_only = String ("");

    new_tuple.set_filename (filename);
    if (usrtag && ! strncmp (filename, "file://", 7))
    {
        StringBuf tag_fid = uri_to_filename (filename);
        StringBuf path = filename_get_parent ((const char *)tag_fid);
        filename_only = String (filename_get_base ((const char *)tag_fid));
        local_tag_file = String (filename_to_uri (str_concat ({(const char *)path, "/user_tag_data.tag"})));
    }
    /* JWT:blacklist stdin - read_tag does seekeys. :(  if (ip->read_tag (filename, file, new_tuple, image)) */
    /* NOTE:TRY EACH CASE, *STOPPING* WHEN ONE RETURNS TRUE!: 
       WHEN SUCCESSFUL FETCHING FROM TAG FILE, IT RETURNS TRUE ONLY IF THE "PRECEDENCE" == "ONLY" (-1),
       OTHERWISE, WE KEEP PROCESSING THE CONDITIONS UNTIL AT LAST WE FETCH (ANY UNSET) TAGS FROM THE FILE ITSELF. */
    if ((local_tag_file[0] && (fromlocalfile = aud_read_tag_from_tagfile ((const char *)filename_only, local_tag_file, loclfile_tuple)) < 0) 
            || (usrtag && (fromtempfile = aud_read_tag_from_tagfile (filename, "tmp_tag_data", file_tuple)) < 0)
            || (usrtag && (fromfile = aud_read_tag_from_tagfile (filename, "user_tag_data", file_tuple)) < 0)
            || (file && ip->read_tag (filename, file, new_tuple, image)))
    {
        which_tuple = &file_tuple;
        if (local_tag_file[0] && fromlocalfile)  //JWT:WE GOT TAGS FROM A LOCAL (DIRECTORY-BASED) user_tag_data.tag FILE.
        {
            /* USE THE "LOCAL" TAGFILE'S TAGS IFF "ONLY" OR "OVERRIDE" AND GLOBAL TAG FILE FAILED OR IS "DEFAULT". */
            if (fromlocalfile < 2 || (! fromfile || fromfile == 2))
            {
                fromfile = fromlocalfile;
                which_tuple = &loclfile_tuple;
            }
        }

        // cleanly replace existing tuple
        new_tuple.set_state (Tuple::Valid);
        if (fromtempfile && ! fromfile)  // WE FOUND TAGS IN tmp_tag_data, BUT NOT IN usr_tag_data.
            fromfile = fromtempfile;
        if (fromfile < 0)  // ONLY - GOT DATA FROM FILE, ONLY USE THAT (MAY NOT WORK, IE. LENGTH MISSING)!
        {
            which_tuple->set_state (Tuple::Valid);
            tuple = std::move (*which_tuple);
        }
        else if (fromfile > 0)  // DEFAULT OR OVERRIDE
        {
            int i_tfld;
            const char * tfld;
            tuple = std::move (new_tuple);
            if (fromfile == 1)  // OVERRIDE - USE FILE DATA, ONLY FILLING IN FIELDS NOT IN FILE W/TAGS DATA:
            {
                tfld = (const char *) which_tuple->get_str (Tuple::Title);
                if (tfld)
                    tuple.set_str (Tuple::Title, tfld);
                tfld = (const char *) which_tuple->get_str (Tuple::Artist);
                if (tfld)
                    tuple.set_str (Tuple::Artist, tfld);
                tfld = (const char *) which_tuple->get_str (Tuple::Album);
                if (tfld)
                    tuple.set_str (Tuple::Album, tfld);
                tfld = (const char *) which_tuple->get_str (Tuple::AlbumArtist);
                if (tfld)
                    tuple.set_str (Tuple::AlbumArtist, tfld);
                tfld = (const char *) which_tuple->get_str (Tuple::Comment);
                if (tfld)
                    tuple.set_str (Tuple::Comment, tfld);
                tfld = (const char *) which_tuple->get_str (Tuple::Genre);
                if (tfld)
                    tuple.set_str (Tuple::Genre, tfld);
                i_tfld = which_tuple->get_int (Tuple::Year);
                if (i_tfld && i_tfld > 0)
                    tuple.set_int (Tuple::Year, i_tfld);
                i_tfld = which_tuple->get_int (Tuple::Track);
                if (i_tfld && i_tfld > 0)
                    tuple.set_int (Tuple::Track, i_tfld);
            }
            else   //DEFAULT - USE NON-EMPTY TAG DATA
            {
                tfld = (const char *) new_tuple.get_str (Tuple::Title);
                if (! tfld)
                {
                    tfld = (const char *) which_tuple->get_str (Tuple::Title);
                    if (tfld)
                        tuple.set_str (Tuple::Title, tfld);
                }
                tfld = (const char *) new_tuple.get_str (Tuple::Artist);
                if (! tfld)
                {
                    tfld = (const char *) which_tuple->get_str (Tuple::Artist);
                    if (tfld)
                        tuple.set_str (Tuple::Artist, tfld);
                }
                tfld = (const char *) new_tuple.get_str (Tuple::Album);
                if (! tfld)
                {
                    tfld = (const char *) which_tuple->get_str (Tuple::Album);
                    if (tfld)
                        tuple.set_str (Tuple::Album, tfld);
                }
                tfld = (const char *) new_tuple.get_str (Tuple::AlbumArtist);
                if (! tfld)
                {
                    tfld = (const char *) which_tuple->get_str (Tuple::AlbumArtist);
                    if (tfld)
                        tuple.set_str (Tuple::AlbumArtist, tfld);
                }
                tfld = (const char *) new_tuple.get_str (Tuple::Comment);
                if (! tfld)
                {
                    tfld = (const char *) which_tuple->get_str (Tuple::Comment);
                    if (tfld)
                        tuple.set_str (Tuple::Comment, tfld);
                }
                tfld = (const char *) new_tuple.get_str (Tuple::Genre);
                if (! tfld)
                {
                    tfld = (const char *) which_tuple->get_str (Tuple::Genre);
                    if (tfld)
                        tuple.set_str (Tuple::Genre, tfld);
                }
                i_tfld = new_tuple.get_int (Tuple::Year);
                if (i_tfld <= 0)
                {
                    i_tfld = which_tuple->get_int (Tuple::Year);
                    if (i_tfld)
                        tuple.set_int (Tuple::Year, i_tfld);
                }
                i_tfld = new_tuple.get_int (Tuple::Track);
                if (i_tfld <= 0)
                {
                    i_tfld = which_tuple->get_int (Tuple::Track);
                    if (i_tfld)
                        tuple.set_int (Tuple::Track, i_tfld);
                }
            }
        }
        else  // NO TAG FILE DATA FOUND, ALL WE HAVE IS WHAT'S IN THE PLAYING FILE ITSELF.
            tuple = std::move (new_tuple);

        return true;
    }

    if (error)
        * error = String (_("Error reading metadata"));

    return false;
}

EXPORT bool aud_file_can_write_tuple (const char * filename, PluginHandle * decoder)
{
    return input_plugin_can_write_tuple (decoder);
}

/* JWT: NEW FUNCTION TO WRITE TAG METADATA TO USER-CREATED TEXT FILE: */
EXPORT int aud_write_tag_to_tagfile (const char * song_filename, const Tuple & tuple, const char * tagdata_filename)
{
    GKeyFile * rcfile = g_key_file_new ();
    String local_tag_fid = String ("");
    String song_key = String (song_filename);
    bool localtagfileexists = false;
    if (! strncmp (song_filename, "file://", 7))  //JWT:ONLY LOOK FOR A LOCAL TAG FILE IF WE'RE PLAYING A "FILE"!:
    {
        struct stat statbuf;
        StringBuf song_fid = uri_to_filename (song_filename);
        StringBuf path = filename_get_parent ((const char *)song_fid);
        local_tag_fid = String (filename_normalize (str_concat ({(const char *)path, "/user_tag_data.tag"}))); 
        if (! stat ((const char *)local_tag_fid, &statbuf))  // LOCAL TAG FILE DOESN'T EXIST:
        {
            localtagfileexists = true;
            song_key = String (filename_get_base ((const char *)song_fid));
        }
    }

    StringBuf filename = localtagfileexists ? str_copy ((const char *)local_tag_fid)
            : filename_build ({aud_get_path (AudPath::UserDir), tagdata_filename});

    //JWT: filename DOES NOT HAVE file:// PREFIX AND IS THE TAG DATA FILE (LOCAL OR GLOBAL):
    if (! g_key_file_load_from_file (rcfile, filename, 
            (GKeyFileFlags)(G_KEY_FILE_KEEP_COMMENTS), nullptr))
        AUDDBG ("w:aud_write_tag_to_tagfile: error opening key file (%s), assuming we're creating a new one.",
            (const char *) filename);

    const char * song_key_const = (const char *)song_key;
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
    tfld = (const char *) tuple.get_str (Tuple::Genre);
    if (tfld)
        g_key_file_set_string (rcfile, song_key_const, "Genre", tfld);
    i_tfld = tuple.get_int (Tuple::Year);
    if (i_tfld)
        g_key_file_set_double (rcfile, song_key_const, "Year", i_tfld);
    i_tfld = tuple.get_int (Tuple::Track);
    if (i_tfld)
        g_key_file_set_double (rcfile, song_key_const, "Track", i_tfld);

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

    if (!strcmp(filename, "-") || strstr(filename, "://-."))  /* JWT:NO SONG-INFO SAVING ON STDIN!!! */
        return false;

    auto ip = (InputPlugin *) aud_plugin_get_header (decoder);
    if (! ip)
        //return false;
        success = false;

    if (strstr(filename, "ytdl://"))
        success = false;

    if (success)
    {
        VFSFile file;

        if (! open_input_file (filename, "r+", ip, file))
            success = false;

        if (success)
        {
            if (! file)  /* JWT:ADDED TO PREVENT SEGFAULT - <input_plugins>.write_tuple () EXPECTS AN OPEN FILE!!! */
                file = VFSFile (filename, "r+");
            success = file ? ip->write_tuple (filename, file, tuple) : false;
        }

        if (success && file && file.fflush () != 0)
            success = false;
    }
    /* JWT:IF CAN'T SAVE TAGS TO FILE (IE. STREAM), TRY SAVING TO USER'S CONFIG: */
    if (! success && aud_get_bool (nullptr, "user_tag_data"))
        success = aud_write_tag_to_tagfile (filename, tuple, "user_tag_data");

    if (success)
        aud_playlist_rescan_file (filename);

    return success;
}

/* JWT: NEW FUNCTION TO REMOVE A TITLE FROM THE TAG FILE: */
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

    auto ip = (InputPlugin *) aud_plugin_get_header (decoder);
    if (! ip)
        return false;

    VFSFile file;
    if (! open_input_file (filename, "r", ip, file))
        return false;

    return ip->file_info_box (filename, file);
}
