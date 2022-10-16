/*
 * pixbufs.c
 * Copyright 2010-2012 John Lindgren
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

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <libfauxdcore/audstrings.h>
#include <libfauxdcore/drct.h>
#include <libfauxdcore/probe.h>
#include <libfauxdcore/runtime.h>

#include "internal.h"
#include "libfauxdgui-gtk.h"

static AudguiPixbuf current_pixbuf;

void audgui_pixbuf_uncache ()
{
    current_pixbuf.clear ();
}

EXPORT void audgui_pixbuf_scale_within (AudguiPixbuf & pixbuf, int size)
{
    int width = pixbuf.width ();
    int height = pixbuf.height ();

    if (width <= size && height <= size)
        return;

    if (width > height)
    {
        height = size * height / width;
        width = size;
    }
    else
    {
        width = size * width / height;
        height = size;
    }

    if (width < 1)
        width = 1;
    if (height < 1)
        height = 1;

    pixbuf.capture (gdk_pixbuf_scale_simple (pixbuf.get (), width, height, GDK_INTERP_BILINEAR));
}

EXPORT AudguiPixbuf audgui_pixbuf_request (const char * filename, bool * queued)
{
    AudArtPtr art = aud_art_request (filename, AUD_ART_DATA, queued);

    auto data = art.data ();
    if (data)
    {
        Tuple tuple;
        AudguiPixbuf pixbuf = audgui_pixbuf_from_data (data->begin (), data->len ());
        if (! pixbuf.ref () && aud_get_bool (nullptr, "user_tag_data")
                && aud_read_tag_from_tagfile (filename, "tmp_tag_data", tuple))
        {
            /* JWT:INCOMING STREAM PIXBUF INVALID, TRY (KEEP) tmp_tag_data.Comment FIELD FOR ORIGINAL
               IMAGE, AS THIS USUALLY ONLY HAPPENS IN PODCASTS:
            */
            String comment = tuple.get_str (Tuple::Comment);
            if (comment && comment[0])
            {
                const char * comment_ptr = (const char *) comment;
                const char * sep = strstr (comment_ptr, ";file://");
                String main_imageuri = sep ? String (str_printf ("%.*s", (int)(sep - comment_ptr), comment_ptr))
                        : comment;
                art = aud_art_request ((const char *) main_imageuri, AUD_ART_DATA, queued);
                data = art.data ();
                if (data)
                    aud_set_bool (nullptr, "_skip_web_art_search", true);

                return data ? audgui_pixbuf_from_data (data->begin (), data->len ()) : AudguiPixbuf ();
            }
            return AudguiPixbuf ();
        }
        return pixbuf;
    }
    return AudguiPixbuf ();
}

EXPORT AudguiPixbuf audgui_pixbuf_fallback ()
{
    static AudguiPixbuf fallback;

    /* JWT:ALLOW USER TO SPECIFY A DIFFERENT FALLBACK IMAGE: */
    String artdefault = aud_get_str(nullptr, "default_cover_file");
    if (artdefault && artdefault[0])
    {
        AudguiPixbuf user_fallback = audgui_pixbuf_request ((const char *) artdefault);
        if (user_fallback)
            return user_fallback.ref ();
    }

    if (! fallback)
    {
        GtkIconTheme * icon_theme = gtk_icon_theme_get_default ();
        int icon_size = audgui_to_native_dpi (48);

        fallback.capture (gtk_icon_theme_load_icon (icon_theme,
         "audio-x-generic", icon_size, (GtkIconLookupFlags) 0, nullptr));
    }

    return fallback.ref ();
}

EXPORT AudguiPixbuf audgui_pixbuf_request_current (bool * queued)
{
    if (queued)
        * queued = false;

    if (! current_pixbuf)
    {
        String filename = aud_drct_get_filename ();
        if (filename)
            current_pixbuf = audgui_pixbuf_request (filename, queued);
    }

    return current_pixbuf.ref ();
}

EXPORT AudguiPixbuf audgui_pixbuf_from_data (const void * data, int64_t size)
{
    GdkPixbuf * pixbuf = nullptr;
    GdkPixbufLoader * loader = gdk_pixbuf_loader_new ();
    GError * error = nullptr;

    if (gdk_pixbuf_loader_write (loader, (const unsigned char *) data, size,
     & error) && gdk_pixbuf_loader_close (loader, & error))
    {
        if ((pixbuf = gdk_pixbuf_loader_get_pixbuf (loader)))
            g_object_ref (pixbuf);
    }
    else
    {
        AUDWARN ("While loading pixbuf: %s\n", error->message);
        g_error_free (error);
    }

    g_object_unref (loader);
    return AudguiPixbuf (pixbuf);
}
