/*  Audacious - Cross-platform multimedia player
 *  Copyright (C) 2005-2011  Audacious development team.
 *
 *  BMP - Cross-platform multimedia player
 *  Copyright (C) 2003-2004  BMP development team.
 *
 *  Based on XMMS:
 *  Copyright (C) 1998-2003  XMMS development team.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses>.
 *
 *  The Audacious team does not consider modular code linking to
 *  Audacious or using our public API to be a derived work.
 */

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <libfauxdcore/i18n.h>
#include <libfauxdcore/playlist.h>
#include <libfauxdcore/runtime.h>
#include "gtk-compat.h"

static void search_cbt_cb (GtkWidget * called_cbt, GtkWidget * other_cbt)
{
    if (gtk_toggle_button_get_active ((GtkToggleButton *) called_cbt))
        gtk_toggle_button_set_active ((GtkToggleButton *) other_cbt, false);
}

static gboolean search_kp_cb (GtkWidget * entry, GdkEventKey * event, GtkWidget * dialog)
{
    if (event->keyval != GDK_KEY_Return)
        return false;

    gtk_dialog_response ((GtkDialog *) dialog, GTK_RESPONSE_ACCEPT);
    return true;
}

static void copy_selected_to_new (int playlist, bool jumptoit)
{
    int entries = aud_playlist_entry_count (playlist);
    Index<PlaylistAddItem> items;

    for (int entry = 0; entry < entries; entry ++)
    {
        if (aud_playlist_entry_get_selected (playlist, entry))
        {
            items.append (aud_playlist_entry_get_filename (playlist, entry),
                    aud_playlist_entry_get_tuple (playlist, entry, Playlist::NoWait),
                    aud_playlist_entry_get_decoder (playlist, entry, Playlist::NoWait));
        }
    }

    int new_list = aud_playlist_new ();
    aud_playlist_entry_insert_batch (new_list, 0, std::move (items), false);
    if (entries > 0 && jumptoit)
    {
        aud_playlist_set_active (new_list);
        aud_playlist_set_focus (new_list, 0);
        aud_playlist_set_position (new_list, 0);
    }
}

EXPORT void playlist_search_and_select ()
{
    /* create dialog */
    GtkWidget * dialog = gtk_dialog_new_with_buttons (
            _("Search entries in active playlist"), nullptr, (GtkDialogFlags) 0 ,
            _("Cancel"), GTK_RESPONSE_REJECT, _("Search"), GTK_RESPONSE_ACCEPT, nullptr);
    gtk_window_set_role ((GtkWindow *) dialog, "search");

    /* help text and logo */
    GtkWidget * hbox = audgui_hbox_new (6);
    GtkWidget * logo = gtk_image_new_from_icon_name ("edit-find", GTK_ICON_SIZE_DIALOG);
    GtkWidget * helptext = gtk_label_new (_("Select entries in playlist by filling one or more "
     "fields. Fields use regular expressions syntax, case-insensitive. If you don't know how "
     "regular expressions work, simply insert a literal portion of what you're searching for."));
    gtk_label_set_line_wrap ((GtkLabel *) helptext, true);
    gtk_box_pack_start ((GtkBox *) hbox, logo, false, false, 0);
    gtk_box_pack_start ((GtkBox *) hbox, helptext, false, false, 0);

    /* title */
    GtkWidget * label_title = gtk_label_new (_("Title:"));
    GtkWidget * entry_title = gtk_entry_new ();
    g_signal_connect (entry_title, "key-press-event", (GCallback) search_kp_cb, dialog);

    /* album */
    GtkWidget * label_album = gtk_label_new (_("Album:"));
    GtkWidget * entry_album = gtk_entry_new ();
    g_signal_connect (entry_album, "key-press-event", (GCallback) search_kp_cb, dialog);

    /* artist */
    GtkWidget * label_performer = gtk_label_new (_("Artist:"));
    GtkWidget * entry_performer = gtk_entry_new ();
    g_signal_connect (entry_performer, "key-press-event", (GCallback) search_kp_cb, dialog);

    /* file name */
    GtkWidget * label_file_name = gtk_label_new (_("File Name:"));
    GtkWidget * entry_file_name = gtk_entry_new ();
    g_signal_connect (entry_file_name, "key-press-event",
     (GCallback) search_kp_cb, dialog);

#ifdef USE_GTK3
    gtk_label_set_max_width_chars ((GtkLabel *) helptext, 60);

    gtk_widget_set_hexpand (entry_title, true);
    gtk_widget_set_halign (label_title, GTK_ALIGN_START);

    gtk_widget_set_hexpand (entry_album, true);
    gtk_widget_set_halign (label_album, GTK_ALIGN_START);

    gtk_widget_set_hexpand (entry_performer, true);
    gtk_widget_set_halign (label_performer, GTK_ALIGN_START);

    gtk_widget_set_hexpand (entry_file_name, true);
    gtk_widget_set_halign (label_file_name, GTK_ALIGN_START);
#else
    gtk_misc_set_alignment ((GtkMisc *) label_title, 1, 0.5);
    gtk_misc_set_alignment ((GtkMisc *) label_album, 1, 0.5);
    gtk_misc_set_alignment ((GtkMisc *) label_performer, 1, 0.5);
    gtk_misc_set_alignment ((GtkMisc *) label_file_name, 1, 0.5);
#endif

    /* some options that control behaviour */
    GtkWidget * checkbt_clearprevsel = gtk_check_button_new_with_label (
     _("Clear previous selection before searching"));
    gtk_toggle_button_set_active ((GtkToggleButton *) checkbt_clearprevsel, true);
    GtkWidget * checkbt_autoenqueue = gtk_check_button_new_with_label (
     _("Automatically toggle queue for matching entries"));
    gtk_toggle_button_set_active ((GtkToggleButton *) checkbt_autoenqueue, false);
    GtkWidget * checkbt_newplaylist = gtk_check_button_new_with_label (
     _("Create a new playlist with matching entries"));
    gtk_toggle_button_set_active ((GtkToggleButton *) checkbt_newplaylist, false);
    GtkWidget * checkbt_jumptoit = gtk_check_button_new_with_label (
     _("Jump right to it (unless new play list)"));
    gtk_toggle_button_set_active ((GtkToggleButton *) checkbt_jumptoit, false);

    g_signal_connect (checkbt_autoenqueue, "clicked",
     (GCallback) search_cbt_cb, checkbt_newplaylist);
    g_signal_connect (checkbt_newplaylist, "clicked",
     (GCallback) search_cbt_cb, checkbt_autoenqueue);

    /* place fields in grid */
    GtkWidget * main_grid = audgui_grid_new ();
    audgui_grid_set_row_spacing (main_grid, 6);
    audgui_grid_set_column_spacing (main_grid, 6);

#ifdef USE_GTK3
    gtk_widget_set_margin_bottom (hbox, 8);
    gtk_widget_set_margin_top (checkbt_clearprevsel, 8);

    GtkGrid * grid = (GtkGrid *) main_grid;
    gtk_grid_attach (grid, hbox, 0, 0, 2, 1);
    gtk_grid_attach (grid, label_title, 0, 1, 1, 1);
    gtk_grid_attach (grid, entry_title, 1, 1, 1, 1);
    gtk_grid_attach (grid, label_album, 0, 2, 1, 1);
    gtk_grid_attach (grid, entry_album, 1, 2, 1, 1);
    gtk_grid_attach (grid, label_performer, 0, 3, 1, 1);
    gtk_grid_attach (grid, entry_performer, 1, 3, 1, 1);
    gtk_grid_attach (grid, label_file_name, 0, 4, 1, 1);
    gtk_grid_attach (grid, entry_file_name, 1, 4, 1, 1);
    gtk_grid_attach (grid, checkbt_clearprevsel, 0, 5, 2, 1);
    gtk_grid_attach (grid, checkbt_autoenqueue, 0, 6, 2, 1);
    gtk_grid_attach (grid, checkbt_newplaylist, 0, 7, 2, 1);
    gtk_grid_attach (grid, checkbt_jumptoit, 0, 8, 2, 1);
#else
    GtkTable * grid = (GtkTable *) main_grid;
    gtk_table_attach_defaults (grid, hbox, 0, 2, 0, 1);
    gtk_table_attach (grid, label_title, 0, 1, 1, 2, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach_defaults (grid, entry_title, 1, 2, 1, 2);
    gtk_table_attach (grid, label_album, 0, 1, 2, 3, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach_defaults (grid, entry_album, 1, 2, 2, 3);
    gtk_table_attach (grid, label_performer, 0, 1, 3, 4, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach_defaults (grid, entry_performer, 1, 2, 3, 4);
    gtk_table_attach (grid, label_file_name, 0, 1, 4, 5, GTK_FILL, GTK_FILL, 0, 0);
    gtk_table_attach_defaults (grid, entry_file_name, 1, 2, 4, 5);
    gtk_table_attach_defaults (grid, checkbt_clearprevsel, 0, 2, 5, 6);
    gtk_table_attach_defaults (grid, checkbt_autoenqueue, 0, 2, 6, 7);
    gtk_table_attach_defaults (grid, checkbt_newplaylist, 0, 2, 7, 8);
    gtk_table_attach_defaults (grid, checkbt_jumptoit, 0, 2, 8, 9);
#endif

    gtk_container_set_border_width ((GtkContainer *) main_grid, 5);
    gtk_container_add ((GtkContainer *) gtk_dialog_get_content_area
     ((GtkDialog *) dialog), main_grid);
    gtk_widget_show_all (dialog);

    if (gtk_dialog_run ((GtkDialog *) dialog) == GTK_RESPONSE_ACCEPT)
    {
        /* create a TitleInput tuple with user search data */
        Tuple tuple;
        const char * searchdata = nullptr;
        int active_playlist = aud_playlist_get_active ();

        searchdata = gtk_entry_get_text ((GtkEntry *) entry_title);
        AUDDBG ("title=\"%s\"\n", searchdata);
        tuple.set_str (Tuple::Title, searchdata);

        searchdata = gtk_entry_get_text ((GtkEntry *) entry_album);
        AUDDBG ("album=\"%s\"\n", searchdata);
        tuple.set_str (Tuple::Album, searchdata);

        searchdata = gtk_entry_get_text ((GtkEntry *) entry_performer);
        AUDDBG ("performer=\"%s\"\n", searchdata);
        tuple.set_str (Tuple::Artist, searchdata);

        searchdata = gtk_entry_get_text ((GtkEntry *) entry_file_name);
        AUDDBG ("filename=\"%s\"\n", searchdata);
        tuple.set_str (Tuple::Basename, searchdata);

        /* check if previous selection should be cleared before searching */
        if (gtk_toggle_button_get_active ((GtkToggleButton *) checkbt_clearprevsel))
            aud_playlist_select_by_patterns (active_playlist, tuple);
        else
        {
            int entries = aud_playlist_entry_count (active_playlist);
            Index<int> entries_already_selected;
            /* SAVE PREV. SELECTIONS: */
            for (int entry = 0; entry < entries; entry ++)
            {
                if (aud_playlist_entry_get_selected (active_playlist, entry))
                    entries_already_selected.append (entry);
            }

            aud_playlist_select_by_patterns (active_playlist, tuple);

            /* RESELECT PREV. SELECTIONS (CLEARED BY aud_playlist_select_by_patterns(): */
            for (int entry = 0; entry < entries_already_selected.len (); entry ++)
                aud_playlist_entry_set_selected (active_playlist, entries_already_selected[entry], true);
        }

        /* check if a new playlist should be created after searching */
        if (gtk_toggle_button_get_active ((GtkToggleButton *) checkbt_newplaylist))
            copy_selected_to_new (active_playlist,
                    gtk_toggle_button_get_active ((GtkToggleButton *) checkbt_jumptoit));
        else
        {
            /* set focus on the first entry found */
            int entries = aud_playlist_entry_count (active_playlist);
            for (int count = 0; count < entries; count ++)
            {
                if (aud_playlist_entry_get_selected (active_playlist, count))
                {
                    aud_playlist_set_focus (active_playlist, count);
                    if (gtk_toggle_button_get_active ((GtkToggleButton *) checkbt_jumptoit))
                        aud_playlist_set_position (active_playlist, count);
                    break;
                }
            }

            /* check if matched entries should be queued */
            if (gtk_toggle_button_get_active ((GtkToggleButton *) checkbt_autoenqueue))
                aud_playlist_queue_insert_selected (active_playlist, -1);
        }
    }

    /* done here :) */
    gtk_widget_destroy (dialog);
}
