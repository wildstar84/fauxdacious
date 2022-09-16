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
#include <libfauxdgui/libfauxdgui-gtk.h>
#include <libfauxdcore/audstrings.h>

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
    auto & c = (gtk_widget_get_style (widget))->base[GTK_STATE_NORMAL];
    int v = aud::max (aud::max (c.red, c.green), c.blue);

    if (v >= 10*256 && v < 80*256)
    {
        r = (double)c.red / v;
        g = (double)c.green / v;
        b = (double)c.blue / v;
    }

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
    auto & c = (gtk_widget_get_style (widget))->base[GTK_STATE_SELECTED];
    float h, s, v;

    rgb_to_hsv (c.red / 65535.0, c.green / 65535.0, c.blue / 65535.0, & h, & s, & v);

    if (s < 0.1) /* monochrome theme? use blue instead */
        h = 4.6;

    s = 1 - 0.9 * i / (VIS_BANDS - 1);
    v = 0.75 + 0.25 * i / (VIS_BANDS - 1);

    hsv_to_rgb (h, s, v, r, g, b);
}

static int expose_vis_cb (GtkWidget * widget, GdkEventExpose * event)
{
    cairo_t * cr = gdk_cairo_create (gtk_widget_get_window (widget));

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

    cairo_destroy (cr);
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

static int expose_cb (GtkWidget * widget, GdkEventExpose * event)
{
    cairo_t * cr = gdk_cairo_create (gtk_widget_get_window (widget));

    clear (widget, cr);

    draw_album_art (cr);
    draw_title (cr);

    cairo_destroy (cr);
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

    if (! area->show_art)
    {
        area->pb = AudguiPixbuf ();
        return;
    }

    bool noAltArt = true;
    bool have_dir_icon_art = false;
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
            const char * tfld_offset = strstr ((const char *) tfld, ";file://");
            if (tfld_offset)
            {
                /* FOUND CHANNEL ICON IN COMMENT FIELD: */
                tfld_offset += 1;
                if (tfld_offset)
                {
                    area->pb = audgui_pixbuf_request (tfld_offset);
                    if (area->pb)
                        noAltArt = false;
                }
            }
            else  /* NO CHANNEL ICON IN COMMENT (OVERWRITTEN BY STREAM METATAGS?), SO CHECK tmp_tag_data: */
            {     /* (THIS ONLY APPLIES TO STREAMING VIDEOS/PODCASTS, WHICH ALWAYS PUT IT THERE) */
                if (filename && aud_read_tag_from_tagfile (filename, "tmp_tag_data", tuple))
                {
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
                                    noAltArt = false;
                            }
                        }
                    }
                }
            }
        }
    }

    if (filename && ! strncmp (filename, "file://", 7)  // JWT:SOMETIMES NULL ON GTK-STARTUP, SO ALWAYS TEST!
            && aud_get_bool ("albumart", "seek_directory_channel_art"))
    {
        /* FOR LOCAL FILES W/O CHANNEL ART, LOOK FOR A DIRECTORY CHANNEL ART ICON FILE: */
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
    aud_set_bool ("albumart", "_have_channel_art", ! (noAltArt && ! have_dir_icon_art)); // TELL AlbumArt PLUGIN.
    if (noAltArt)
        area->pb = (have_dir_icon_art && albumart_plugin_isactive) ? dir_icon_art.ref ()
                : audgui_pixbuf_request_current ();

    if (area->pb)
    {
        audgui_pixbuf_scale_within (area->pb, ICON_SIZE);
        aud_set_bool ("albumart", "_last_art_was_fallback", false);
    }
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
            aud_set_bool ("albumart", "_last_art_was_fallback", true);
            /* JWT:USER DOESN'T WANT TO SEE THE FALLBACK IMAGE! */
            if (aud_get_bool ("gtkui", "infoarea_hide_fallback_art"))
            {
                area->pb = AudguiPixbuf ();
                area->show_art = false;
                return;
            }
        }
    }
    /* JWT:NOW CHECK FOR "m_art_force_dups" (SEE Qt SIDE): */
    bool m_art_force_dups = true;  /* SET TO HIDE TO FORCE HIDING ALBUM-ART ICON. */
    /* JWT:THIS CHECK NEEDED ONLY IF ALBUM-ART PLGN ACTIVE && HIDE-DUPS ON && ALBUM-ART SAYS IT'S A DUP!: */
    if (noAltArt && albumart_plugin_isactive && aud_get_bool ("albumart", "hide_dup_art_icon")
            && aud_get_bool ("albumart", "_infoarea_hide_art"))
    {
        m_art_force_dups = false;
        if (area->widget)
        {
            GtkWidget * parent_window = gtk_widget_get_parent (area->widget);
            if (parent_window)
            {
                GtkWidget * grandparent_window = gtk_widget_get_parent (parent_window);
                if (grandparent_window)
                {
                    if (gtk_widget_is_toplevel (grandparent_window))
                        m_art_force_dups = true;  // MINI-FAUXDACIOUS IS UNDOCKED.
                }
            }
            else  // NO PARENT WINDOW (YET?! - MINI-FAUXDACIOUS JUST LAUNCHED):
                m_art_force_dups = true;  // WE CAN'T TELL DOCK-STATUS, SO TREAT AS UNDOCKED.
        }

        if (! m_art_force_dups)
        {
            area->pb = AudguiPixbuf ();
            area->show_art = false;
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

static void ui_infoarea_playback_start ()
{
    g_return_if_fail (area);

    if (! area->stopped) /* moved to the next song without stopping? */
        infoarea_next ();

    area->stopped = false;
    area->show_art = aud_get_bool ("gtkui", "infoarea_show_art");

    ui_infoarea_set_title ();
    set_album_art ();

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

EXPORT void ui_infoarea_show_art (bool show)
{
    if (! area)
        return;

    /* JWT:NOTE: TOGGLED BY MENU, BUT MUST AND W/OTHER SHOW/HIDE CONDITIONS (IN set_album_art)!: */
    area->show_art = show;
    set_album_art ();
    gtk_widget_queue_draw (area->main);
}

EXPORT void ui_infoarea_toggle_art ()
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

    ui_infoarea_toggle_art ();
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

        g_signal_connect (vis.widget, "expose-event", (GCallback) expose_vis_cb, nullptr);
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
    hook_dissociate ("gtkui toggle infoarea_art", (HookFunction) ui_infoarea_toggle_art, nullptr);
    hook_dissociate ("playback stop", (HookFunction) ui_infoarea_playback_stop);
    hook_dissociate ("playback ready", (HookFunction) ui_infoarea_playback_start);
    hook_dissociate ("tuple change", (HookFunction) ui_infoarea_set_title);

    timer_remove (TimerRate::Hz30, ui_infoarea_do_fade);

    delete area;
    area = nullptr;
}

EXPORT GtkWidget * ui_infoarea_new ()
{
    g_return_val_if_fail (! area, nullptr);

    compute_sizes ();

    area = new UIInfoArea ();
    area->widget = nullptr;
    area->box = gtk_hbox_new (false, 0);

    area->main = gtk_drawing_area_new ();
    gtk_widget_set_size_request (area->main, HEIGHT, HEIGHT);
    gtk_box_pack_start ((GtkBox *) area->box, area->main, true, true, 0);

    g_signal_connect (area->main, "expose-event", (GCallback) expose_cb, nullptr);

    hook_associate ("tuple change", (HookFunction) ui_infoarea_set_title, nullptr);
    hook_associate ("playback ready", (HookFunction) ui_infoarea_playback_start, nullptr);
    hook_associate ("playback stop", (HookFunction) ui_infoarea_playback_stop, nullptr);
    hook_associate ("gtkui toggle infoarea_art", (HookFunction) ui_infoarea_toggle_art, nullptr);
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
