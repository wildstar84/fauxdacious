/*
 * about.cc
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

#include <QDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QVBoxLayout>

#include <libfauxdcore/audstrings.h>
#include <libfauxdcore/i18n.h>
#include <libfauxdcore/runtime.h>
#include <libfauxdcore/vfs.h>

#include "libfauxdqt.h"

static QTabWidget * buildCreditsNotebook (QWidget * parent)
{
    const char * data_dir = aud_get_path (AudPath::DataDir);
    const char * titles[2] = {N_("Credits"), N_("License")};
    const char * filenames[2] = {"AUTHORS", "COPYING"};

    auto tabs = new QTabWidget (parent);
    tabs->setDocumentMode (true);
    tabs->setMinimumSize (6 * audqt::sizes.OneInch, 2 * audqt::sizes.OneInch);

    for (int i = 0; i < 2; i ++)
    {
        auto text = VFSFile::read_file (filename_build ({data_dir, filenames[i]}), VFS_APPEND_NULL);
        auto edit = new QPlainTextEdit (text.begin (), parent);
        edit->setReadOnly (true);
        edit->setFrameStyle (QFrame::NoFrame);
        tabs->addTab (edit, _(titles[i]));
    }

    return tabs;
}

static QDialog * buildAboutWindow ()
{
// JWT:FIXME: RETAINED NEXT 2 FOR OUR LOGO TO WORK:
    const char * data_dir = aud_get_path (AudPath::DataDir);
    const char * logo_path = filename_build ({data_dir, "images", "about-logo.png"});
    const char * about_text = "<big><b>Fauxdacious " VERSION "</b></big><br>" COPYRIGHT;
    const char * website = "https://wildstar84.wordpress.com/fauxdacious";

    auto window = new QDialog;
    window->setWindowTitle (_("About Fauxdacious"));

    auto logo = new QLabel (window);
    logo->setPixmap (QPixmap (logo_path));
// JWT:FIXME: REVERTED NEXT 2 PREV. 1 SINCE SVG VSN. OF OUR LOGO DOESN'T WORK
// IN QT AND I DON'T KNOW HOW TO PROPERLY CONVERT IT TO QT'S LIKING?!
//    int logo_size = audqt::to_native_dpi (400);
//    logo->setPixmap (QIcon (":/about-logo.svg").pixmap (logo_size, logo_size));
    logo->setAlignment (Qt::AlignHCenter);

    auto text = new QLabel (about_text, window);
    text->setAlignment (Qt::AlignHCenter);

    auto anchor = QString ("<a href='%1'>%1</a>").arg (website);
    auto link_label = new QLabel (anchor, window);
    link_label->setAlignment (Qt::AlignHCenter);
    link_label->setOpenExternalLinks (true);

    auto layout = audqt::make_vbox (window);
    layout->addSpacing (audqt::sizes.EightPt);
    layout->addWidget (logo);
    layout->addWidget (text);
    layout->addWidget (link_label);
    layout->addWidget (buildCreditsNotebook (window));

    return window;
}

static QDialog * s_aboutwin = nullptr;

namespace audqt {

EXPORT void aboutwindow_show ()
{
    if (! s_aboutwin)
    {
        s_aboutwin = buildAboutWindow ();
        s_aboutwin->setAttribute (Qt::WA_DeleteOnClose);

        QObject::connect (s_aboutwin, & QObject::destroyed, [] () {
            s_aboutwin = nullptr;
        });
    }

    window_bring_to_front (s_aboutwin);
}

EXPORT void aboutwindow_hide ()
{
    if (s_aboutwin)
        delete s_aboutwin;
}

} // namespace audqt
