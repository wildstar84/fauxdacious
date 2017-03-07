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

    if (ext_matches.len () == 1)
    {
        AUDINFO ("Matched %s by extension.\n", aud_plugin_get_name (ext_matches[0]));
        return ext_matches[0];
    }

    AUDDBG ("Matched %d plugins by extension.\n", ext_matches.len ());

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

    if (error)
        * error = String (_("File format not recognized"));

    AUDINFO ("No plugins matched.\n");
    return nullptr;
}

/* JWT: NEW FUNCTION TO FETCH TAG METADATA FROM USER-CREATED TEXT FILE: */
EXPORT int aud_read_tag_from_tagfile (const char * song_filename, const char * tagdata_filename, Tuple & tuple)
{
    if (! aud_get_bool (nullptr, tagdata_filename))
        return 0;

    GKeyFile * rcfile = g_key_file_new ();
    StringBuf filename = filename_build ({aud_get_path (AudPath::UserDir), tagdata_filename});

    if (! g_key_file_load_from_file (rcfile, filename, G_KEY_FILE_NONE, nullptr))
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
    else
    {
    	   g_free (precedence);
        return 2;
    }
}

EXPORT bool aud_file_read_tag (const char * filename, PluginHandle * decoder,
 VFSFile & file, Tuple & tuple, Index<char> * image, String * error)
{
    auto ip = load_input_plugin (decoder, error);
    if (! ip)
        return false;

    if (! open_input_file (filename, "r", ip, file, error))
        return false;

    Tuple file_tuple, new_tuple;
    new_tuple.set_filename (filename);
    int fromfile = 0;

    /* JWT:blacklist stdin - read_tag does seekeys. :(  if (ip->read_tag (filename, file, new_tuple, image)) */
    if (! strncmp (filename, "stdin://", 8) || (fromfile = aud_read_tag_from_tagfile (filename, 
        "user_tag_data", file_tuple)) < 0 || ip->read_tag (filename, file, new_tuple, image))
    {
        // cleanly replace existing tuple
        new_tuple.set_state (Tuple::Valid);
        if (fromfile < 0)  //ONLY - GOT DATA FROM FILE, ONLY USE THAT (MAY NOT WORK)!
            tuple = std::move (file_tuple);
        else if (fromfile > 0)
        {
            int i_tfld;
            const char * tfld;
            tuple = std::move (new_tuple);
            if (fromfile == 1)  //OVERRIDE - USE FILE DATA, ONLY FILLING IN FIELDS NOT IN FILE W/TAGS DATA:
            {
                tfld = (const char *) file_tuple.get_str (Tuple::Title);
                if (tfld)
                    tuple.set_str (Tuple::Title, tfld);
                tfld = (const char *) file_tuple.get_str (Tuple::Artist);
                if (tfld)
                    tuple.set_str (Tuple::Artist, tfld);
                tfld = (const char *) file_tuple.get_str (Tuple::Album);
                if (tfld)
                    tuple.set_str (Tuple::Album, tfld);
                tfld = (const char *) file_tuple.get_str (Tuple::AlbumArtist);
                if (tfld)
                    tuple.set_str (Tuple::AlbumArtist, tfld);
                tfld = (const char *) file_tuple.get_str (Tuple::Comment);
                if (tfld)
                    tuple.set_str (Tuple::Comment, tfld);
                tfld = (const char *) file_tuple.get_str (Tuple::Genre);
                if (tfld)
                    tuple.set_str (Tuple::Genre, tfld);
                i_tfld = file_tuple.get_int (Tuple::Year);
                if (i_tfld && i_tfld > 0)
                    tuple.set_int (Tuple::Year, i_tfld);
                i_tfld = file_tuple.get_int (Tuple::Track);
                if (i_tfld && i_tfld > 0)
                    tuple.set_int (Tuple::Track, i_tfld);
            }
            else   //DEFAULT - USE NON-EMPTY TAG DATA
            {
                tfld = (const char *) new_tuple.get_str (Tuple::Title);
                if (! tfld)
                {
                    tfld = (const char *) file_tuple.get_str (Tuple::Title);
                    if (tfld)
                        tuple.set_str (Tuple::Title, tfld);
                }
                tfld = (const char *) new_tuple.get_str (Tuple::Artist);
                if (! tfld)
                {
                    tfld = (const char *) file_tuple.get_str (Tuple::Artist);
                    if (tfld)
                        tuple.set_str (Tuple::Artist, tfld);
                }
                tfld = (const char *) new_tuple.get_str (Tuple::Album);
                if (! tfld)
                {
                    tfld = (const char *) file_tuple.get_str (Tuple::Album);
                    if (tfld)
                        tuple.set_str (Tuple::Album, tfld);
                }
                tfld = (const char *) new_tuple.get_str (Tuple::AlbumArtist);
                if (! tfld)
                {
                    tfld = (const char *) file_tuple.get_str (Tuple::AlbumArtist);
                    if (tfld)
                        tuple.set_str (Tuple::AlbumArtist, tfld);
                }
                tfld = (const char *) new_tuple.get_str (Tuple::Comment);
                if (! tfld)
                {
                    tfld = (const char *) file_tuple.get_str (Tuple::Comment);
                    if (tfld)
                        tuple.set_str (Tuple::Comment, tfld);
                }
                tfld = (const char *) new_tuple.get_str (Tuple::Genre);
                if (! tfld)
                {
                    tfld = (const char *) file_tuple.get_str (Tuple::Genre);
                    if (tfld)
                        tuple.set_str (Tuple::Genre, tfld);
                }
                i_tfld = new_tuple.get_int (Tuple::Year);
                if (i_tfld <= 0)
                {
                    i_tfld = file_tuple.get_int (Tuple::Year);
                    if (i_tfld)
                        tuple.set_int (Tuple::Year, i_tfld);
                }
                i_tfld = new_tuple.get_int (Tuple::Track);
                if (i_tfld <= 0)
                {
                    i_tfld = file_tuple.get_int (Tuple::Track);
                    if (i_tfld)
                        tuple.set_int (Tuple::Track, i_tfld);
                }
            }
        }
        else
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
int write_tag_to_tagfile (const char * song_filename, const Tuple & tuple)
{
    GKeyFile * rcfile = g_key_file_new ();
    StringBuf filename = filename_build ({aud_get_path (AudPath::UserDir), "user_tag_data"});

    if (! g_key_file_load_from_file (rcfile, filename, 
            (GKeyFileFlags)(G_KEY_FILE_KEEP_COMMENTS), nullptr))
        AUDDBG ("w:write_tag_to_tagfile: error opening key file (%s), assuming we're creating a new one.",
            (const char *) filename);

    char * precedence = g_key_file_get_string (rcfile, song_filename, "Precedence", nullptr);
    if (! precedence)
        g_key_file_set_string (rcfile, song_filename, "Precedence", "DEFAULT");
    g_free (precedence);
    const char * tfld;
    int i_tfld;

    tfld = (const char *) tuple.get_str (Tuple::Title);
    if (tfld)
        g_key_file_set_string (rcfile, song_filename, "Title", tfld);
    tfld = (const char *) tuple.get_str (Tuple::Artist);
    if (tfld)
        g_key_file_set_string (rcfile, song_filename, "Artist", tfld);
    tfld = (const char *) tuple.get_str (Tuple::Album);
    if (tfld)
        g_key_file_set_string (rcfile, song_filename, "Album", tfld);
    tfld = (const char *) tuple.get_str (Tuple::AlbumArtist);
    if (tfld)
        g_key_file_set_string (rcfile, song_filename, "AlbumArtist", tfld);
    tfld = (const char *) tuple.get_str (Tuple::Comment);
    if (tfld)
        g_key_file_set_string (rcfile, song_filename, "Comment", tfld);
    tfld = (const char *) tuple.get_str (Tuple::Genre);
    if (tfld)
        g_key_file_set_string (rcfile, song_filename, "Genre", tfld);
    i_tfld = tuple.get_int (Tuple::Year);
    if (i_tfld)
        g_key_file_set_double (rcfile, song_filename, "Year", i_tfld);
    i_tfld = tuple.get_int (Tuple::Track);
    if (i_tfld)
        g_key_file_set_double (rcfile, song_filename, "Track", i_tfld);

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
        success = write_tag_to_tagfile (filename, tuple);

    if (success)
        aud_playlist_rescan_file (filename);

    return success;
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
