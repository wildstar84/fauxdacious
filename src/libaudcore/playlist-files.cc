/*
 * playlist-files.c
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "playlist-internal.h"

#include "audstrings.h"
#include "i18n.h"
#include "interface.h"
#include "plugin.h"
#include "plugins-internal.h"
#include "runtime.h"

EXPORT bool aud_filename_is_playlist (const char * filename)
{
    bool userurl2playlist = false;

    if (strstr (filename, "://") && strncmp (filename, "file://", 7)
            && strncmp (filename, "cdda://", 7) && strncmp (filename, "dvd://", 6)
            && strncmp (filename, "stdin://", 8))  //JWT:WE'RE SOME KIND OF URL:
    {
        String url_helper = aud_get_str ("audacious", "url_helper");
    	   StringBuf temp_playlist_filename = filename_build ({aud_get_path (AudPath::UserDir), "tempurl.pls"});
    	   remove ((const char *) temp_playlist_filename);
        if (url_helper[0])  //JWT:WE HAVE A PERL HELPER, LESSEE IF IT RECOGNIZES & CONVERTS IT (ie. tunein.com, youtube, etc):
        {
            system ((const char *) str_concat ({url_helper, " ", filename, " ", aud_get_path (AudPath::UserDir)}));    
            if (access((const char *) temp_playlist_filename, F_OK ) != -1 )
                userurl2playlist = true;
        }
    }

    /* JWT:THE HELPER CONVERTS RECOGNIZED URL PATTERNS TO A TEMP. SINGLE-ITEM PLAYLIST AS THIS IS 
       THE ONLY WAY IN AUDACIOUS TO *CHANGE* THE URL (filename) TO SOMETHING ELSE! */
    StringBuf ext = userurl2playlist ? str_printf (_("pls")) : uri_get_extension (filename);
    if (ext)
    {
        for (PluginHandle * plugin : aud_plugin_list (PluginType::Playlist))
        {
            if (aud_plugin_get_enabled (plugin) && playlist_plugin_has_ext (plugin, ext))
                return true;
        }
    }

    return false;
}

bool playlist_load (const char * filename, String & title, Index<PlaylistAddItem> & items)
{
    bool userurl2playlist = false;
    bool plugin_found = false;

    AUDINFO ("Loading playlist %s.\n", filename);

    if (strstr (filename, "://") && ! strstr (filename, "file:/"))
    {
        StringBuf temp_playlist_filename = filename_build ({aud_get_path (AudPath::UserDir), "tempurl.pls"});
        if (access((const char *) temp_playlist_filename, F_OK ) != -1 )
            userurl2playlist = true;
    }

    StringBuf ext = userurl2playlist ? str_printf (_("pls")) : uri_get_extension (filename);

    if (ext)
    {
        for (PluginHandle * plugin : aud_plugin_list (PluginType::Playlist))
        {
            if (! aud_plugin_get_enabled (plugin) || ! playlist_plugin_has_ext (plugin, ext))
                continue;

            AUDINFO ("Trying playlist plugin %s.\n", aud_plugin_get_name (plugin));
            plugin_found = true;

            auto pp = (PlaylistPlugin *) aud_plugin_get_header (plugin);
            if (! pp)
                continue;

            if (userurl2playlist)
            {
                //ext.steal (str_printf (_("tempurl.pls")));
                //ext.steal (filename_build ({aud_get_path (AudPath::UserDir), ext}));
                //ext.steal (filename_to_uri (ext));
                ext = filename_build ({aud_get_path (AudPath::UserDir), _("tempurl.pls")});
                ext = filename_to_uri (ext);

                VFSFile file ((const char *) ext, "r");
                if (! file)
                {
                    aud_ui_show_error (str_printf (_("Error opening %s:\n%s"),
                            (const char *) ext, file.error ()));
                    return false;
                }

                if (pp->load ((const char *) ext, file, title, items))
                    return true;
            }
            else
            {
                VFSFile file (filename, "r");
                if (! file)
                {
                    aud_ui_show_error (str_printf (_("Error opening %s:\n%s"),
                     filename, file.error ()));
                    return false;
                }

                if (pp->load (filename, file, title, items))
                    return true;
            }

            title = String ();
            items.clear ();
        }
    }

    if (plugin_found)
        aud_ui_show_error (str_printf (_("Error loading %s."), filename));
    else
        aud_ui_show_error (str_printf (_("Cannot load %s: unsupported file "
         "name extension."), filename));

    return false;
}

// This procedure is only used when loading playlists from ~/.config/audacious;
// hence, it is drastically simpler than the full-featured routines in adder.cc.
// All support for adding folders, cuesheets, subtunes, etc. is omitted here.
// Additionally, in order to avoid heavy I/O at startup, failed entries are not
// rescanned; they can be rescanned later by refreshing the playlist. */
bool playlist_insert_playlist_raw (int list, int at, const char * filename)
{
    String title;
    Index<PlaylistAddItem> items;

    if (! playlist_load (filename, title, items))
        return false;

    if (title && ! aud_playlist_entry_count (list))
        aud_playlist_set_title (list, title);

    playlist_entry_insert_batch_raw (list, at, std::move (items));

    return true;
}

EXPORT bool aud_playlist_save (int list, const char * filename, Playlist::GetMode mode)
{
    String title = aud_playlist_get_title (list);

    Index<PlaylistAddItem> items;
    items.insert (0, aud_playlist_entry_count (list));

    int i = 0;
    for (PlaylistAddItem & item : items)
    {
        item.filename = aud_playlist_entry_get_filename (list, i);
        item.tuple = aud_playlist_entry_get_tuple (list, i, mode);
        item.tuple.delete_fallbacks ();
        i ++;
    }

    AUDINFO ("Saving playlist %s.\n", filename);

    StringBuf ext = uri_get_extension (filename);

    if (ext)
    {
        for (PluginHandle * plugin : aud_plugin_list (PluginType::Playlist))
        {
            if (! aud_plugin_get_enabled (plugin) || ! playlist_plugin_has_ext (plugin, ext))
                continue;

            PlaylistPlugin * pp = (PlaylistPlugin *) aud_plugin_get_header (plugin);
            if (! pp || ! pp->can_save)
                continue;

            VFSFile file (filename, "w");
            if (! file)
                return false;

            return pp->save (filename, file, title, items) && file.fflush () == 0;
        }
    }

    aud_ui_show_error (str_printf (_("Cannot save %s: unsupported file name extension."), filename));

    return false;
}

EXPORT Index<Playlist::SaveFormat> aud_playlist_save_formats ()
{
    Index<Playlist::SaveFormat> formats;

    for (auto plugin : aud_plugin_list (PluginType::Playlist))
    {
        if (! aud_plugin_get_enabled (plugin) || ! playlist_plugin_can_save (plugin))
            continue;

        auto & format = formats.append ();
        format.name = String (aud_plugin_get_name (plugin));

        for (auto & ext : playlist_plugin_get_exts (plugin))
            format.exts.append (ext);
    }

    return formats;
}
