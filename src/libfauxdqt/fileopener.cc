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

#include <libfauxdcore/drct.h>
#include <libfauxdcore/i18n.h>
#include <libfauxdcore/playlist.h>
#include <libfauxdcore/runtime.h>

#include <libfauxdqt/libfauxdqt.h>

namespace audqt {

static aud::array<FileMode, QPointer<QFileDialog>> s_dialogs;

static void import_playlist (int playlist, const String & filename)
{
    aud_playlist_set_filename (playlist, filename);
    aud_playlist_entry_delete (playlist, 0, aud_playlist_entry_count (playlist));
    aud_playlist_entry_insert (playlist, -1, filename, Tuple (), false);
}

static void export_playlist (int playlist, const String & filename)
{
    Playlist::GetMode mode = Playlist::Wait;
    if (aud_get_bool (nullptr, "metadata_on_play"))
        mode = Playlist::NoWait;

    aud_playlist_set_filename (playlist, filename);
    aud_playlist_save (playlist, filename, mode);
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
        dialog->setLabelText (QFileDialog::Accept, _(labels[mode]));

        if (mode == FileMode::ExportPlaylist)
            dialog->setAcceptMode (QFileDialog::AcceptSave);

        QObject::connect (dialog.data (), & QFileDialog::directoryEntered, [] (const QString & path)
            { aud_set_str ("audgui", "filesel_path", path.toUtf8 ().constData ()); });

        int playlist = aud_playlist_get_active ();

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
                    export_playlist (playlist, files[0].filename);
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
