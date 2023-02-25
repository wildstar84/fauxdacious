/*
 * about.c
 * Copyright 2011-2013 John Lindgren
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

#include <gtk/gtk.h>

#include <libfauxdcore/audstrings.h>
#include <libfauxdcore/i18n.h>
#include <libfauxdcore/runtime.h>
#include <libfauxdcore/vfs.h>

#include "gtk-compat.h"
#include "internal.h"
#include "libfauxdgui.h"
#include "libfauxdgui-gtk.h"

static const char about_text[] = "<big><b>Fauxdacious " VERSION "</b> (" BUILDSTAMP ")</big>\n" COPYRIGHT;
static const char website[] = "https://wildstar84.wordpress.com/fauxdacious";

static GtkWidget * create_credits_notebook (const char * credits, const char * license, const char * manpage)
{
    const char * titles[3] = {N_("Credits"), N_("License"), N_("Help")};
    const char * text[3] = {credits, license, manpage};

    GtkWidget * notebook = gtk_notebook_new ();

    for (int i = 0; i < 3; i ++)
    {
        GtkWidget * label = gtk_label_new (_(titles[i]));

        GtkWidget * scrolled = gtk_scrolled_window_new (nullptr, nullptr);
        gtk_widget_set_size_request (scrolled, -1, 2 * audgui_get_dpi ());
        gtk_scrolled_window_set_policy ((GtkScrolledWindow *) scrolled,
         GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

        GtkTextBuffer * buffer = gtk_text_buffer_new (nullptr);
        gtk_text_buffer_set_text (buffer, text[i], -1);
        GtkWidget * text = gtk_text_view_new_with_buffer (buffer);
        gtk_text_view_set_editable ((GtkTextView *) text, false);
        gtk_text_view_set_cursor_visible ((GtkTextView *) text, false);
        gtk_text_view_set_left_margin ((GtkTextView *) text, 6);
        gtk_text_view_set_right_margin ((GtkTextView *) text, 6);
        gtk_container_add ((GtkContainer *) scrolled, text);

        gtk_notebook_append_page ((GtkNotebook *) notebook, scrolled, label);
    }

    return notebook;
}

static GtkWidget * create_about_window ()
{
    const char * data_dir = aud_get_path (AudPath::DataDir);

    int dpi = audgui_get_dpi ();

    GtkWidget * about_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title ((GtkWindow *) about_window, _("About Fauxdacious"));
    gtk_window_set_resizable ((GtkWindow *) about_window, false);
    gtk_container_set_border_width ((GtkContainer *) about_window, 3);

    audgui_destroy_on_escape (about_window);

    GtkWidget * vbox = audgui_vbox_new (6);
    gtk_container_add ((GtkContainer *) about_window, vbox);

#ifdef _WIN32
    StringBuf logo_path = filename_build ({data_dir, "images", "about-logo-win32.png"});
#else
    StringBuf logo_path = filename_build ({data_dir, "images", "about-logo.png"});
#endif
    GtkWidget * image = gtk_image_new_from_file (logo_path);
    gtk_box_pack_start ((GtkBox *) vbox, image, false, false, 0);

    GtkWidget * label = gtk_label_new (nullptr);
    gtk_label_set_markup ((GtkLabel *) label, about_text);
    gtk_label_set_justify ((GtkLabel *) label, GTK_JUSTIFY_CENTER);
    gtk_box_pack_start ((GtkBox *) vbox, label, false, false, 0);

    GtkWidget * button = gtk_link_button_new (website);

#ifdef USE_GTK3
    gtk_widget_set_halign (button, GTK_ALIGN_CENTER);
    gtk_box_pack_start ((GtkBox *) vbox, button, false, false, 0);
#else
    GtkWidget * align = gtk_alignment_new (0.5, 0.5, 0, 0);
    gtk_container_add ((GtkContainer *) align, button);
    gtk_box_pack_start ((GtkBox *) vbox, align, false, false, 0);
#endif

    auto credits = VFSFile::read_file (filename_build ({data_dir, "AUTHORS"}), VFS_APPEND_NULL);
    auto license = VFSFile::read_file (filename_build ({data_dir, "COPYING"}), VFS_APPEND_NULL);
    auto manpage = VFSFile::read_file (filename_build ({data_dir, "MANTEXT"}), VFS_APPEND_NULL);

    GtkWidget * notebook = create_credits_notebook (credits.begin (), license.begin (), manpage.begin ());
    gtk_widget_set_size_request (notebook, 6 * dpi, 2 * dpi);
    gtk_box_pack_start ((GtkBox *) vbox, notebook, true, true, 0);

    return about_window;
}

EXPORT void audgui_show_about_window ()
{
    if (! audgui_reshow_unique_window (AUDGUI_ABOUT_WINDOW))
        audgui_show_unique_window (AUDGUI_ABOUT_WINDOW, create_about_window ());
}

EXPORT void audgui_hide_about_window ()
{
    audgui_hide_unique_window (AUDGUI_ABOUT_WINDOW);
}
