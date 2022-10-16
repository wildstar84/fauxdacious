/*
 * art.cc
 * Copyright 2014 William Pitcock
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

#include <QApplication>
#include <QPixmap>
#include <QIcon>
#include <QImage>

#include <libfauxdcore/audstrings.h>
#include <libfauxdcore/drct.h>
#include <libfauxdcore/probe.h>
#include <libfauxdcore/runtime.h>
#include <libfauxdqt/libfauxdqt.h>

namespace audqt {

EXPORT QImage art_request (const char * filename, bool * queued)
{
    AudArtPtr art = aud_art_request (filename, AUD_ART_DATA, queued);

    auto data = art.data ();
//x    return data ? QImage::fromData ((const uchar *) data->begin (), data->len ()) : QImage ();
    if (data)
    {
        Tuple tuple;
        QImage qimg = QImage::fromData ((const uchar *) data->begin (), data->len ());
        if (qimg.isNull () && aud_get_bool (nullptr, "user_tag_data")
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

                return data ? QImage::fromData ((const uchar *) data->begin (), data->len ()) : QImage ();
            }
            return QImage ();
        }
        return qimg;
    }
    return QImage ();
}

EXPORT QPixmap art_scale (const QImage & image, unsigned int w, unsigned int h, bool want_hidpi)
{
    // return original image if requested size is zero,
    // or original size is smaller than requested size
    if ((w == 0 && h == 0) || ((unsigned) image.width () <= w && (unsigned) image.height () <= h))
        return QPixmap::fromImage (image);

    qreal r = want_hidpi ? qApp->devicePixelRatio () : 1;
    auto pixmap = QPixmap::fromImage (image.scaled (w * r, h * r,
     Qt::KeepAspectRatio, Qt::SmoothTransformation));

    pixmap.setDevicePixelRatio (r);
    return pixmap;
}

EXPORT QPixmap art_request (const char * filename, unsigned int w, unsigned int h, bool want_hidpi)
{
    auto img = art_request (filename);
    if (! img.isNull ())
        return art_scale (img, w, h, want_hidpi);

    unsigned size = to_native_dpi (48);
    //x return get_icon ("audio-x-generic").pixmap (aud::min (w, size), aud::min (h, size));
    return QPixmap (get_icon ("audio-x-generic").pixmap (aud::max (w, size), aud::max (h, size)));
}

EXPORT QPixmap art_request_current (unsigned int w, unsigned int h, bool want_hidpi)
{
    String filename = aud_drct_get_filename ();
    if (! filename)
        return QPixmap ();

    auto img = art_request (filename);
    if (img.isNull ())
        return QPixmap ();

    return art_scale (img, w, h, want_hidpi);
}

EXPORT QPixmap art_request_fallback (unsigned int w, unsigned int h)
{
    unsigned size = to_native_dpi (48);

    /* JWT:ALLOW USER TO SPECIFY A DIFFERENT FALLBACK IMAGE: */
    String artdefault = aud_get_str(nullptr, "default_cover_file");
    if (artdefault && artdefault[0])
    {
        QPixmap user_fallback = art_request ((const char *) artdefault, w, h);
        if (! user_fallback.isNull ())
            return user_fallback;
    }

    return QPixmap (get_icon ("audio-x-generic").pixmap (aud::max (w, size), aud::max (h, size)));
}

} // namespace audqt
