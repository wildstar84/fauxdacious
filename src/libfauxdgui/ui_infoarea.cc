/*
 * ui_infoarea.c
 * Copyright 2010-2012 William Pitcock and John Lindgren
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

#include <math.h>
#include <sys/stat.h>
#include <string.h>

#include <gtk/gtk.h>

#include <libfauxdcore/drct.h>
#include <libfauxdcore/hook.h>
#include <libfauxdcore/interface.h>
#include <libfauxdcore/runtime.h>
#include <libfauxdcore/probe.h>
#include <libfauxdcore/plugins.h>
#include <libfauxdcore/audstrings.h>

#include "gtk-compat.h"
#include "libfauxdgui-gtk.h"
#include "ui_infoarea.h"

#define VIS_BANDS 12
#define VIS_DELAY 2 /* delay before falloff in frames */
#define VIS_FALLOFF 2 /* falloff in decibels per frame */

int SPACING, ICON_SIZE, HEIGHT, BAND_WIDTH, BAND_SPACING, VIS_WIDTH, VIS_SCALE, VIS_CENTER;

static void compute_sizes ()
{
    int dpi = audgui_get_dpi ();

    SPACING = aud::rescale (dpi, 12, 1);
    ICON_SIZE = 2 * aud::rescale (dpi, 3, 1); // should be divisible by 2
    HEIGHT = ICON_SIZE + 2 * SPACING;
    BAND_WIDTH = aud::rescale (dpi, 16, 1);
    BAND_SPACING = aud::rescale (dpi, 48, 1);
    VIS_WIDTH = VIS_BANDS * (BAND_WIDTH + BAND_SPACING) - BAND_SPACING + 2 * SPACING;
    VIS_SCALE = aud::rescale (ICON_SIZE, 8, 5);
    VIS_CENTER = VIS_SCALE + SPACING;
}

typedef struct {
    GtkWidget * box, * main;

    String title, artist, album;
    String last_title, last_artist, last_album;
    AudguiPixbuf pb, last_pb;
    float alpha, last_alpha;

    bool show_art;
    bool stopped;
    GtkWidget * widget;
} UIInfoArea;

class InfoAreaVis : public Visualizer
{
public:
    constexpr InfoAreaVis () :
        Visualizer (Freq) {}

    GtkWidget * widget = nullptr;
    float bars[VIS_BANDS] {};
    char delay[VIS_BANDS] {};

    void clear ();
    void render_freq (const float * freq);
};

static InfoAreaVis vis;

/****************************************************************************/

static UIInfoArea * area = nullptr;

void InfoAreaVis::render_freq (const float * freq)
{
    /* xscale[i] = pow (256, i / VIS_BANDS) - 0.5; */
    const float xscale[VIS_BANDS + 1] = {0.5, 1.09, 2.02, 3.5, 5.85, 9.58,
     15.5, 24.9, 39.82, 63.5, 101.09, 160.77, 255.5};

    for (int i = 0; i < VIS_BANDS; i ++)
    {
        /* 40 dB range */
        float x = 40 + compute_freq_band (freq, xscale, i, VIS_BANDS);

        bars[i] -= aud::max (0, VIS_FALLOFF - delay[i]);

        if (delay[i])
            delay[i] --;

        if (x > bars[i])
        {
            bars[i] = x;
            delay[i] = VIS_DELAY;
        }
    }

    if (widget)
        gtk_widget_queue_draw (widget);
}

void InfoAreaVis::clear ()
{
    memset (bars, 0, sizeof bars);
    memset (delay, 0, sizeof delay);

    if (widget)
        gtk_widget_queue_draw (widget);
}

/****************************************************************************/

static void clear (GtkWidget * widget, cairo_t * cr)
{
    double r = 1, g = 1, b = 1;

    /* In a dark theme, try to match the tone of the base color */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    auto & c = (gtk_widget_get_style (widget))->base[GTK_STATE_NORMAL];
    int v = aud::max (aud::max (c.red, c.green), c.blue);

    if (v >= 10*256 && v < 80*256)
    {
        r = (double)c.red / v;
        g = (double)c.green / v;
        b = (double)c.blue / v;
    }
G_GNUC_END_IGNORE_DEPRECATIONS

    GtkAllocation alloc;
    gtk_widget_get_allocation (widget, & alloc);

    cairo_pattern_t * gradient = cairo_pattern_create_linear (0, 0, 0, alloc.height);
    cairo_pattern_add_color_stop_rgb (gradient, 0, 0.25 * r, 0.25 * g, 0.25 * b);
    cairo_pattern_add_color_stop_rgb (gradient, 0.5, 0.15 * r, 0.15 * g, 0.15 * b);
    cairo_pattern_add_color_stop_rgb (gradient, 0.5, 0.1 * r, 0.1 * g, 0.1 * b);
    cairo_pattern_add_color_stop_rgb (gradient, 1, 0, 0, 0);

    cairo_set_source (cr, gradient);
    cairo_rectangle (cr, 0, 0, alloc.width, alloc.height);
    cairo_fill (cr);

    cairo_pattern_destroy (gradient);
}

static void draw_text (GtkWidget * widget, cairo_t * cr, int x, int y, int
 width, float r, float g, float b, float a, const char * font,
 const char * text)
{
    cairo_move_to (cr, x, y);
    cairo_set_source_rgba (cr, r, g, b, a);

    PangoFontDescription * desc = pango_font_description_from_string (font);
    PangoLayout * pl = gtk_widget_create_pango_layout (widget, nullptr);
    pango_layout_set_text (pl, text, -1);
    pango_layout_set_font_description (pl, desc);
    pango_font_description_free (desc);
    pango_layout_set_width (pl, width * PANGO_SCALE);
    pango_layout_set_ellipsize (pl, PANGO_ELLIPSIZE_END);

    pango_cairo_show_layout (cr, pl);

    g_object_unref (pl);
}

/****************************************************************************/

static void rgb_to_hsv (float r, float g, float b, float * h, float * s,
 float * v)
{
    float max, min;

    max = r;
    if (g > max)
        max = g;
    if (b > max)
        max = b;

    min = r;
    if (g < min)
        min = g;
    if (b < min)
        min = b;

    * v = max;

    if (max == min)
    {
        * h = 0;
        * s = 0;
        return;
    }

    if (r == max)
        * h = 1 + (g - b) / (max - min);
    else if (g == max)
        * h = 3 + (b - r) / (max - min);
    else
        * h = 5 + (r - g) / (max - min);

    * s = (max - min) / max;
}

static void hsv_to_rgb (float h, float s, float v, float * r, float * g,
 float * b)
{
    for (; h >= 2; h -= 2)
    {
        float * p = r;
        r = g;
        g = b;
        b = p;
    }

    if (h < 1)
    {
        * r = 1;
        * g = 0;
        * b = 1 - h;
    }
    else
    {
        * r = 1;
        * g = h - 1;
        * b = 0;
    }

    * r = v * (1 - s * (1 - * r));
    * g = v * (1 - s * (1 - * g));
    * b = v * (1 - s * (1 - * b));
}

static void get_color (GtkWidget * widget, int i, float * r, float * g, float * b)
{
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    auto & c = (gtk_widget_get_style (widget))->base[GTK_STATE_SELECTED];
    float h, s, v;

    rgb_to_hsv (c.red / 65535.0, c.green / 65535.0, c.blue / 65535.0, & h, & s, & v);
G_GNUC_END_IGNORE_DEPRECATIONS

    if (s < 0.1) /* monochrome theme? use blue instead */
        h = 4.6;

    s = 1 - 0.9 * i / (VIS_BANDS - 1);
    v = 0.75 + 0.25 * i / (VIS_BANDS - 1);

    hsv_to_rgb (h, s, v, r, g, b);
}

#ifdef USE_GTK3
static gboolean draw_vis_cb (GtkWidget * widget, cairo_t * cr)
{
#else
static gboolean draw_vis_cb (GtkWidget * widget, GdkEventExpose * event)
{
    cairo_t * cr = gdk_cairo_create (gtk_widget_get_window (widget));
#endif
    clear (widget, cr);

    for (int i = 0; i < VIS_BANDS; i++)
    {
        int x = SPACING + i * (BAND_WIDTH + BAND_SPACING);
        int v = aud::clamp ((int) (vis.bars[i] * VIS_SCALE / 40), 0, VIS_SCALE);
        int m = aud::min (VIS_CENTER + v, HEIGHT);

        float r, g, b;
        get_color (widget, i, & r, & g, & b);

        cairo_set_source_rgb (cr, r, g, b);
        cairo_rectangle (cr, x, VIS_CENTER - v, BAND_WIDTH, v);
        cairo_fill (cr);

        cairo_set_source_rgb (cr, r * 0.3, g * 0.3, b * 0.3);
        cairo_rectangle (cr, x, VIS_CENTER, BAND_WIDTH, m - VIS_CENTER);
        cairo_fill (cr);
    }

#ifndef USE_GTK3
    cairo_destroy (cr);
#endif
    return true;
}

static void draw_album_art (cairo_t * cr)
{
    g_return_if_fail (area);

    if (area->pb)
    {
        int left = SPACING + (ICON_SIZE - area->pb.width ()) / 2;
        int top = SPACING + (ICON_SIZE - area->pb.height ()) / 2;
        gdk_cairo_set_source_pixbuf (cr, area->pb.get (), left, top);
        cairo_paint_with_alpha (cr, area->alpha);
    }

    if (area->last_pb)
    {
        int left = SPACING + (ICON_SIZE - area->last_pb.width ()) / 2;
        int top = SPACING + (ICON_SIZE - area->last_pb.height ()) / 2;
        gdk_cairo_set_source_pixbuf (cr, area->last_pb.get (), left, top);
        cairo_paint_with_alpha (cr, area->last_alpha);
    }
}

static void draw_title (cairo_t * cr)
{
    g_return_if_fail (area);

    GtkAllocation alloc;
    gtk_widget_get_allocation (area->main, & alloc);

    int x = area->show_art ? HEIGHT : SPACING;
    int y_offset1 = ICON_SIZE / 2;
    int y_offset2 = ICON_SIZE * 3 / 4;
    int width = alloc.width - x;

    if (area->title)
        draw_text (area->main, cr, x, SPACING, width, 1, 1, 1, area->alpha,
         "18", area->title);
    if (area->last_title)
        draw_text (area->main, cr, x, SPACING, width, 1, 1, 1, area->last_alpha,
         "18", area->last_title);
    if (area->artist)
        draw_text (area->main, cr, x, SPACING + y_offset1, width, 1, 1, 1,
         area->alpha, "9", area->artist);
    if (area->last_artist)
        draw_text (area->main, cr, x, SPACING + y_offset1, width, 1, 1, 1,
         area->last_alpha, "9", area->last_artist);
    if (area->album)
        draw_text (area->main, cr, x, SPACING + y_offset2, width, 0.7,
         0.7, 0.7, area->alpha, "9", area->album);
    if (area->last_album)
        draw_text (area->main, cr, x, SPACING + y_offset2, width, 0.7,
         0.7, 0.7, area->last_alpha, "9", area->last_album);
}

#ifdef USE_GTK3
static gboolean draw_cb (GtkWidget * widget, cairo_t * cr)
{
    g_return_val_if_fail (area, false);
#else
static gboolean draw_cb (GtkWidget * widget, GdkEventExpose * event)
{
    cairo_t * cr = gdk_cairo_create (gtk_widget_get_window (widget));
#endif
    clear (widget, cr);

    draw_album_art (cr);
    draw_title (cr);

#ifndef USE_GTK3
    cairo_destroy (cr);
#endif
    return true;
}

static void ui_infoarea_do_fade (void *)
{
    g_return_if_fail (area);
    bool done = true;

    if (aud_drct_get_playing () && area->alpha < 1)
    {
        area->alpha += 0.1;
        done = false;
    }

    if (area->last_alpha > 0)
    {
        area->last_alpha -= 0.1;
        done = false;
    }

    gtk_widget_queue_draw (area->main);

    if (done)
        timer_remove (TimerRate::Hz30, ui_infoarea_do_fade);
}

static void ui_infoarea_set_title ()
{
    g_return_if_fail (area);

    Tuple tuple = aud_drct_get_tuple ();
    String title = tuple.get_str (Tuple::Title);
    String artist = tuple.get_str (Tuple::Artist);
    String album = tuple.get_str (Tuple::Album);

    if (! g_strcmp0 (title, area->title) && ! g_strcmp0 (artist, area->artist)
     && ! g_strcmp0 (album, area->album))
        return;

    area->title = std::move (title);
    area->artist = std::move (artist);
    area->album = std::move (album);

    gtk_widget_queue_draw (area->main);
}

static void set_album_art ()
{
    g_return_if_fail (area);

    area->pb = AudguiPixbuf ();
    if (! area->show_art)
        return;  // WE'RE ALREADY SET TO HIDE, SO HIDE & DONE!

    bool noChannelArt = true;   /* JWT:REMAINS TRUE IF NO CHANNEL-ART FOUND. */
    /* JWT:NOTE:  DIRECTORY ICONS ARE *NOT* CONSIDERED "CHANNEL-ART", THOUGH THEY ARE TREATED AS SUCH,
       IFF THERE'S NO PRIMARY ART IMAGE FOUND!
    */
    bool have_dir_icon_art = false;  /* JWT:BECOMES TRUE IF ENTRY IS FILE AND A DIRECTORY-ICON IS FOUND. */
    bool albumart_plugin_isactive = aud_get_bool ("albumart", "_isactive");
    AudguiPixbuf dir_icon_art;
    const char * filename = aud_drct_get_filename ();  // JWT:FIXME: THIS THANG CAN COME UP NULL ON GTK-STARTUP!!!

    if (albumart_plugin_isactive)
    {
        /* JWT:AlbumArt PLUGIN ACTIVE, SEE IF WE HAVE "CHANNEL ICON" TO PUT IN INFO_BAR INSTEAD: */
        Tuple tuple = aud_drct_get_tuple ();
        String tfld = tuple.get_str (Tuple::Comment);
        if (tfld && tfld[0])
        {
            /* NOTE:  ANY "CHANNEL ICON" FILE URI WILL ALWAYS BE IN THE TUPLE COMMENT UNLESS
               WE'RE A STREAM AND THE COMMENT FIELD HAS SOMETHING ELSE NON-BLANK IN IT!  THIS
               IS BECAUSE IF A STREAM HAS CHANNEL ART, IT CAME FROM THE URL HELPER AND WILL BE
               THERE UNLESS THE STREAM'S STREAMING METADATA OVERWRITES IT WITH AN ACTUAL
               "COMMENT", THEN IT MUST BE IN tmp_tag_data:
               PROGRAMMER NOTE:  THIS ONLY APPLIES TO "one-off" PODCASTS & VIDEOS, B/C STREAMING
               STATIONS, ETC. ONLY PROVIDE AN ICON FOR THE STATION, WHICH BECOMES THE CHANNEL-
               ICON WHEN albumart PLUGIN FINDS A MAIN IMAGE FOR THE CURRENTLY-PLAYING SONG.
            */
            const char * tfld_offset = strstr ((const char *) tfld, ";file://");
            if (tfld_offset)
            {
                /* FOUND CHANNEL ICON IN COMMENT FIELD: */
                tfld_offset += 1;
                if (tfld_offset)
                {
                    area->pb = audgui_pixbuf_request (tfld_offset);
                    if (area->pb)
                        noChannelArt = false;
                }
            }
            else if (filename && aud_read_tag_from_tagfile (filename, "tmp_tag_data", tuple)
                    && (! strncmp (filename, "http://", 7) || ! strncmp (filename, "https://", 8)))
            {
                /* STREAM COMMENT WITHOUT CHANNEL ICON (OVERWRITTEN BY STREAM'S METATAGS?),
                   (THIS ONLY APPLIES TO STREAMING VIDEOS/PODCASTS, WHICH ALWAYS PUT IT THERE)
                   SO CHECK tmp_tag_data:
                */
                String tfld = tuple.get_str (Tuple::Comment);
                if (tfld && tfld[0])
                {
                    /* THERE'S A COMMENT BUT NO ART FILE IN IT (IE. RUMBLE.COM STREAMS HAVE A COMMENT): */
                    const char * tfld_offset = strstr ((const char *) tfld, ";file://");
                    if (tfld_offset)
                    {
                        tfld_offset += 1;
                        if (tfld_offset)
                        {
                            area->pb = audgui_pixbuf_request (tfld_offset);
                            if (area->pb)
                                noChannelArt = false;
                        }
                    }
                }
                /* FOR tmp_tag_data (1-OFF) STREAMS W/O CHANNEL-ART, CHECK FOR (CHANNEL) ARTIST'S ICON FILE: */
                if (noChannelArt)
                {
                    String artist_tag = tuple.get_str (Tuple::Artist);
                    if (artist_tag && artist_tag[0])
                    {
                        Index<String> extlist = str_list_to_index ("jpg,png,gif,jpeg", ",");
                        for (auto & ext : extlist)
                        {
                            String channelfn = String (str_concat ({aud_get_path (AudPath::UserDir),
                                    "/albumart/", (const char *) artist_tag, ".", (const char *) ext}));
                            const char * filenamechar = channelfn;
                            struct stat statbuf;
                            if (stat (filenamechar, &statbuf) >= 0)  // ART IMAGE FILE EXISTS:
                            {
                                String coverart_uri = String (filename_to_uri (filenamechar));
                                area->pb = audgui_pixbuf_request ((const char *) coverart_uri);
                                if (area->pb)  // FOUND "CHANNEL" ART FILE NAMED AFTER THE ARTIST:
                                {
                                    noChannelArt = false;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    /* FOR LOCAL FILES W/O CHANNEL ART, LOOK FOR A DIRECTORY "CHANNEL ART" ICON FILE INSTEAD: */
    if (noChannelArt)
    {
        if (filename && ! strncmp (filename, "file://", 7)  // JWT:SOMETIMES NULL ON GTK-STARTUP, SO ALWAYS TEST!
                && aud_get_bool ("albumart", "seek_directory_channel_art"))
        {
            String dir_channel_icon = aud_get_str ("albumart", "directory_channel_art");
            if (dir_channel_icon && dir_channel_icon[0])
            {
                struct stat statbuf;
                StringBuf icon_path = str_concat ({filename_get_parent (uri_to_filename (filename)), "/"});
                StringBuf icon_fid = str_concat ({icon_path, dir_channel_icon});
                const char * filename;
                const char * ext;
                int isub_p;
                uri_parse (icon_fid, & filename, & ext, nullptr, & isub_p);
                if (! ext || ! ext[0])
                {
                    Index<String> extlist = str_list_to_index ("jpg,png,jpeg", ",");
                    for (auto & ext : extlist)
                    {
                        dir_channel_icon = String (str_concat ({icon_fid, ".", (const char *) ext}));
                        struct stat statbuf;
                        if (stat ((const char *) dir_channel_icon, &statbuf) < 0)  // ART IMAGE FILE DOESN'T EXIST:
                            dir_channel_icon = String ("");
                        else
                            break;
                    }
                }
                else
                    dir_channel_icon = String (icon_fid);

                if (dir_channel_icon && dir_channel_icon[0] && stat ((const char *) dir_channel_icon, & statbuf) == 0)
                {
                    dir_icon_art = audgui_pixbuf_request ((const char *) dir_channel_icon);
                    if (dir_icon_art)
                        have_dir_icon_art = true;
                }
            }
        }
        area->pb = (have_dir_icon_art && albumart_plugin_isactive) ? dir_icon_art.ref ()
                : audgui_pixbuf_request_current ();
    }

    if (area->pb)
        audgui_pixbuf_scale_within (area->pb, ICON_SIZE);
    else
    {
        if (have_dir_icon_art && ! albumart_plugin_isactive)
        {
            area->pb = dir_icon_art.ref ();
            if (area->pb)
                audgui_pixbuf_scale_within (area->pb, ICON_SIZE);
        }
        if (! area->pb)
        {
            area->pb = audgui_pixbuf_fallback ();
            if (aud_get_bool ("gtkui", "infoarea_hide_fallback_art"))
            {
                /* JWT:USER CHOOSES NOT TO SEE THE FALLBACK IMAGES, SO FORCE HIDE! */
                area->pb = AudguiPixbuf ();
                area->show_art = false;
                return;
            }
        }
    }
    int infoarea_hide_art_gtk = aud_get_int ("albumart", "_infoarea_hide_art_gtk");  // ALBUMART SAYS: -1=NOT CALLED/INACTIVE, 0=SHOW!, 1,2,3=DEPENDS(hidelevel).
    if (infoarea_hide_art_gtk >= 0)
        aud_set_int ("albumart", "_infoarea_hide_art_gtk_prev", infoarea_hide_art_gtk);  // SAVE IN CASE USER TURNS OFF MINI-FAUXD.

    /* JWT:LOGIC OF NEXT LINE IS IF (DIRECTORY ICON):  HIDE ONLY IF NO ALBUMART(3) B/C THE DIR-ICON
       *IS* THE ALBUM-ART!  OTHERWISE, HIDE ONLY IF ALBUMART ACTIVE AND SAYS IT'S A DUP. (1,2, OR 3).
       (OF COURSE, THERE MUST BE NO CHANNEL ART, AND ALBUMART PLUGIN MUST BE ACTIVE, AND USER WANTS
       DUPLICATE IMAGES HIDDEN, AND EITHER INFOBAR OR DOCKED MINI-FAUXDACIOUS (SINGLE WINDOW)!)
       (DEFAULT IS SHOW, UNLESS DETERMINED OTHERWISE BELOW):
    */
    int hidelevel = have_dir_icon_art ? 2 : 0;

    /* JWT:THIS CHECK NEEDED ONLY IF ALBUM-ART PLGN ACTIVE && HIDE-DUPS ON && ALBUM-ART SAYS IT'S A DUP!: */
    if (noChannelArt
            && infoarea_hide_art_gtk > hidelevel
            && albumart_plugin_isactive
            && aud_get_bool ("albumart", "hide_dup_art_icon"))
    {
        bool m_art_hide_dups = true;  // IF HERE, DEFAULT IS HIDE UNLESS DETERMINED OTHERWISE BELOW:
        if (area->widget)  // MINI-FAUXDACIOUS PLUGIN IS RUNNING, CHECK IF DOCKED:
        {
            GtkWidget * parent_window = gtk_widget_get_parent (area->widget);
            if (parent_window)
            {
                GtkWidget * grandparent_window = gtk_widget_get_parent (parent_window);
                if (grandparent_window && gtk_widget_is_toplevel (grandparent_window))
                    m_art_hide_dups = false;  // MINI-FAUXDACIOUS IS UNDOCKED(FLOATING), SO SHOW.
            }
            else  // NO PARENT WINDOW (YET?! - MINI-FAUXDACIOUS JUST LAUNCHED):
                m_art_hide_dups = false;  // WE CAN'T TELL DOCK-STATUS, SO TREAT AS UNDOCKED.
        }

        if (m_art_hide_dups)
        {
            area->pb = AudguiPixbuf ();
            area->show_art = false;  // HIDE INFOBAR ART (WE'RE A DUP. IMAGE & MINI-FAUXD DOCKED OR OFF)
            return;
        }
    }
}

static void infoarea_next ()
{
    g_return_if_fail (area);

    area->last_title = std::move (area->title);
    area->last_artist = std::move (area->artist);
    area->last_album = std::move (area->album);
    area->last_pb = std::move (area->pb);

    area->last_alpha = area->alpha;
    area->alpha = 0;

    gtk_widget_queue_draw (area->main);
}

/* CALLED WHEN PLAYBACK STARTS: */
/* JWT:NOTE:  REASON FOR THE ADDED HAIRY LOGIC IS DUE TO THE FACT THAT WHEN ALBUMART PLUGIN
   RUNNING, BOTH THIS AND ui_infoarea_hooked () GET CALLED, BUT THE CALLING ORDER IS NOT RELIABLE!
   (START WITH INFOBAR, TOGGLE TO MINI-FAUXD & BACK CHANGES THE CALLING ORDER FOR SURE!)
*/
static void ui_infoarea_playback_start ()
{
    g_return_if_fail (area);

    /* -1 MEANS ui_infoarea_hooked() HASN'T (YET) BEEN CALLED FOR THIS ENTRY DURING PLAY: */
    aud_set_int ("albumart", "_infoarea_hide_art_gtk", -1);
    if (! area->stopped) /* moved to the next song without stopping? */
        infoarea_next ();

    area->stopped = false;
    if (! aud_get_bool ("albumart", "_isactive"))
        area->show_art = aud_get_bool ("gtkui", "infoarea_show_art");

    ui_infoarea_set_title ();
    set_album_art ();
    gtk_widget_queue_draw (area->main);

    timer_add (TimerRate::Hz30, ui_infoarea_do_fade);
}

static void ui_infoarea_playback_stop ()
{
    g_return_if_fail (area);

    infoarea_next ();
    area->stopped = true;

    timer_add (TimerRate::Hz30, ui_infoarea_do_fade);
}

static void realize_cb (GtkWidget * widget)
{
    /* using a native window avoids redrawing parent widgets */
    gdk_window_ensure_native (gtk_widget_get_window (widget));
}

/* CALLED BY STARTING/STOPPING THE INFOBAR OR MINI-FAUXDACIOUS: */
EXPORT void ui_infoarea_show_art (bool show)
{
    if (! area)
        return;

    /* JWT:NOTE: TOGGLED BY MENU, BUT MUST CHECK OTHER SHOW/HIDE CONDITIONS IN set_album_art()!: */
    area->show_art = show;
    int infoarea_hide_art_gtk_prev = aud_get_int ("albumart", "_infoarea_hide_art_gtk_prev");
    if (infoarea_hide_art_gtk_prev >= 0)
        aud_set_int ("albumart", "_infoarea_hide_art_gtk", infoarea_hide_art_gtk_prev);

    set_album_art ();  // MAY FORCE HIDE!
    gtk_widget_queue_draw (area->main);
}

/* JWT:CALLED BY HOOKS IN AlbumArt PLUGIN, INCL. WHEN USER TOGGLES IT'S "Hide info bar art icon un..." CHECKBOX: */
EXPORT void ui_infoarea_hooked ()
{
    if (! area)
        return;

    area->show_art = aud_get_bool ("gtkui", "infoarea_show_art");
    set_album_art ();
    gtk_widget_queue_draw (area->main);
}

/* JWT:CATCH CHANGES IN DOCKED-STATUS: */
EXPORT void ui_infoarea_chg_dock_status (PluginHandle * plugin, void *)
{
    if (! strstr (aud_plugin_get_basename (plugin), "info-bar-plugin-gtk"))
        return;  // CATCHES ANY PLUGIN'S DOCK-STATUS CHANGE, IGNORE ALL BUT THIS ONE!

    int infoarea_hide_art_gtk_prev = aud_get_int ("albumart", "_infoarea_hide_art_gtk_prev");
    if (infoarea_hide_art_gtk_prev >= 0)
        aud_set_int ("albumart", "_infoarea_hide_art_gtk", infoarea_hide_art_gtk_prev);

    if (! area)
        return;

    area->show_art = aud_get_bool ("gtkui", "infoarea_show_art");
    set_album_art ();
    gtk_widget_queue_draw (area->main);
}

EXPORT void ui_infoarea_show_vis (bool show)
{
    if (! area)
        return;

    if (show)
    {
        if (vis.widget)
            return;

        vis.widget = gtk_drawing_area_new ();

        /* note: "realize" signal must be connected before adding to box */
        g_signal_connect (vis.widget, "realize", (GCallback) realize_cb, nullptr);

        gtk_widget_set_size_request (vis.widget, VIS_WIDTH, HEIGHT);
        gtk_box_pack_start ((GtkBox *) area->box, vis.widget, false, false, 0);

        g_signal_connect (vis.widget, AUDGUI_DRAW_SIGNAL, (GCallback) draw_vis_cb, nullptr);
        gtk_widget_show (vis.widget);

        aud_visualizer_add (& vis);
    }
    else
    {
        if (! vis.widget)
            return;

        aud_visualizer_remove (& vis);

        gtk_widget_destroy (vis.widget);
        vis.widget = nullptr;

        vis.clear ();
    }
}

static void destroy_cb (GtkWidget * widget)
{
    g_return_if_fail (area);

    ui_infoarea_show_vis (false);

    hook_dissociate ("plugin dock status changed", (HookFunction) ui_infoarea_chg_dock_status, nullptr);
    hook_dissociate ("gtkui toggle infoarea_art", (HookFunction) ui_infoarea_hooked, nullptr);
    hook_dissociate ("playback stop", (HookFunction) ui_infoarea_playback_stop);
    hook_dissociate ("playback ready", (HookFunction) ui_infoarea_playback_start);
    hook_dissociate ("tuple change", (HookFunction) ui_infoarea_set_title);

    timer_remove (TimerRate::Hz30, ui_infoarea_do_fade);

    delete area;
    area = nullptr;
}

EXPORT GtkWidget * ui_infoarea_new ()
{
    compute_sizes ();

    area = new UIInfoArea ();
    area->widget = nullptr;
    area->box = audgui_hbox_new (0);

    area->main = gtk_drawing_area_new ();
    gtk_widget_set_size_request (area->main, HEIGHT, HEIGHT);
    gtk_box_pack_start ((GtkBox *) area->box, area->main, true, true, 0);

    g_signal_connect (area->main, AUDGUI_DRAW_SIGNAL, (GCallback) draw_cb, nullptr);

    hook_associate ("tuple change", (HookFunction) ui_infoarea_set_title, nullptr);
    hook_associate ("playback ready", (HookFunction) ui_infoarea_playback_start, nullptr);
    hook_associate ("playback stop", (HookFunction) ui_infoarea_playback_stop, nullptr);
    hook_associate ("gtkui toggle infoarea_art", (HookFunction) ui_infoarea_hooked, nullptr);
    hook_associate ("plugin dock status changed", (HookFunction) ui_infoarea_chg_dock_status, nullptr);

    g_signal_connect (area->box, "destroy", (GCallback) destroy_cb, nullptr);

    if (aud_drct_get_ready ())
    {
        ui_infoarea_set_title ();
        set_album_art ();

        /* skip fade-in */
        area->alpha = 1;
    }

    GtkWidget * frame = gtk_frame_new (nullptr);
    gtk_frame_set_shadow_type ((GtkFrame *) frame, GTK_SHADOW_IN);
    gtk_container_add ((GtkContainer *) frame, area->box);
    area->widget = frame;

    return frame;
}
