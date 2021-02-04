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

#include <string.h>

#include <glib.h>  /* for g_dir_open, g_file_test */

#include "audstrings.h"
#include "index.h"
#include "tuple.h"
#include "runtime.h"

struct SearchParams {
    String filename;
    Index<String> include, exclude;
};

static bool has_front_cover_extension (const char * name)
{
    const char * ext = strrchr (name, '.');
    if (! ext)
        return false;

    return ! strcmp_nocase (ext, ".jpg") || ! strcmp_nocase (ext, ".jpeg") ||
     ! strcmp_nocase (ext, ".png");
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
            use_album_tag_cover = true;

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
    return image_local ? String (filename_to_uri (image_local)) : String ();
}
