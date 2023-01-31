/*
 * art-search.c
 * Copyright 2006-2013 Michael Hanselmann, Yoshiki Yazawa, and John Lindgren
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

#include <sys/stat.h>
#include <string.h>
#include <utime.h>

#include <glib.h>  /* for g_dir_open, g_file_test */

#include "audstrings.h"
#include "index.h"
#include "tuple.h"
#include "runtime.h"
#include "drct.h"

struct SearchParams {
    String filename;
    Index<String> include, exclude;
};

static bool has_front_cover_extension (const char * name)
{
    const char * ext = strrchr (name, '.');
    if (! ext)
        return false;

    Index<String> extlist = str_list_to_index (".jpg,.png,.jpeg,.gif,.webp", ",");
    for (auto & tryext : extlist)
    {
        if (strcmp_nocase (ext, tryext))
            continue;

        return true;
    }
    return false;
}

static bool cover_name_filter (const char * name,
 const Index<String> & keywords)
{
    if (! keywords.len ())
        return false;

    for (const String & keyword : keywords)
    {
        if (strstr_nocase (name, keyword))
            return true;
    }

    return false;
}

static String fileinfo_recursive_get_image (const char * path,
        const SearchParams * params, int depth, Tuple & tuple)
{
    GDir * d = g_dir_open (path, 0, nullptr);
    if (! d)
        return String ();

    const char * name;

    if (! depth)
    {
        bool use_file_cover = aud_get_bool (nullptr, "use_file_cover");
        bool use_album_tag_cover = false;
        String album_tag = tuple.get_str (Tuple::Album);
        String album_artfile = String ("");

        if (aud_get_bool (nullptr, "use_album_tag_cover") && album_tag && album_tag[0])
        {
            use_album_tag_cover = true;
            StringBuf album_tag_buf = str_copy (album_tag);
            str_replace_char (album_tag_buf, ' ', '~');  // JWT:PROTECT SPACES, WHICH I STUPIDLY DIDN'T ENCODE IN ALBUMART!
            album_tag_buf = str_encode_percent (album_tag_buf);
            str_replace_char (album_tag_buf, '~', ' ');  // JWT:UNPROTECT SPACES!
            album_tag = String (album_tag_buf);
        }

        if (use_file_cover || use_album_tag_cover)
        {
            /* Look for images matching file name */
            while ((name = g_dir_read_name (d)))
            {
                StringBuf newpath = filename_build ({path, name});

                if (! g_file_test (newpath, G_FILE_TEST_IS_DIR) &&
                        has_front_cover_extension (name))
                {
                    if (use_file_cover && same_basename (name, params->filename))
                    {
                        g_dir_close (d);
                        return String (newpath);
                    }
                    else if (use_album_tag_cover && same_basename (name, album_tag))
                        album_artfile = String (newpath);
                }
            }

            g_dir_rewind (d);
        }
        if (album_artfile && album_artfile[0])
        {
            g_dir_close (d);
            return album_artfile;
        }
    }
    /* Search for files using filter */
    while ((name = g_dir_read_name (d)))
    {
        StringBuf newpath = filename_build ({path, name});

        if (! g_file_test (newpath, G_FILE_TEST_IS_DIR)
                && has_front_cover_extension (name)
                && cover_name_filter (name, params->include)
                && ! cover_name_filter (name, params->exclude))
        {
            g_dir_close (d);
            return String (newpath);
        }
    }

    g_dir_rewind (d);

    if (aud_get_bool (nullptr, "recurse_for_cover") && depth < aud_get_int (nullptr, "recurse_for_cover_depth"))
    {
        /* Descend into directories recursively. */
        while ((name = g_dir_read_name (d)))
        {
            StringBuf newpath = filename_build ({path, name});

            if (g_file_test (newpath, G_FILE_TEST_IS_DIR))
            {
                String tmp = fileinfo_recursive_get_image (newpath, params, depth + 1, tuple);

                if (tmp)
                {
                    g_dir_close (d);
                    return tmp;
                }
            }
        }
    }

    g_dir_close (d);
    return String ();
}

String art_search (const char * filename, Tuple & tuple)
{
    StringBuf local = uri_to_filename (filename);
    if (! local)
        return String ();

    const char * elem = last_path_element (local);
    if (! elem)
        return String ();

    String include = aud_get_str (nullptr, "cover_name_include");
    String exclude = aud_get_str (nullptr, "cover_name_exclude");

    SearchParams params = {
        String (elem),
        str_list_to_index (include, ", "),
        str_list_to_index (exclude, ", ")
    };

    cut_path_element (local, elem - local);

    String image_local = fileinfo_recursive_get_image (local, & params, 0, tuple);
    if (image_local)
        return String (filename_to_uri (image_local));

    /* JWT:NOW CHECK THE ALBUM-ART CACHE (IF USER WISHES): */
    if (! aud_get_bool (nullptr, "search_albumart_cache"))
        return String ();

    String Title = tuple.get_str (Tuple::Title);
    String Artist = tuple.get_str (Tuple::Artist);
    String Album = tuple.get_str (Tuple::Album);
    String audio_fn = tuple.get_str (Tuple::AudioFile);
    if (! audio_fn || ! audio_fn[0])
        audio_fn = aud_drct_get_filename ();

    if (Title && Title[0])
    {
        bool split_titles = aud_get_bool (nullptr, "split_titles");
        const char * album = (const char *) Album;
        if (album && album[0])  // ALBUM FIELD NOT BLANK AND NOT A FILE/URL:
        {
            const char * album_uri = strstr (album, "://");  // FOR URI, WE'LL ASSUME LONGEST IS "stdin" (5 chars)
            if (album_uri && (album_uri-album) < 6)  // ALBUM FIELD IS A URI (PBLY A PODCAST/VIDEO FROM STREAMFINDER!):
                Album = String ("_");
            else if (split_titles)
            {
                /* ALBUM MAY ALSO CONTAIN THE STREAM NAME (IE. "<ALBUM> - <STREAM NAME>"): STRIP THAT OFF: */
                const char * throwaway = strstr (album, " - ");
                int albumlen = throwaway ? throwaway - album : -1;
                Album = String (str_copy (album, albumlen));
            }
        }
        else
            Album = String ("_");

        if (! split_titles)
        {
            /* ARTIST MAY BE IN TITLE INSTEAD (IE. "<ARTIST> - <TITLE>"): IF SO, USE THAT FOR ARTIST: */
            const char * title = (const char *) Title;
            if (title)
            {
                const char * artistlen = strstr (title, " - ");
                if (artistlen)
                {
                    Artist = String (str_copy (title, artistlen - title));
                    const char * titleoffset = artistlen+3;
                    if (titleoffset)
                        Title = String (str_copy (artistlen+3, -1));
                }
            }
        }

        StringBuf albart_FN, artist_FN;
        StringBuf album_buf = str_copy (Album);
        str_replace_char (album_buf, ' ', '~');  // JWT:PROTECT SPACES, WHICH I STUPIDLY DIDN'T ENCODE IN ALBUMART!
        if (Artist && Artist[0])
        {
            StringBuf artist_buf = str_copy (Artist);
            str_replace_char (artist_buf, ' ', '~');
            artist_FN = str_encode_percent (artist_buf);
            albart_FN = str_concat ({(const char *) str_encode_percent (album_buf), "__",
                    (const char *) artist_FN});
            str_replace_char (artist_FN, '~', ' ');  // JWT:UNPROTECT SPACES!
        }
        else if (Title && Title[0])
        {
            StringBuf title_buf = str_copy (Title);
            str_replace_char (title_buf, ' ', '~');
            albart_FN = str_concat ({(const char *) str_encode_percent (album_buf), "__",
                    (const char *) str_encode_percent (title_buf)});
            artist_FN = StringBuf ();
        }
        else
        {
            albart_FN = str_encode_percent (album_buf);
            artist_FN = StringBuf ();
        }
        str_replace_char (albart_FN, '~', ' ');  // JWT:UNPROTECT SPACES!

        String coverart_file;
        Index<String> extlist = str_list_to_index ("jpg,png,gif,jpeg", ",");
        for (auto & ext : extlist)
        {
            coverart_file = String (str_concat ({aud_get_path (AudPath::UserDir),
                    "/albumart/", (const char *) albart_FN, ".", (const char *) ext}));
            const char * filenamechar = coverart_file;
            struct stat statbuf;
            if (stat (filenamechar, &statbuf) >= 0)  // ART IMAGE FILE EXISTS:
            {
                if (utime (filenamechar, nullptr) < 0)
                    AUDWARN ("i:Failed to update art-file time (for easier user-lookup)!\n");

                return String (filename_to_uri (filenamechar));
            }
        }
        /* NO ALBUM IMG. IN CACHE, CHECK FOR IMG. MATCHING THIS ARTIST (ADDED BY USER): */
        if (artist_FN)
        {
            for (auto & ext : extlist)
            {
                coverart_file = String (str_concat ({aud_get_path (AudPath::UserDir),
                        "/albumart/", (const char *) artist_FN, ".", (const char *) ext}));
                const char * filenamechar = coverart_file;
                struct stat statbuf;
                if (stat (filenamechar, &statbuf) >= 0)  // ART IMAGE FILE EXISTS FOR ARTIST:
                    return String (filename_to_uri (filenamechar));
            }
        }
    }

    return String ();
}
