/*
 * infowin.c
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

#include <gtk/gtk.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <libfauxdcore/audstrings.h>
#include <libfauxdcore/hook.h>
#include <libfauxdcore/i18n.h>
#include <libfauxdcore/interface.h>
#include <libfauxdcore/mainloop.h>
#include <libfauxdcore/playlist.h>
#include <libfauxdcore/probe.h>
#include <libfauxdcore/runtime.h>
#include <libfauxdcore/tuple.h>
#include <libfauxdcore/drct.h>
#include <libfauxdcore/vfs.h>

#include "internal.h"
#include "libfauxdgui.h"
#include "libfauxdgui-gtk.h"

#define AUDGUI_STATUS_TIMEOUT 3000

enum {
    CODEC_FORMAT,
    CODEC_QUALITY,
    CODEC_BITRATE,
    CODEC_ITEMS
};

static const char * codec_labels[CODEC_ITEMS] = {
    N_("Format:"),
    N_("Quality:"),
    N_("Bitrate:")
};

static struct {
    GtkTextView * location;
    GtkWidget * location_scrolled;
    GtkTextBuffer * textbuffer;
    GtkWidget * title;
    GtkWidget * artist;
    GtkWidget * album;
    GtkWidget * album_artist;
    GtkWidget * comment;
    GtkWidget * year;
    GtkWidget * track;
    GtkWidget * genre;
    GtkWidget * image;
    GtkWidget * codec[3];
    GtkWidget * apply;
    GtkWidget * autofill;
    GtkWidget * ministatus;
} widgets;

static GtkWidget * infowin;
static int current_playlist_id, current_entry;
static bool force_image;
static String current_file;
static Tuple current_tuple;
static PluginHandle * current_decoder = nullptr;
static bool can_write = false;
static QueuedFunc ministatus_timer;

/* This is by no means intended to be a complete list.  If it is not short, it
 * is useless: scrolling through ten pages of dropdown list is more work than
 * typing out the genre. */

static const char * genre_table[] = {
 N_("Acid Jazz"),
 N_("Acid Rock"),
 N_("Ambient"),
 N_("Bebop"),
 N_("Bluegrass"),
 N_("Blues"),
 N_("Chamber Music"),
 N_("Classical"),
 N_("Country"),
 N_("Death Metal"),
 N_("Disco"),
 N_("Easy Listening"),
 N_("Folk"),
 N_("Funk"),
 N_("Gangsta Rap"),
 N_("Gospel"),
 N_("Grunge"),
 N_("Hard Rock"),
 N_("Heavy Metal"),
 N_("Hip-hop"),
 N_("House"),
 N_("Jazz"),
 N_("Jungle"),
 N_("Metal"),
 N_("New Age"),
 N_("New Wave"),
 N_("Noise"),
 N_("Pop"),
 N_("Punk Rock"),
 N_("Rap"),
 N_("Reggae"),
 N_("Rock"),
 N_("Rock and Roll"),
 N_("Rhythm and Blues"),
 N_("Ska"),
 N_("Soul"),
 N_("Swing"),
 N_("Techno"),
 N_("Trip-hop")};

static GtkWidget * small_label_new (const char * text)
{
    static PangoAttrList * attrs = nullptr;

    if (! attrs)
    {
        attrs = pango_attr_list_new ();
        pango_attr_list_insert (attrs, pango_attr_scale_new (PANGO_SCALE_SMALL));
    }

    GtkWidget * label = gtk_label_new (text);
    gtk_label_set_attributes ((GtkLabel *) label, attrs);
    gtk_misc_set_alignment ((GtkMisc *) label, 0, 0.5);

    return label;
}

static void set_entry_str_from_field (GtkWidget * widget, const Tuple & tuple,
 Tuple::Field field, bool editable, bool clear, bool & changed)
{
    String text = tuple.get_str (field);

    if (! text && ! clear)
    {
        if (gtk_entry_get_text_length ((GtkEntry *) widget) > 0)
            changed = true;
        return;
    }

    gtk_entry_set_text ((GtkEntry *) widget, text ? text : "");
    gtk_editable_set_editable ((GtkEditable *) widget, editable);
}

static void set_entry_int_from_field (GtkWidget * widget, const Tuple & tuple,
 Tuple::Field field, bool editable, bool clear, bool & changed)
{
    int value = tuple.get_int (field);

    if (value <= 0 && ! clear)
    {
        if (gtk_entry_get_text_length ((GtkEntry *) widget) > 0)
            changed = true;
        return;
    }

    gtk_entry_set_text ((GtkEntry *) widget, (value > 0) ? (const char *) int_to_str (value) : "");
    gtk_editable_set_editable ((GtkEditable *) widget, editable);
}

static void set_field_str_from_entry (Tuple & tuple, Tuple::Field field, GtkWidget * widget)
{
    const char * text = gtk_entry_get_text ((GtkEntry *) widget);

    if (text[0])
        tuple.set_str (field, text);
    else
        tuple.unset (field);
}

static void set_field_int_from_entry (Tuple & tuple, Tuple::Field field, GtkWidget * widget)
{
    const char * text = gtk_entry_get_text ((GtkEntry *) widget);

    if (text[0])
        tuple.set_int (field, atoi (text));
    else
        tuple.unset (field);
}

static void entry_changed ()
{
    if (can_write)
        gtk_widget_set_sensitive (widgets.apply, true);
}

static void ministatus_display_message (const char * text)
{
    gtk_label_set_text ((GtkLabel *) widgets.ministatus, text);
    gtk_widget_hide (widgets.autofill);
    gtk_widget_show (widgets.ministatus);

    ministatus_timer.queue (AUDGUI_STATUS_TIMEOUT, [] () {
        gtk_widget_hide (widgets.ministatus);
        gtk_widget_show (widgets.autofill);
    });
}

static void infowin_update_tuple ()
{
    bool success = false;

    set_field_str_from_entry (current_tuple, Tuple::Title, widgets.title);
    set_field_str_from_entry (current_tuple, Tuple::Artist, widgets.artist);
    set_field_str_from_entry (current_tuple, Tuple::Album, widgets.album);
    set_field_str_from_entry (current_tuple, Tuple::AlbumArtist, widgets.album_artist);
    set_field_str_from_entry (current_tuple, Tuple::Comment, widgets.comment);
    set_field_str_from_entry (current_tuple, Tuple::Genre,
            gtk_bin_get_child ((GtkBin *) widgets.genre));
    set_field_int_from_entry (current_tuple, Tuple::Year, widgets.year);
    set_field_int_from_entry (current_tuple, Tuple::Track, widgets.track);

    /* JWT:IF RECORDING ON, SAVE THE TAG DATA EDITS TO THE FILE BEING RECORDED! */
    if (aud_get_bool (nullptr, "record"))
    {
        String recording_file = aud_get_str ("filewriter", "_record_fid");
        if (recording_file && recording_file[0])
        {
            String error;
            VFSFile file (recording_file, "r");
            PluginHandle * decoder = aud_file_find_decoder (recording_file, true, file, & error);

            success = aud_file_write_tuple (recording_file, decoder, current_tuple);
        }
    }
    if (! success)
        success = aud_file_write_tuple (current_file, current_decoder, current_tuple);

    if (success)
    {
        ministatus_display_message (_("Save successful"));
        gtk_widget_set_sensitive (widgets.apply, false);
    }
    else
        ministatus_display_message (_("Save error"));
}

static void infowin_select_entry (int entry)
{
    int list = aud_playlist_by_unique_id (current_playlist_id);

    if (list >= 0 && entry >= 0 && entry < aud_playlist_entry_count (list))
    {
        aud_playlist_select_all (list, false);
        aud_playlist_entry_set_selected (list, entry, true);
        aud_playlist_set_focus (list, entry);
        audgui_infowin_show (list, entry);
    }
    else
        audgui_infowin_hide ();
}

static void infowin_prev ()
{
    infowin_select_entry (current_entry - 1);
}

static void infowin_next ()
{
    infowin_select_entry (current_entry + 1);
}

static void genre_fill (GtkWidget * combo)
{
    Index<const char *> list;
    for (auto genre : genre_table)
        list.append (_(genre));

    list.sort (g_utf8_collate);

    for (auto genre : list)
        gtk_combo_box_text_append_text ((GtkComboBoxText *) combo, genre);
}

static void autofill_toggled (GtkToggleButton * toggle)
{
    aud_set_bool ("audgui", "clear_song_fields", ! gtk_toggle_button_get_active (toggle));
}

static void infowin_display_image (const char * filename)
{
    if (! force_image && (! current_file || strcmp (filename, current_file)))
        return;

    AudguiPixbuf pb = audgui_pixbuf_request (filename);
    if (! pb)
        pb = audgui_pixbuf_fallback ();

    if (pb)
        audgui_scaled_image_set (widgets.image, pb.get ());
}

static void infowin_destroyed ()
{
    hook_dissociate ("art ready", (HookFunction) infowin_display_image);

    ministatus_timer.stop ();

    memset (& widgets, 0, sizeof widgets);

    infowin = nullptr;
    force_image = false;
    current_file = String ();
    current_tuple = Tuple ();
    current_decoder = nullptr;
}

static void add_entry (GtkWidget * grid, const char * title, GtkWidget * entry,
 int x, int y, int span)
{
    GtkWidget * label = small_label_new (title);

    gtk_table_attach ((GtkTable *) grid, label, x, x + span, y, y + 1,
     (GtkAttachOptions)(GTK_FILL | GTK_EXPAND), GTK_FILL, 0, 0);
    gtk_table_attach ((GtkTable *) grid, entry, x, x + span, y + 1, y + 2,
     (GtkAttachOptions)(GTK_FILL | GTK_EXPAND), GTK_FILL, 0, 0);

    g_signal_connect (entry, "changed", (GCallback) entry_changed, nullptr);
}

/* JWT:NEXT 3 FUNCTIONS ADDED TO ALLOW 'EM TO PICK A COVER-ART FILE TO FILL THE Comment: FIELD!: */
static void coverart_entry_response_cb (GtkWidget * dialog, int response, GtkWidget * entry)
{
    if (response == GTK_RESPONSE_ACCEPT)
    {
        const char * coverart_fid (gtk_file_chooser_get_uri ((GtkFileChooser *) dialog));
        if (coverart_fid)
        {
            force_image = true;
            const char * prev_comment = gtk_entry_get_text ((GtkEntry *) entry);
            String coverart_uri = (strstr (coverart_fid, "://"))
                    ? String (coverart_fid)
                    : String (filename_to_uri (filename_normalize (filename_expand (str_copy (coverart_fid)))));
            String coverart_str = coverart_uri;
            if (prev_comment)
            {
                const char * album_img_index = strstr (prev_comment, ";file:");
                /* IF COMMENT FIELD HAS AN ALBUM-ART (2ND) IMAGE: KEEP IT BUT REPLACE
                    WHAT'S BEFORE IT WITH SELECTED IMAGE FILE: */
                if (album_img_index)
                    coverart_str = String (str_concat ({(const char *) coverart_uri, album_img_index}));
            }
            infowin_display_image (coverart_uri);  // THEY PICKED AN IMAGE FILE, SHOW IT IN THE WINDOW!
            gtk_entry_set_text ((GtkEntry *) entry, coverart_str);
        }
    }

    gtk_widget_destroy (dialog);
}

static void coverart_entry_browse_cb (GtkWidget * entry, GtkEntryIconPosition pos,
 GdkEvent * event, void * data)
{
    GtkFileFilter * filter;
    GtkWidget * dialog = gtk_file_chooser_dialog_new (_("Select Cover Art File - Fauxdacious"), nullptr,
     GTK_FILE_CHOOSER_ACTION_OPEN, _("Cancel"), GTK_RESPONSE_CANCEL,
     _("Open"), GTK_RESPONSE_ACCEPT, nullptr);

    filter = gtk_file_filter_new ();
    gtk_file_filter_set_name (filter, _("Coverart image files"));
    gtk_file_filter_add_pattern (filter, _("*.jpg"));
    gtk_file_filter_add_pattern (filter, _("*.png"));
    gtk_file_filter_add_pattern (filter, _("*.jpeg"));
    gtk_file_filter_add_pattern (filter, _("*.gif"));
    gtk_file_chooser_add_filter ((GtkFileChooser *) dialog, filter);
    gtk_file_chooser_set_current_folder_uri ((GtkFileChooser *) dialog,
            filename_to_uri (aud_get_path (AudPath::UserDir)));
    gtk_file_chooser_set_local_only ((GtkFileChooser *) dialog, false);

    g_signal_connect (dialog, "response", (GCallback) coverart_entry_response_cb, entry);
    g_signal_connect_object (entry, "destroy", (GCallback) gtk_widget_destroy,
            dialog, G_CONNECT_SWAPPED);

    gtk_widget_show (dialog);
}

static GtkWidget * coverart_file_entry_new (GtkFileChooserAction action, const char * title)
{
    GtkWidget * entry = gtk_entry_new ();

    gtk_entry_set_icon_from_icon_name ((GtkEntry *) entry,
            GTK_ENTRY_ICON_SECONDARY, "document-open");
    g_signal_connect (entry, "icon-press", (GCallback) coverart_entry_browse_cb, _("File"));

    return entry;
}

static void create_infowin ()
{
    int dpi = audgui_get_dpi ();
    int xysize = (int)(2.5 * dpi);

    infowin = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_container_set_border_width ((GtkContainer *) infowin, 6);
    gtk_window_set_title ((GtkWindow *) infowin, _("Song Info - Fauxdacious"));
    gtk_window_set_type_hint ((GtkWindow *) infowin,
     GDK_WINDOW_TYPE_HINT_DIALOG);

    GtkWidget * main_grid = gtk_table_new (0, 0, false);
    gtk_table_set_col_spacings ((GtkTable *) main_grid, 6);
    gtk_table_set_row_spacings ((GtkTable *) main_grid, 6);
    gtk_container_add ((GtkContainer *) infowin, main_grid);

    widgets.image = audgui_scaled_image_new (nullptr);
    gtk_widget_set_size_request (widgets.image, xysize, xysize);
    gtk_table_attach ((GtkTable *) main_grid, widgets.image, 0, 1, 0, 1,
     GTK_FILL, GTK_FILL, 0, 0);

    widgets.location_scrolled = gtk_scrolled_window_new (nullptr, nullptr);
    gtk_scrolled_window_set_shadow_type ((GtkScrolledWindow *) widgets.location_scrolled, GTK_SHADOW_IN);
    gtk_scrolled_window_set_policy ((GtkScrolledWindow *) widgets.location_scrolled,
     GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    widgets.location = (GtkTextView *) gtk_text_view_new ();
    gtk_text_view_set_editable (widgets.location, false);
    gtk_text_view_set_cursor_visible (widgets.location, true);
    gtk_text_view_set_left_margin (widgets.location, 4);
    gtk_text_view_set_right_margin (widgets.location, 4);
    gtk_text_view_set_wrap_mode (widgets.location, GTK_WRAP_CHAR);
    widgets.textbuffer = gtk_text_view_get_buffer (widgets.location);

    gtk_container_add ((GtkContainer *) widgets.location_scrolled, (GtkWidget *) widgets.location);
    GtkWidget * scrolled_vbox = gtk_vbox_new (false, 6);
    gtk_box_pack_start ((GtkBox *) scrolled_vbox, widgets.location_scrolled, true, true, 0);
    gtk_widget_show_all (scrolled_vbox);
    gtk_table_attach ((GtkTable *) main_grid, scrolled_vbox, 0, 1, 1, 2,
     GTK_FILL, (GtkAttachOptions)(GTK_FILL | GTK_EXPAND), 0, 0);

    GtkWidget * codec_grid = gtk_table_new (0, 0, false);
    gtk_table_set_row_spacings ((GtkTable *) codec_grid, 2);
    gtk_table_set_col_spacings ((GtkTable *) codec_grid, 12);
    gtk_table_attach ((GtkTable *) main_grid, codec_grid, 0, 1, 2, 3,
     GTK_FILL, GTK_FILL, 0, 8);

    for (int row = 0; row < CODEC_ITEMS; row ++)
    {
        GtkWidget * label = small_label_new (_(codec_labels[row]));
        gtk_table_attach ((GtkTable *) codec_grid, label, 0, 1, row, row + 1,
         GTK_FILL, GTK_FILL, 0, 0);

        widgets.codec[row] = small_label_new (nullptr);
        gtk_table_attach ((GtkTable *) codec_grid, widgets.codec[row], 1, 2, row, row + 1,
         GTK_FILL, GTK_FILL, 0, 0);
    }

    GtkWidget * grid = gtk_table_new (0, 0, false);
    gtk_table_set_row_spacings ((GtkTable *) grid, 2);
    gtk_table_set_col_spacings ((GtkTable *) grid, 6);
    gtk_table_attach ((GtkTable *) main_grid, grid, 1, 2, 0, 3,
     (GtkAttachOptions)(GTK_FILL | GTK_EXPAND), GTK_FILL, 0, 0);

    widgets.title = gtk_entry_new ();
    gtk_widget_set_size_request (widgets.title, 3 * dpi, -1);
    add_entry (grid, _("Title"), widgets.title, 0, 0, 2);

    widgets.artist = gtk_entry_new ();
    add_entry (grid, _("Artist"), widgets.artist, 0, 2, 2);

    widgets.album = gtk_entry_new ();
    add_entry (grid, _("Album"), widgets.album, 0, 4, 2);

    widgets.album_artist = gtk_entry_new ();
    add_entry (grid, _("Album Artist"), widgets.album_artist, 0, 6, 2);

    /* JWT: ALLOW 'EM TO PICK A COVER-ART FILE TO FILL THE Comment: FIELD!: */
    widgets.comment = coverart_file_entry_new (GTK_FILE_CHOOSER_ACTION_OPEN, _("Coverart File"));
    add_entry (grid, _("Comment"), widgets.comment, 0, 8, 2);

    widgets.genre = gtk_combo_box_text_new_with_entry ();
    genre_fill (widgets.genre);
    add_entry (grid, _("Genre"), widgets.genre, 0, 10, 2);

    widgets.year = gtk_entry_new ();
    add_entry (grid, _("Year"), widgets.year, 0, 12, 1);

    widgets.track = gtk_entry_new ();
    add_entry (grid, _("Track Number"), widgets.track, 1, 12, 1);

    GtkWidget * bottom_hbox = gtk_hbox_new (false, 6);
    gtk_table_attach ((GtkTable *) main_grid, bottom_hbox, 0, 2, 3, 4,
     GTK_FILL, GTK_FILL, 0, 0);

    widgets.autofill = gtk_check_button_new_with_mnemonic (_("_Auto-fill empty fields"));

    gtk_toggle_button_set_active ((GtkToggleButton *) widgets.autofill,
     ! aud_get_bool ("audgui", "clear_song_fields"));
    g_signal_connect (widgets.autofill, "toggled", (GCallback) autofill_toggled, nullptr);

    gtk_widget_set_no_show_all (widgets.autofill, true);
    gtk_widget_show (widgets.autofill);
    gtk_box_pack_start ((GtkBox *) bottom_hbox, widgets.autofill, true, true, 0);

    widgets.ministatus = small_label_new (nullptr);
    gtk_widget_set_no_show_all (widgets.ministatus, true);
    gtk_box_pack_start ((GtkBox *) bottom_hbox, widgets.ministatus, true, true, 0);

    widgets.apply = audgui_button_new (_("_Save"), "document-save",
     (AudguiCallback) infowin_update_tuple, nullptr);

    GtkWidget * close_button = audgui_button_new (_("_Close"), "window-close",
     (AudguiCallback) audgui_infowin_hide, nullptr);

    GtkWidget * prev_button = audgui_button_new (_("_Previous"), "go-previous",
     (AudguiCallback) infowin_prev, nullptr);

    GtkWidget * next_button = audgui_button_new (_("_Next"), "go-next",
     (AudguiCallback) infowin_next, nullptr);

    gtk_box_pack_end ((GtkBox *) bottom_hbox, close_button, false, false, 0);
    gtk_box_pack_end ((GtkBox *) bottom_hbox, widgets.apply, false, false, 0);
    gtk_box_pack_end ((GtkBox *) bottom_hbox, next_button, false, false, 0);
    gtk_box_pack_end ((GtkBox *) bottom_hbox, prev_button, false, false, 0);

    audgui_destroy_on_escape (infowin);
    g_signal_connect (infowin, "destroy", (GCallback) infowin_destroyed, nullptr);

    hook_associate ("art ready", (HookFunction) infowin_display_image, nullptr);
}

static void infowin_show (int list, int entry, const String & filename, const String & entryfn,
 const Tuple & tuple, PluginHandle * decoder, bool writable)
{
    if (! infowin)
        create_infowin ();

    current_playlist_id = aud_playlist_get_unique_id (list);
    current_entry = entry;
    current_file = entryfn;
    force_image = false;
    current_tuple = tuple.ref ();
    current_decoder = decoder;
    can_write = writable;

    bool clear = aud_get_bool ("audgui", "clear_song_fields");
    bool changed = false;

    set_entry_str_from_field (widgets.title, tuple, Tuple::Title, writable, clear, changed);
    set_entry_str_from_field (widgets.artist, tuple, Tuple::Artist, writable, clear, changed);
    set_entry_str_from_field (widgets.album, tuple, Tuple::Album, writable, clear, changed);
    set_entry_str_from_field (widgets.album_artist, tuple, Tuple::AlbumArtist, writable, clear, changed);
    set_entry_str_from_field (widgets.comment, tuple, Tuple::Comment, writable, clear, changed);
    set_entry_str_from_field (gtk_bin_get_child ((GtkBin *) widgets.genre),
     tuple, Tuple::Genre, writable, clear, changed);

    gtk_text_buffer_set_text (widgets.textbuffer, uri_to_display (filename), -1);

    set_entry_int_from_field (widgets.year, tuple, Tuple::Year, writable, clear, changed);
    set_entry_int_from_field (widgets.track, tuple, Tuple::Track, writable, clear, changed);

    String codec_values[CODEC_ITEMS];

    codec_values[CODEC_FORMAT] = tuple.get_str (Tuple::Codec);
    codec_values[CODEC_QUALITY] = tuple.get_str (Tuple::Quality);

    if (tuple.get_value_type (Tuple::Bitrate) == Tuple::Int)
        codec_values[CODEC_BITRATE] = String (str_printf (_("%d kb/s"),
         tuple.get_int (Tuple::Bitrate)));

    for (int row = 0; row < CODEC_ITEMS; row ++)
    {
        const char * text = codec_values[row] ? (const char *) codec_values[row] : _("N/A");
        gtk_label_set_text ((GtkLabel *) widgets.codec[row], text);
    }

    infowin_display_image (entryfn);

    gtk_widget_set_sensitive (widgets.apply, changed);
    gtk_widget_grab_focus (widgets.title);

    if (! audgui_reshow_unique_window (AUDGUI_INFO_WINDOW))
        audgui_show_unique_window (AUDGUI_INFO_WINDOW, infowin);
}

EXPORT void audgui_infowin_show (int playlist, int entry)
{
    String filename = aud_playlist_entry_get_filename (playlist, entry);
    g_return_if_fail (filename != nullptr);

    String entryfn = filename;
    String error;
    PluginHandle * decoder = aud_playlist_entry_get_decoder (playlist, entry,
     Playlist::Wait, & error);
    Tuple tuple = decoder ? aud_playlist_entry_get_tuple (playlist, entry,
     Playlist::Wait, & error) : Tuple ();

    if (aud_get_bool (nullptr, "record"))  //JWT:SWITCH TO RECORDING FILE, IF RECORDING!:
    {
        filename = aud_get_str ("filewriter", "_record_fid");
        VFSFile file (filename, "r");
        decoder = aud_file_find_decoder (filename, true, file, & error);
    }

    if (decoder && tuple.valid () && ! aud_custom_infowin (filename, decoder))
    {
        /* cuesheet entries cannot be updated - JWT:THEY CAN NOW IN FAUXDACIOUS (EXCEPT CUESHEET CAN OVERRIDE)! */
        bool can_write = aud_file_can_write_tuple (filename, decoder);
        /* JWT:LET 'EM SAVE TO USER'S CONFIG FILE IF CAN'T SAVE TO FILE/STREAM: */
        if (! can_write && aud_get_bool (nullptr, "user_tag_data"))
            can_write = true;

        tuple.delete_fallbacks ();
        infowin_show (playlist, entry, filename, entryfn, tuple, decoder, can_write);
    }
    else
        audgui_infowin_hide ();

    if (error)
        aud_ui_show_error (str_printf (_("Error opening %s:\n%s"),
         (const char *) filename, (const char *) error));
}

EXPORT void audgui_infowin_show_current ()
{
AUDERR("--audgui_infowin_show_current called...\n");
    int playlist = aud_playlist_get_playing ();
    int position;

    if (playlist == -1)
        playlist = aud_playlist_get_active ();

    position = aud_playlist_get_position (playlist);

AUDERR("--POSITION=%d=\n", position);
    if (position == -1)
        return;

    audgui_infowin_show (playlist, position);
}

EXPORT void audgui_infowin_hide ()
{
    audgui_hide_unique_window (AUDGUI_INFO_WINDOW);
}