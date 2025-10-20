/*
 * infowin.cc
 * Copyright 2006-2022 Ariadne Conill, Tomasz Moń, Eugene Zagidullin,
 *                     John Lindgren, and Thomas Lange
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

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPixmap>
#include <QPainter>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include <libfauxdcore/audstrings.h>
#include <libfauxdcore/hook.h>
#include <libfauxdcore/i18n.h>
#include <libfauxdcore/interface.h>
#include <libfauxdcore/playlist.h>
#include <libfauxdcore/probe.h>
#include <libfauxdcore/runtime.h>
#include <libfauxdcore/drct.h>
#include <libfauxdcore/vfs.h>

#include "info-widget.h"
#include "libfauxdqt.h"
#include "libfauxdqt-internal.h"

namespace audqt {

/* This class remedies some of the deficiencies of QLabel (such as lack
 * of proper wrapping).  It can be expanded and/or made more visible if
 * it turns out to be useful outside InfoWindow. */
class InfoWindow : public QDialog
{
public:
    InfoWindow (QWidget * parent = nullptr);

    void fillInfo (int playlist, int entry, const char * filename, const Tuple & tuple,
     PluginHandle * decoder, bool updating_enabled);
    void displayImage (const char * filename);

protected:
    InfoWidget m_infowidget;

private:
    String m_filename;
    QLabel m_image;
    QTextEdit m_uri_label;
    QDialogButtonBox * bbox;
    QPushButton * ArtLookupButton;
    QPushButton * Save2TagfileButton = nullptr;
    QPushButton * EmbedButton;

    const HookReceiver<InfoWindow, const char *>
     art_hook {"art ready", this, & InfoWindow::displayImage};
};

static bool force_image;
static void displayTupleImage (void * image_fn, void * hookarg);

InfoWindow::InfoWindow (QWidget * parent) : QDialog (parent)
{
    bool user_tag_data = aud_get_bool (nullptr, "user_tag_data");
    setWindowTitle (_("Song Info - Fauxdacious"));
    setWindowRole ("song-info");
    setContentsMargins (margins.TwoPt);

    m_uri_label.setFixedWidth((int)(2.5 * audqt::sizes.OneInch));
    m_uri_label.setFrameStyle(QFrame::NoFrame);
    m_uri_label.setReadOnly(true);
    m_uri_label.viewport()->setAutoFillBackground(false);

    auto left_vbox = make_vbox (nullptr);
    left_vbox->addWidget(&m_image, 1, Qt::AlignCenter);
    left_vbox->addWidget(&m_uri_label, 1);

    auto hbox = make_hbox (nullptr);
    hbox->addLayout (left_vbox);
    hbox->addWidget (& m_infowidget);

    auto vbox = make_vbox (this);
    vbox->addLayout (hbox);

    ArtLookupButton = new QPushButton (_("Art Lookup"), this);
    ArtLookupButton->setIcon (get_icon ("document-open"));
    if (user_tag_data)
    {
        Save2TagfileButton = new QPushButton (_("Tagfile"), this);
        Save2TagfileButton->setIcon (get_icon ("document-save"));
    }
    EmbedButton = new QPushButton (_("Embed"), this);
    EmbedButton->setIcon (get_icon ("document-save"));
    bbox = new QDialogButtonBox (QDialogButtonBox::Close, this);
    bbox->button (QDialogButtonBox::Close)->setText (translate_str (N_("_Close")));
    bbox->addButton (ArtLookupButton, QDialogButtonBox::ActionRole);
    bbox->addButton (EmbedButton, QDialogButtonBox::ActionRole);
    if (user_tag_data && Save2TagfileButton)
        bbox->addButton (Save2TagfileButton, QDialogButtonBox::ActionRole);
    vbox->addWidget (bbox);

//    m_infowidget.linkEnabled (EmbedButton);
    EmbedButton->setDisabled (true);
    connect (EmbedButton, & QPushButton::clicked, [this] () {
        m_infowidget.updateFile (false);
        deleteLater ();
    });

    /* JWT:ADD AN [Art Lookup] BUTTON TO ALLOW USER TO SELECT ALBUM ART IMAGE FOR Tuple::Comment FIELD: */
    connect (ArtLookupButton, & QPushButton::clicked, [this] () {
        m_infowidget.show_coverart_dialog (this);
    });

    if (user_tag_data && Save2TagfileButton)
    {
        connect (Save2TagfileButton, & QPushButton::clicked, [this] () {
            m_infowidget.updateFile (true);
            deleteLater ();
        });
    }

    connect (bbox, & QDialogButtonBox::rejected, this, & QObject::deleteLater);
}

void InfoWindow::fillInfo (int playlist, int entry, const char * filename, const Tuple & tuple,
 PluginHandle * decoder, bool updating_enabled)
{
    String entryfn = tuple.get_str (Tuple::AudioFile);

    if (! entryfn || ! entryfn[0])
        entryfn = String (filename);

    m_filename = entryfn;

    m_uri_label.setPlainText ((QString) uri_to_display ((const char *) entryfn));
    force_image = false;
    displayImage (entryfn);
    m_infowidget.fillInfo (playlist, entry, (const char *) entryfn, tuple,
            decoder, updating_enabled);
    if (! updating_enabled || aud_get_bool (nullptr, "record"))
    {
        m_infowidget.setEmbedButton (nullptr);
        EmbedButton->setDisabled (true);
    }
    else
        m_infowidget.setEmbedButton (EmbedButton);
}

void InfoWindow::displayImage (const char * filename)
{
    if (force_image || ! strcmp_safe (filename, m_filename))
    {
        int xysize = (int)(2.5 * sizes.OneInch);
        m_image.setPixmap (art_request (filename, xysize, xysize));
        force_image = false;
    }
}

static InfoWindow * s_infowin = nullptr;

/* JWT:IMMEDIATELY DISPLAY ART IMAGE USER SELECTS WITH THE [Art Lookup] BUTTON (SO USER CAN SEE IT): */
static void displayTupleImage (void * image_fn, void * hookarg)
{
    if (s_infowin && image_fn && strstr ((const char *) image_fn, "://"))
    {
        force_image = true;
        s_infowin->displayImage ((const char *) image_fn);
        window_bring_to_front (s_infowin);
    }
}

static void show_infowin (int playlist, int entry, const char * filename,
 const Tuple & tuple, PluginHandle * decoder, bool can_write)
{
    if (! s_infowin)
    {
        s_infowin = new InfoWindow;
        s_infowin->setAttribute (Qt::WA_DeleteOnClose);
        hook_associate ("image change", displayTupleImage, nullptr);

        QObject::connect (s_infowin, & QObject::destroyed, [] () {
            hook_dissociate ("image change", displayTupleImage, nullptr);
            s_infowin = nullptr;
        });
    }

    s_infowin->fillInfo (playlist, entry, filename, tuple, decoder, can_write);
    s_infowin->resize (7 * sizes.OneInch, 3 * sizes.OneInch);
    window_bring_to_front (s_infowin);
}

EXPORT void infowin_show (int playlist, int entry)
{
    String filename = aud_playlist_entry_get_filename (playlist, entry);
    if (! filename)
        return;

    String error;
    PluginHandle * decoder = aud_playlist_entry_get_decoder (playlist, entry,
     Playlist::Wait, & error);
    Tuple tuple = decoder ? aud_playlist_entry_get_tuple (playlist, entry,
     Playlist::Wait, & error) : Tuple ();

    String entryfn = tuple.get_str (Tuple::AudioFile);
    if (! entryfn || ! entryfn[0])
        entryfn = filename;

    if (aud_get_bool (nullptr, "record"))  //JWT:SWITCH TO RECORDING FILE, IF RECORDING!:
    {
        filename = aud_get_str ("filewriter", "_record_fid");
        AUDDBG ("-infowin_show: RECORDING FID=%s=\n", (const char *) filename);
        VFSFile file (filename, "r");
        decoder = aud_file_find_decoder (filename, true, file, & error);
    }

    if (decoder && tuple.valid () && ! aud_custom_infowin (filename, decoder))
    {
        /* cuesheet entries cannot be updated - JWT:THEY CAN NOW IN FAUXDACIOUS (EXCEPT CUESHEET CAN OVERRIDE)! */
        bool can_write = false;

        if (aud_get_bool (nullptr, "record"))
            can_write = false; /* JWT:DON'T LET 'EM SAVE IF RECORDING AND NOT USING TAG-FILES! */
        else if (! strncmp (entryfn, "file://", 7))  /* JWT:MUST BE A LOCAL FILE! */
            can_write = aud_file_can_write_tuple (filename, decoder);

        tuple.delete_fallbacks ();
        show_infowin (playlist, entry, filename, tuple, decoder, can_write);
    }
    else
        infowin_hide ();

    if (error)
        aud_ui_show_error (str_printf (_("Error opening %s:\n%s"),
                (const char *) filename, (const char *) error));
}

EXPORT void infowin_show_current ()
{
    int playlist = aud_playlist_get_playing ();
    int position;

    if (playlist == -1)
        playlist = aud_playlist_get_active ();

    position = aud_playlist_get_position (playlist);

    if (position == -1)
        return;

    infowin_show (playlist, position);
}

EXPORT void infowin_hide ()
{
    delete s_infowin;
}

} // namespace audqt
