/*
 * fileopener.cc
 * Copyright 2014 Michał Lipski
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

#include <QFileDialog>
#include <QPointer>
#include <QRegularExpression>

#include <libaudcore/audstrings.h>
#include <libfauxdcore/drct.h>
#include <libfauxdcore/i18n.h>
#include <libaudcore/interface.h>
#include <libfauxdcore/playlist.h>
#include <libfauxdcore/runtime.h>
#include <libfauxdcore/audstrings.h>

#include <libfauxdqt/libfauxdqt.h>

namespace audqt {

static aud::array<FileMode, QPointer<QFileDialog>> s_dialogs;

static void import_playlist (int playlist, const char * filename)
{
    aud_playlist_set_filename (playlist, filename);
    aud_playlist_entry_delete (playlist, 0, aud_playlist_entry_count (playlist));
    aud_playlist_entry_insert (playlist, -1, filename, Tuple (), false);
}

static void export_playlist (int playlist, const char * filename)
{
    Playlist::GetMode mode = Playlist::Wait;
    if (aud_get_bool (nullptr, "metadata_on_play"))
        mode = Playlist::NoWait;

    aud_playlist_set_filename (playlist, filename);
    aud_playlist_save (playlist, filename, mode);
}

static void export_playlist (int playlist, const char * filename,
        const char * default_ext)
{
    if (uri_get_extension(filename))
        export_playlist (playlist, filename);
    else if (default_ext && default_ext[0])
        export_playlist (playlist, str_concat({filename, ".", default_ext}));
    else
        aud_ui_show_error (_("Please type a filename extension or select a "
                            "format from the drop-down list."));
}

static QStringList get_format_filters ()
{
    QStringList filters;
    filters.append (QString (_("Select Format by Extension")).append (" (*)"));

    for (auto & format : aud_playlist_save_formats ())
    {
        QStringList extensions;
        for (const char * ext : format.exts)
            extensions.append (QString("*.%1").arg (ext));

        filters.append(QString("%1 (%2)")
                .arg(static_cast<const char *> (format.name))
                .arg(extensions.join (' ')));
    }

    return filters;
}

static QString get_extension_from_filter (QString filter)
{
    /*
     * Find first extension in filter string
     * "Audacious Playlists (*.audpl)" -> audpl
     * "M3U Playlists (*.m3u *.m3u8)"  -> m3u
     */
    QRegularExpression regex (".*\\(\\*\\.(\\S*).*\\)$");
    return regex.match (filter).captured (1);
}

EXPORT void fileopener_show (FileMode mode)
{
    QPointer<QFileDialog> & dialog = s_dialogs[mode];

    if (! dialog)
    {
        static constexpr aud::array<FileMode, const char *> titles {
            N_("Open Files - Fauxdacious"),
            N_("Open Folder - Fauxdacious"),
            N_("Add Files - Fauxdacious"),
            N_("Add Folder - Fauxdacious"),
            N_("Import Playlist - Fauxdacious"),
            N_("Export Playlist - Fauxdacious")
        };

        static constexpr aud::array<FileMode, const char *> labels {
            N_("Open"),
            N_("Open"),
            N_("Add"),
            N_("Add"),
            N_("Import"),
            N_("Export")
        };

        static constexpr aud::array<FileMode, QFileDialog::FileMode> modes {
            QFileDialog::ExistingFiles,
            QFileDialog::Directory,
            QFileDialog::ExistingFiles,
            QFileDialog::Directory,
            QFileDialog::ExistingFile,
            QFileDialog::AnyFile
        };

        String path = aud_get_str ("audgui", "filesel_path");
        dialog = new QFileDialog (nullptr, _(titles[mode]), QString (path));

        dialog->setAttribute (Qt::WA_DeleteOnClose);
        dialog->setFileMode (modes[mode]);
        if (modes[mode] == QFileDialog::FileMode::Directory)
            dialog->setOption (QFileDialog::ShowDirsOnly);
        dialog->setLabelText (QFileDialog::Accept, _(labels[mode]));
        dialog->setLabelText (QFileDialog::Reject, _("Cancel"));

        /* JWT:DON'T USE DEFAULT NATIVE DIALOG IF DARK THEME OR ICON-THEME IS SET (WILL IGNORE DARK THEME/ICONS)!: */
        int use_native_sysdialogs = aud_get_int ("audqt", "use_native_sysdialogs");
        String icon_theme = aud_get_str ("audqt", "icon_theme");
        if (use_native_sysdialogs == 2 || (use_native_sysdialogs == 1
                && (!strcmp (aud_get_str ("audqt", "theme"), "dark") || (icon_theme && icon_theme[0]))))
            dialog->setOption(QFileDialog::DontUseNativeDialog);

        int playlist = aud_playlist_get_active ();

        if (mode == FileMode::ExportPlaylist)
        {
            dialog->setAcceptMode (QFileDialog::AcceptSave);
            dialog->setNameFilters(get_format_filters());

            String filename = aud_playlist_get_filename (playlist);
            if (filename)
            {
                StringBuf local = uri_to_filename (filename);
                if (local)
                    dialog->selectFile ((const char *) local);
                else
                    dialog->selectUrl (QUrl ((const char *) filename));
            }
        }

        QObject::connect (dialog.data (), & QFileDialog::directoryEntered, [] (const QString & path)
            { aud_set_str ("audgui", "filesel_path", path.toUtf8 ().constData ()); });

        QObject::connect (dialog.data (), & QFileDialog::accepted, [dialog, mode, playlist] ()
        {
            Index<PlaylistAddItem> files;
            for (const QUrl & url : dialog->selectedUrls ())
                files.append (String (url.toEncoded ().constData ()));

            switch (mode)
            {
            case FileMode::Add:
            case FileMode::AddFolder:
                aud_drct_pl_add_list (std::move (files), -1);
                break;
            case FileMode::Open:
            case FileMode::OpenFolder:
                aud_drct_pl_open_list (std::move (files));
                break;
            case FileMode::ImportPlaylist:
                if (files.len () == 1)
                    import_playlist (playlist, files[0].filename);
                break;
            case FileMode::ExportPlaylist:
                if (files.len () == 1)
                {
                    QString filter = dialog->selectedNameFilter ();
                    QString extension = get_extension_from_filter (filter);
                    export_playlist (playlist, files[0].filename,
                            extension.toUtf8 ());
                }
                break;
            default:
                /* not reached */
                break;
            }
        });
    }

    window_bring_to_front (dialog);
}

} // namespace audqt
