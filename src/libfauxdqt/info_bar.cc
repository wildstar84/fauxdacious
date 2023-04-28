/*
 * info_bar.cc
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

#include <sys/stat.h>
#include <cmath>

#include "info_bar.h"
// #include "settings.h"

#include "libfauxdqt-internal.h"
#include "libfauxdqt.h"

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPoint>

#include <libfauxdcore/drct.h>
#include <libfauxdcore/interface.h>
#include <libfauxdcore/plugins.h>
#include <libfauxdcore/plugin.h>
#include <libfauxdcore/runtime.h>
#include <libfauxdcore/probe.h>
#include <libfauxdcore/audstrings.h>

namespace audqt {

static constexpr int FadeSteps = 10;

static constexpr int VisBands = 12;
static constexpr int VisDelay = 2; /* delay before falloff in frames */
static constexpr int VisFalloff = 2; /* falloff in decibels per frame */

struct BarColors
{
    QColor main, shadow;
};

struct My_PixelSizes
{
    int Spacing, IconSize, Height, BandWidth, BandSpacing, VisWidth, VisScale, VisCenter;

    My_PixelSizes (int dpi) :
        Spacing (aud::rescale (dpi, 12, 1)),
        IconSize (2 * aud::rescale (dpi, 3, 1)), // should be divisible by 2
        Height (IconSize + 2 * Spacing),
        BandWidth (aud::rescale (dpi, 16, 1)),
        BandSpacing (aud::rescale (dpi, 48, 1)),
        VisWidth (VisBands * (BandWidth + BandSpacing) - BandSpacing + 2 * Spacing),
        VisScale (aud::rescale (IconSize, 8, 5)),
        VisCenter (VisScale + Spacing) {}
};

class InfoVis : public QWidget, Visualizer
{
public:
    InfoVis (QWidget * parent = nullptr);
    ~InfoVis ();

    void enable (bool enabled);
    void set_parent (QWidget * parent);

    const QGradient & gradient () const
        { return m_gradient; }
    const My_PixelSizes & pixelSizes () const
        { return ps; }

protected:
    void render_freq (const float * freq);
    void clear ();
    void changeEvent (QEvent * event);
    void paintEvent (QPaintEvent *);

private:
    bool infovis_active;  /* JWT:TRUE IF "BOUNCY-BAR" VIS SUBWIDGET IS ACTIVE (ACCEPTING INPUTS). */
    void update_colors ();

    const My_PixelSizes ps;
    QWidget * is_the_infobar;  /* JWT:DIFFERENTIATE INFOBAR FROM MINI-FAUXD (SET IF INFOBAR, NULL OTHERWISE) */
    QLinearGradient m_gradient;
    BarColors m_bar_colors[VisBands];

    float m_bars[VisBands] {};
    char m_delay[VisBands] {};
};

void InfoVis::update_colors ()
{
    auto & base = palette ().color (QPalette::Window);
    auto & highlight = palette ().color (QPalette::Highlight);

    m_gradient.setStops (audqt::dark_bg_gradient (base));

    for (int i = 0; i < VisBands; i ++)
    {
        m_bar_colors[i].main = audqt::vis_bar_color (highlight, i, VisBands);
        m_bar_colors[i].shadow = m_bar_colors[i].main.darker (333);
    }
}

InfoVis::InfoVis (QWidget * parent) :
    QWidget (parent),
    Visualizer (Freq),
    ps (audqt::sizes.OneInch),
    m_gradient (0, 0, 0, ps.Height)
{
    infovis_active = true;
    update_colors ();
    setAttribute (Qt::WA_OpaquePaintEvent);
    resize (ps.VisWidth + 2 * ps.Spacing, ps.Height);
}

InfoVis::~InfoVis ()
{
    enable (false);
    if (! is_the_infobar)
        infovis_active = false;
}

void InfoVis::set_parent (QWidget * parent)
{
    is_the_infobar = parent;  /* JWT: *InfoBar's* parent if INFOBAR, NULL if MINI-FAUXDACIOUS! */
}

void InfoVis::render_freq (const float * freq)
{
    if (! infovis_active || ! isVisible ())
        return;

    /* xscale[i] = pow (256, i / VIS_BANDS) - 0.5; */
    const float xscale[VisBands + 1] = {0.5, 1.09, 2.02, 3.5, 5.85, 9.58,
     15.5, 24.9, 39.82, 63.5, 101.09, 160.77, 255.5};

    for (int i = 0; i < VisBands; i ++)
    {
        /* 40 dB range */
        float x = 40 + compute_freq_band (freq, xscale, i, VisBands);

        m_bars[i] -= aud::max (0, VisFalloff - m_delay[i]);

        if (m_delay[i])
            m_delay[i] --;

        if (x > m_bars[i])
        {
            m_bars[i] = x;
            m_delay[i] = VisDelay;
        }
    }

    repaint ();
}

void InfoVis::clear ()
{
    memset (m_bars, 0, sizeof m_bars);
    memset (m_delay, 0, sizeof m_delay);

    if (infovis_active)
        update ();
}

void InfoVis::changeEvent (QEvent * event)
{
    if (! infovis_active || ! event)
        return;

    if (event->type () == QEvent::PaletteChange)
        update_colors ();

    QWidget::changeEvent (event);
}

void InfoVis::paintEvent (QPaintEvent *)
{
    if (! infovis_active)
        return;

    QPainter p (this);
    p.fillRect (0, 0, ps.VisWidth, ps.Height, m_gradient);

    for (int i = 0; i < VisBands; i ++)
    {
        int x = ps.Spacing + i * (ps.BandWidth + ps.BandSpacing);
        int v = aud::clamp ((int) (m_bars[i] * ps.VisScale / 40), 0, ps.VisScale);
        int m = aud::min (ps.VisCenter + v, ps.Height);

        p.fillRect (x, ps.VisCenter - v, ps.BandWidth, v, m_bar_colors[i].main);
        p.fillRect (x, ps.VisCenter, ps.BandWidth, m - ps.VisCenter, m_bar_colors[i].shadow);
    }
}

void InfoVis::enable (bool enabled)
{
    if (enabled)
        aud_visualizer_add (this);
    else
    {
        clear ();
        aud_visualizer_remove (this);
    }

    setVisible (enabled);
}

EXPORT InfoBar::InfoBar (QWidget * parent) :
    QWidget (parent),
    m_vis (new InfoVis (this)),
    ps (m_vis->pixelSizes ()),
    m_stopped (true)
{
    m_parent = parent;
    m_dockwidget = nullptr;
    infobar_active = true;
    update_vis ();
    setFixedHeight (ps.Height);

    m_art_enabled = aud_get_bool ("qtui", "infoarea_show_art");
    m_art_force_dups = true;  // JWT:FORCE ART ON FLOATING MINI-FAUXD IF "HIDE-DUPS" IN EFFECT BUT OTHERWISE SHOW!
    m_fbart_hidden = aud_get_bool ("qtui", "infoarea_hide_fallback_art");

    for (SongData & d : sd)
    {
        d.title.setTextFormat (Qt::PlainText);
        d.artist.setTextFormat (Qt::PlainText);
        d.album.setTextFormat (Qt::PlainText);
        d.alpha = 0;
    }

    if (aud_drct_get_ready ())
    {
        m_stopped = false;
        update_title ();
        update_album_art ();

        /* skip fade-in */
        sd[Cur].alpha = FadeSteps;
    }
    /* JWT:NEEDED FOR KEYPRESS EVENTS TO BE SEEN!: */
    this->setFocusPolicy (Qt::StrongFocus);
}

EXPORT InfoBar::~InfoBar ()
{
    if (! m_parent)
        infobar_active = false;
}

EXPORT void InfoBar::resizeEvent (QResizeEvent *)
{
    if (! infobar_active)
        return;

    reellipsize_title ();
    m_vis->move (width () - ps.VisWidth, 0);
}

EXPORT void InfoBar::paintEvent (QPaintEvent *)
{
    if (! infobar_active)
        return;

    QPainter p (this);

    m_art_force_dups = true;

    /* JWT:NOTE:  m_parent MEANS WE'RE THE INFO_BAR, *NOT* MINI-FAUXDACIOUS! */

    /* JWT:THIS CHECK NEEDED ONLY IF ALBUM-ART PLGN ACTIVE && HIDE-DUPS ON && ALBUM-ART SAYS IT'S A DUP!: */
    if (aud_get_bool ("albumart", "_isactive") && aud_get_bool ("albumart", "hide_dup_art_icon")
            && aud_get_bool ("albumart", "_infoarea_hide_art"))
    {
        m_art_force_dups = false;
        /* CHECK IF WE'RE MINI-FAUXDACIOUS, AND IF SO, WE ONLY HAVE TO FETCH THE DOCK-WIDGET 1ST TIME: */
        if (! m_parent)
        {
            if (! m_dockwidget)  // 1ST TIME SINCE ACTIVATED - NEED IT'S DOCK WIDGET TO SEE IF FLOATING:
            {
                PluginHandle * mini_fauxd = aud_plugin_lookup_basename ("info-bar-plugin-qt");
                if (mini_fauxd && aud_plugin_get_enabled (mini_fauxd))
                {
                    auto * item = audqt::DockItem::find_by_plugin (mini_fauxd);
                    if (item)
                        m_dockwidget = (QDockWidget *) item->host_data ();  // GOT IT!
                    else
                    {
                        m_dockwidget = nullptr;   // DIDN'T GET IT (NOT ALWAYS IMMEDATELY AVAILABLE)!:
                        m_art_force_dups = true;  // SO WE CAN'T TELL DOCK-STATUS, SO TREAT AS UNDOCKED.
                    }
                }
            }
            /* JWT:FORCE ENABLED ON UNDOCKED (FLOATING) MINI-FAUXDACIOUS AND ONLY HIDDEN B/C OF DUP. ART!: */
            if (m_dockwidget && m_dockwidget->isFloating ())
                m_art_force_dups = true;
        }
    }
    bool show_art = (m_art_enabled && m_art_force_dups
            && (! m_fbart_hidden || ! aud_get_bool ("albumart", "_last_art_was_fallback")));
    int viswidth = m_vis->isVisible () ? ps.VisWidth : 0;
    int offset = show_art ? ps.Height : ps.Spacing;

    if (! infobar_active)
        return;

    p.fillRect(0, 0, width () - viswidth, ps.Height, m_vis->gradient ());

    for (SongData & d : sd)
    {
        p.setOpacity ((qreal) d.alpha / FadeSteps);

        if (show_art && ! d.art.isNull ())
        {
            auto sz = d.art.size () / d.art.devicePixelRatio ();
            int left = ps.Spacing + (ps.IconSize - sz.width ()) / 2;
            int top = ps.Spacing + (ps.IconSize - sz.height ()) / 2;
            p.drawPixmap (left, top, d.art);
        }

        QFont font = p.font ();
        font.setPointSize (18);
        p.setFont (font);

        if (d.title.text ().isNull () && ! d.orig_title.isNull ())
        {
            QFontMetrics metrics = p.fontMetrics ();
            d.title = QStaticText (metrics.elidedText (d.orig_title, Qt::ElideRight,
                    width () - viswidth - offset - ps.Spacing));
        }

        p.setPen (QColor (255, 255, 255));
        p.drawStaticText (offset, ps.Spacing, d.title);

        font.setPointSize (9);
        p.setFont (font);

        p.drawStaticText (offset, ps.Spacing + ps.IconSize / 2, d.artist);

        p.setPen (QColor (179, 179, 179));
        p.drawStaticText (offset, ps.Spacing + ps.IconSize * 3 / 4, d.album);
    }
}

EXPORT void InfoBar::update_title ()
{
    if (! infobar_active)
        return;

    Tuple tuple = aud_drct_get_tuple ();

    sd[Cur].title.setText (QString ());
    sd[Cur].orig_title = String (str_get_one_line (tuple.get_str (Tuple::Title), true));
    sd[Cur].artist.setText ((const char *) str_get_one_line (tuple.get_str (Tuple::Artist), true));
    sd[Cur].album.setText ((const char *) tuple.get_str (Tuple::Album));

    update ();
}

EXPORT void InfoBar::update_album_art ()
{
    if (! infobar_active)
        return;

    bool noChannelArt = true;
    bool have_dir_icon_art = false;  /* JWT:BECOMES TRUE IF ENTRY IS FILE AND A DIRECTORY-ICON IS FOUND. */
    bool albumart_plugin_isactive = aud_get_bool ("albumart", "_isactive");
    QPixmap dir_icon_art;
    const char * filename = aud_drct_get_filename ();

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
                    sd[Cur].art = audqt::art_request (tfld_offset, ps.IconSize, ps.IconSize);
                    if (! sd[Cur].art.isNull ())
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
                            sd[Cur].art = audqt::art_request (tfld_offset, ps.IconSize, ps.IconSize);
                            if (! sd[Cur].art.isNull ())
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
                                sd[Cur].art = audqt::art_request ((const char *) coverart_uri,
                                        ps.IconSize, ps.IconSize);
                                if (! sd[Cur].art.isNull ())
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
        if (filename && ! strncmp (filename, "file://", 7)  // JWT:SOMETIMES NULL ON GTK-STARTUP, BUT TEST HERE TOO!
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
                    dir_icon_art = audqt::art_request ((const char *) dir_channel_icon, ps.IconSize, ps.IconSize);
                    if (! dir_icon_art.isNull ())
                        have_dir_icon_art = true;
                }
            }
        }
        sd[Cur].art = (have_dir_icon_art && albumart_plugin_isactive) ? dir_icon_art
                : audqt::art_request_current (ps.IconSize, ps.IconSize);
    }
    aud_set_bool ("albumart", "_have_channel_art", ! (noChannelArt && ! have_dir_icon_art)); // TELL AlbumArt PLUGIN.
    if (sd[Cur].art.isNull () && have_dir_icon_art && ! albumart_plugin_isactive)
        sd[Cur].art = dir_icon_art;

    if (sd[Cur].art.isNull ())
    {
        sd[Cur].art = audqt::art_request_fallback (ps.IconSize, ps.IconSize);
        aud_set_bool ("albumart", "_last_art_was_fallback", true);
    }
    else
        aud_set_bool ("albumart", "_last_art_was_fallback", false);
}

EXPORT void InfoBar::next_song ()
{
    sd[Prev] = std::move (sd[Cur]);
    sd[Cur].alpha = 0;
}

EXPORT void InfoBar::do_fade ()
{
    bool done = true;

    if (aud_drct_get_playing () && sd[Cur].alpha < FadeSteps)
    {
        sd[Cur].alpha ++;
        done = false;
    }

    if (sd[Prev].alpha > 0)
    {
        sd[Prev].alpha --;
        done = false;
    }

    if (infobar_active)
        update ();

    if (done)
        fade_timer.stop ();
}

EXPORT void InfoBar::playback_ready_cb ()
{
    if (! m_stopped)
        next_song ();

    m_stopped = false;

    if (! infobar_active)
        return;

    update_title ();
    update_album_art ();

    update ();
    fade_timer.start ();
}

EXPORT void InfoBar::playback_stop_cb ()
{
    next_song ();
    m_stopped = true;

    if (! infobar_active)
        return;

    update ();
    fade_timer.start ();
}

EXPORT void InfoBar::reellipsize_title ()
{
    // re-ellipsize text on next paintEvent
    for (SongData & d : sd)
        d.title.setText (QString ());
}

EXPORT void InfoBar::update_vis ()
{
    if (! infobar_active)
        return;

    m_vis->set_parent (m_parent);
    reellipsize_title ();

    m_vis->enable (aud_get_bool ("qtui", "infoarea_show_vis"));
    update ();
}

EXPORT void InfoBar::update_art ()
{
    reellipsize_title ();
    m_art_enabled = aud_get_bool ("qtui", "infoarea_show_art");
    m_fbart_hidden = aud_get_bool ("qtui", "infoarea_hide_fallback_art");

    if (! infobar_active)
        return;

    update ();
}

void InfoBar::keyPressEvent (QKeyEvent * event)
{
    if (! event)
        return;

    auto CtrlShiftAlt = Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier;

    if (! (event->modifiers () & CtrlShiftAlt))
    {
        switch (event->key ())
        {
          case Qt::Key_Right:
            aud_drct_seek (aud_drct_get_time () + aud_get_int (0, "step_size") * 1000);
            break;
          case Qt::Key_Left:
            aud_drct_seek (aud_drct_get_time () - aud_get_int (0, "step_size") * 1000);
            break;
          case Qt::Key_Up:
            aud_drct_set_volume_main (aud_drct_get_volume_main () + aud_get_int (0, "volume_delta"));
            break;
          case Qt::Key_Down:
            aud_drct_set_volume_main (aud_drct_get_volume_main () - aud_get_int (0, "volume_delta"));
            break;
          case Qt::Key_Space:
            aud_drct_play_pause ();
            break;
          case Qt::Key_Z:
            aud_drct_pl_prev ();
            break;
          case Qt::Key_X:
            aud_drct_play ();
            break;
          case Qt::Key_C:
            aud_drct_pause ();
            break;
          case Qt::Key_V:
            aud_drct_stop ();
            break;
          case Qt::Key_B:
            aud_drct_pl_next ();
            break;
        }
    }
    else if (m_parent == nullptr && (event->modifiers () & Qt::AltModifier))
    {
        /* NOTE:  Mini-Fauxdacious ONLY (AVOID CONFLICT W/MAIN-WINDOW BINDINGS)!: */
        PluginHandle * plugin;

        switch (event->key ())
        {
          case Qt::Key_A:
            plugin = aud_plugin_lookup_basename ("albumart-qt");
            if (plugin)
                aud_plugin_enable (plugin, (! aud_plugin_get_enabled (plugin)));
            break;
          case Qt::Key_B:
            plugin = aud_plugin_lookup_basename ("blur_scope-qt");
            if (plugin)
                aud_plugin_enable (plugin, (! aud_plugin_get_enabled (plugin)));
            break;
          case Qt::Key_L:
            plugin = aud_plugin_lookup_basename ("lyricwiki-qt");
            if (plugin)
                aud_plugin_enable (plugin, (! aud_plugin_get_enabled (plugin)));
            break;
          case Qt::Key_M:
            plugin = aud_plugin_lookup_basename ("info-bar-plugin-qt");
            if (plugin)
                aud_plugin_enable (plugin, (! aud_plugin_get_enabled (plugin)));
            break;
          case Qt::Key_O:
            plugin = aud_plugin_lookup_basename ("gl-spectrum-qt");
            if (plugin)
                aud_plugin_enable (plugin, (! aud_plugin_get_enabled (plugin)));
            break;
          case Qt::Key_P:
            plugin = aud_plugin_lookup_basename ("playlist-manager-qt");
            if (plugin)
                aud_plugin_enable (plugin, (! aud_plugin_get_enabled (plugin)));
            break;
//DOESN'T WORK, BUT Ctrl-Q DOES!:          case Qt::Key_Q:
//            aud_quit ();
//            break;
          case Qt::Key_S:
            plugin = aud_plugin_lookup_basename ("search-tool-qt");
            if (plugin)
                aud_plugin_enable (plugin, (! aud_plugin_get_enabled (plugin)));
            break;
          case Qt::Key_U:
            plugin = aud_plugin_lookup_basename ("vumeter-qt");
            if (plugin)
                aud_plugin_enable (plugin, (! aud_plugin_get_enabled (plugin)));
            break;
          case Qt::Key_V:
            plugin = aud_plugin_lookup_basename ("video_display");
            if (plugin)
                aud_plugin_enable (plugin, (! aud_plugin_get_enabled (plugin)));
            break;
        }
    }
    return;
}

void InfoBar::mouseDoubleClickEvent (QMouseEvent * e)
{
    if (m_parent == nullptr || ! e)
        return;

    PluginHandle * infobarHandle = aud_plugin_lookup_basename ("info-bar-plugin-qt");
    if (infobarHandle && ! aud_plugin_get_enabled (infobarHandle))
        aud_plugin_enable (infobarHandle, true);

    e->accept ();
}

} // namespace audqt
