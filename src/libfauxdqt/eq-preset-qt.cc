/*
 * eq-preset-qt.cc
 * Copyright 2018 John Lindgren
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

#include "libfauxdqt.h"
#include "treeview.h"

#include <QBoxLayout>
#include <QDialog>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QCheckBox>
#include <QStandardItemModel>

#include "libfauxdcore/audstrings.h"
#include "libfauxdcore/equalizer.h"
#include "libfauxdcore/i18n.h"
#include "libfauxdcore/interface.h"
#include "libfauxdcore/playlist.h"
#include "libfauxdcore/runtime.h"
#include "libfauxdcore/vfs.h"

namespace audqt {

class PresetItem : public QStandardItem
{
public:
    explicit PresetItem (const EqualizerPreset & preset) :
        QStandardItem ((const char *) preset.name),
        preset (preset) {}

    const EqualizerPreset preset;
};

class PresetModel : public QStandardItemModel
{
public:
    explicit PresetModel(QObject * parent)
        : QStandardItemModel(0, 1, parent),
          m_orig_presets(aud_eq_read_presets("eq.preset"))
    {
        revert_all();
    }

    void revert_all ();
    void save_all ();

    QModelIndex add_preset (const EqualizerPreset & preset);
    QModelIndex add_preset (const char * name);
    void apply_preset (int row);

    const EqualizerPreset * preset_at (int row) const
    {
        auto pitem = static_cast<PresetItem *> (item (row));
        return pitem ? & pitem->preset : nullptr;
    }

    bool removeRows (int row, int count, const QModelIndex & parent = QModelIndex ()) override
    {
        bool removed = QStandardItemModel::removeRows (row, count, parent);
        m_changed = m_changed || removed;
        return removed;
    }

private:
    Index<EqualizerPreset> const m_orig_presets;
    bool m_changed = false;
};

void PresetModel::revert_all ()
{
    clear ();

    for (const EqualizerPreset & preset : m_orig_presets)
        appendRow (new PresetItem (preset));

    m_changed = false;
}

void PresetModel::save_all ()
{
    if (! m_changed)
        return;

    Index<EqualizerPreset> presets;
    for (int row = 0; row < rowCount (); row ++)
        presets.append (* preset_at (row));

    presets.sort ([] (const EqualizerPreset & a, const EqualizerPreset & b)
        { return strcmp (a.name, b.name); });

    aud_eq_write_presets (presets, "eq.preset");
    m_changed = false;
}

QModelIndex PresetModel::add_preset (const EqualizerPreset & preset)
{
    int insert_idx = rowCount ();
    for (int row = 0; row < rowCount (); row ++)
    {
        if (preset_at (row)->name == preset.name)
        {
            insert_idx = row;
            break;
        }
    }

    setItem (insert_idx, new PresetItem (preset));
    m_changed = true;

    return index (insert_idx, 0);
}

QModelIndex PresetModel::add_preset (const char * name)
{
    EqualizerPreset preset {String (name)};
    aud_eq_update_preset (preset);
    return add_preset (preset);
}

void PresetModel::apply_preset (int row)
{
    auto preset = preset_at (row);
    if (! preset)
        return;

    aud_eq_apply_preset (* preset);
    aud_set_bool (nullptr, "equalizer_active", true);
}

class PresetView : public TreeView
{
public:
    PresetView (QPushButton * export_btn) :
        m_export_btn (export_btn)
    {
        setEditTriggers (QTreeView::NoEditTriggers);
        setHeaderHidden (true);
        setIndentation (0);
        setSelectionMode (QTreeView::ExtendedSelection);
        setUniformRowHeights (true);

        setModel (new PresetModel (this));

        connect(this, &QTreeView::activated, [this](const QModelIndex & index) {
            pmodel()->apply_preset(index.row());
        });
    }

    PresetModel * pmodel () const
        { return static_cast<PresetModel *> (model ()); }

    void add_imported (const Index<EqualizerPreset> & presets);

    const EqualizerPreset * preset_for_export () const
    {
        auto idxs = selectionModel ()->selectedIndexes ();
        if (idxs.size () != 1)
            return nullptr;

        return pmodel ()->preset_at (idxs[0].row ());
    }

    void removeSelRows ()
    {
        while (1)
        {
            auto idxs = selectionModel ()->selectedIndexes ();
            if (idxs.size () <= 0)
                break;

            pmodel ()->removeRows (idxs[0].row (), 1);
        }
    }

protected:
    void selectionChanged (const QItemSelection & selected,
                           const QItemSelection & deselected) override
    {
        TreeView::selectionChanged (selected, deselected);

        auto idxs = selectionModel ()->selectedIndexes ();
        // JWT:THEY DON'T HAVE TO SELECT ONE IN FAUXDACIOUS!:  m_export_btn->setEnabled (idxs.size () == 1);
    }

private:
    QPushButton * m_export_btn;
};

void PresetView::add_imported (const Index<EqualizerPreset> & presets)
{
    QItemSelection sel;
    for (auto & preset : presets)
    {
        auto idx = pmodel ()->add_preset (preset);
        sel.select (idx, idx);
    }

    selectionModel ()->select (sel, QItemSelectionModel::Clear |
                                    QItemSelectionModel::SelectCurrent);

    if (presets.len () == 1)
    {
        aud_eq_apply_preset (presets[0]);
        aud_set_bool (nullptr, "equalizer_active", true);
    }
}

static Index<EqualizerPreset> import_file (const char * filename)
{
    VFSFile file (filename, "r");
    if (! file)
        return Index<EqualizerPreset> ();

    if (str_has_suffix_nocase (filename, ".eqf") ||
        str_has_suffix_nocase (filename, ".q1"))
    {
        return aud_import_winamp_presets (file);
    }

    /* assume everything else is a native preset file */
    Index<EqualizerPreset> presets;
    presets.append ();

    if (! aud_load_preset_file (presets[0], file))
        presets.clear ();

    return presets;
}

static bool export_file (const char * filename, const EqualizerPreset & preset)
{
    VFSFile file (filename, "w");
    if (! file)
        return false;

    if (str_has_suffix_nocase (filename, ".eqf") ||
        str_has_suffix_nocase (filename, ".q1"))
    {
        return aud_export_winamp_preset (preset, file);
    }

    /* assume everything else is a native preset file */
    bool filesave_result = aud_save_preset_file (preset, file);

    // JWT:IF [Auto] IS ON && FILENAME SAVED == SONG AUTO (DEFAULT) PATH/SONGNAME.preset, LIGHT UP THE 
    // [PRESET] BUTTON INDICATING SONG-SPECIFIC EQUALIZATION IS IN EFFECT!

    if (filesave_result && aud_get_bool (nullptr, "equalizer_autoload") 
            && ! strcmp((const char *) str_decode_percent (aud_get_str(nullptr, "_eq_last_preset_filename"),-1), 
            (const char *) str_decode_percent (filename)))
    {
        aud_set_bool(nullptr, "equalizer_songauto", true);
        aud_set_str(nullptr, "_eq_last_preset_filename", "");
    }
    return filesave_result;
}

static const char * name_filter = N_("Preset files (*.preset *.eqf *.q1)");

static void set_default_preset_dir ()
{
    aud_set_str (nullptr, "_preset_dir", aud_get_path (AudPath::UserDir));
    if (aud_get_bool (nullptr, "try_local_preset_files"))
    {
        int current_playlist = aud_playlist_get_playing ();
        if (current_playlist >= 0)
        {
            int current_song = aud_playlist_get_position (current_playlist);
            if (current_song >= 0)
            {
                String filename = aud_playlist_entry_get_filename (current_playlist, current_song);

                StringBuf scheme = uri_get_scheme (filename);
                if (aud_get_bool (nullptr, "try_local_preset_files") && ! strcmp (scheme, "file"))
                {
                    StringBuf path = filename_get_parent ((const char *) uri_to_filename (filename));
                    aud_set_str (nullptr, "_preset_dir", (const char *) path);
                }
            }
        }
    }
}

static void show_import_dialog (QDialog * parent, PresetView * view, QPushButton * revert_btn)
{
    auto dialog = new QFileDialog (parent, _("Load Preset File - Fauxdacious"));

    dialog->setAttribute (Qt::WA_DeleteOnClose);
    dialog->setFileMode (QFileDialog::ExistingFile);
    dialog->setLabelText (QFileDialog::Accept, _("Load"));
    dialog->setNameFilter (_(name_filter));
    dialog->setWindowRole ("file-dialog");
    set_default_preset_dir ();
    dialog->setDirectory (QString (aud_get_str (nullptr, "_preset_dir")));

    /* JWT:DON'T USE DEFAULT NATIVE DIALOG IF DARK THEME OR ICON-THEME IS SET (WILL IGNORE DARK THEME/ICONS)!: */
    int use_native_sysdialogs = aud_get_int ("audqt", "use_native_sysdialogs");
    String icon_theme = aud_get_str ("audqt", "icon_theme");
    if (use_native_sysdialogs == 2 || (use_native_sysdialogs == 1
            && (!strcmp (aud_get_str ("audqt", "theme"), "dark") || (icon_theme && icon_theme[0]))))
        dialog->setOption(QFileDialog::DontUseNativeDialog);

    auto do_import = [dialog, view, revert_btn]() {
        auto urls = dialog->selectedUrls ();
        if (urls.size () != 1)
            return;

        auto filename = urls[0].toEncoded ();
        auto presets = import_file (filename);

        if (presets.len ())
        {
            view->add_imported (presets);
            view->pmodel ()->save_all ();
            revert_btn->setEnabled (true);
            dialog->deleteLater ();
        }
        else
        {
            aud_ui_show_error (str_printf (_("Error loading %s."), filename.constData ()));
        }
    };

    QObject::connect (dialog, &QFileDialog::accepted, do_import);

    window_bring_to_front (dialog);
}

static void show_export_dialog (QDialog * parent, const EqualizerPreset & preset)
{
    String filename;
    auto dialog = new QFileDialog (parent, _("Save Preset File"));

    dialog->setAttribute (Qt::WA_DeleteOnClose);
    dialog->setAcceptMode (QFileDialog::AcceptSave);
    dialog->setFileMode (QFileDialog::AnyFile);
    dialog->setLabelText (QFileDialog::Accept, _("Save"));
    dialog->setNameFilter (_(name_filter));
    dialog->setWindowRole("file-dialog");

    /* JWT:DON'T USE DEFAULT NATIVE DIALOG IF DARK THEME OR ICON-THEME IS SET (WILL IGNORE DARK THEME/ICONS)!: */
    int use_native_sysdialogs = aud_get_int ("audqt", "use_native_sysdialogs");
    String icon_theme = aud_get_str ("audqt", "icon_theme");
    if (use_native_sysdialogs == 2 || (use_native_sysdialogs == 1
            && (!strcmp (aud_get_str ("audqt", "theme"), "dark") || (icon_theme && icon_theme[0]))))
        dialog->setOption(QFileDialog::DontUseNativeDialog);

    /* TODO: replace other illegal characters on Win32 */

    /* JWT:OUR PRESET-FILENAME FILLIN CODE */
    int current_playlist = aud_playlist_get_playing ();
    bool dialog_set = false;
    aud_set_str (nullptr, "_eq_last_preset_filename", "");
    aud_set_str (nullptr, "_preset_dir", aud_get_path (AudPath::UserDir));
    dialog->setDirectory (QString (aud_get_path (AudPath::UserDir)));
    if (preset.name)  // AUDACIOUS 3.10+ SAYS SAVE USING SELECTED PRESET NAME, IF ONE SELECTED!:
    {
        auto safe = QString (preset.name).replace (QLatin1Char ('/'), QLatin1Char ('_'));
        dialog->selectFile (safe + ".preset");
        dialog_set = true;
    }
    else if (current_playlist >= 0)  // JWT:NONE SELECTED, CALCULATE THE EXPORT NAME:
    {
        int current_song = aud_playlist_get_position (current_playlist);
        if (current_song >= 0)
        {
            if (aud_get_bool (nullptr, "try_local_preset_files") && aud_get_bool (nullptr, "_save_as_dirdefault"))
            {
                String eqpreset_dir_default_file = aud_get_str (nullptr, "eqpreset_dir_default_file");
                if (eqpreset_dir_default_file && eqpreset_dir_default_file[0])
                {
                    filename = aud_playlist_entry_get_filename (current_playlist, current_song);
                    StringBuf scheme = uri_get_scheme (filename);
                    if (! strcmp (scheme, "file"))  // JWT:WE'RE A "FILE" AND SAVE TO LOCAL DIR, AND SAVE AS DIR-PRESET:
                    {
                        StringBuf path = filename_get_parent (uri_to_filename (filename));
                        aud_set_str (nullptr, "_preset_dir", (const char *) path);
                        dialog->setDirectory (QString ((const char *) path));
                        String preset_file_namepart = eqpreset_dir_default_file;
                        aud_set_str (nullptr, "_eq_last_preset_filename", String (filename_to_uri
                                (str_concat ({aud_get_str (nullptr, "_preset_dir"), "/",
                                preset_file_namepart}))));
                        auto safe = QString (preset_file_namepart).replace (QLatin1Char ('/'), QLatin1Char ('_'));
                        dialog->selectFile (safe);
                        dialog_set = true;
                    }
                }
            }
            if (! dialog_set)
            {
                const char * slash;
                const char * base;
                filename = aud_playlist_entry_get_filename (current_playlist, current_song);
                const char * dross = aud_get_bool (nullptr, "eqpreset_nameonly") ? strstr (filename, "?") : nullptr;
                int ln = -1;
                StringBuf scheme = uri_get_scheme (filename);
                if (aud_get_bool (nullptr, "eqpreset_use_url_sitename")
                        && strcmp (scheme, "file") && strcmp (scheme, "stdin")
                        && strcmp (scheme, "cdda") && strcmp (scheme, "dvd"))
                {
                    /* WE'RE A URL AND USER WANTS TO SAVE PRESETFILE FOR THE BASE SITE NAME, IE. "www.site.com": */
                    slash = strstr (filename, "//");
                    if (slash)
                    {
                        slash+=2;
                        const char * endbase = strstr (slash, "/");
                        ln = endbase ? endbase - slash : -1;
                        String urlbase = String (str_copy (slash, ln));
                        auto split = str_list_to_index (slash, "?&#:/");
                        for (auto & str : split)
                        {
                            urlbase = String (str_copy (str));
                            break;
                        }
                        String preset_file_namepart = String (str_concat ({urlbase, ".preset"}));
                        aud_set_str (nullptr, "_eq_last_preset_filename", String (filename_to_uri
                                (str_concat ({aud_get_path (AudPath::UserDir), "/",
                                preset_file_namepart}))));
                        auto safe = QString (preset_file_namepart).replace (QLatin1Char ('/'), QLatin1Char ('_'));
                        dialog->selectFile (safe);
                        dialog_set = true;
                    }
                }
                if (! dialog_set)
                {
                    /* JWT: EXTRACT JUST THE "NAME" PART (URLs MAY END W/"/") TO USE TO NAME THE EQ. FILE: */
                    slash = filename ? strrchr (filename, '/') : nullptr;
                    if (slash && dross && slash > dross)
                    {
                        slash = dross;
                        while (slash > filename && slash[0] != '/')
                        {
                            --slash;
                        }
                        if (slash[0] != '/')
                            slash = nullptr;
                    }
                    base = slash ? slash + 1 : nullptr;
                    if (slash && (!base || base[0] == '\0'))  // FILENAME (URL) ENDS IN A "/"!
                    {
                        do
                        {
                            --slash;
                            ++ln;
                        } while (slash && slash > filename && slash[0] != '/');
                        base = slash ? slash + 1 : (const char *) filename;
                        if (ln > 0)
                        {
                            String preset_file_namepart = String (str_concat ({str_encode_percent (base, ln), ".preset"}));
                            aud_set_str (nullptr, "_eq_last_preset_filename", String (filename_to_uri
                                    (str_concat ({aud_get_path (AudPath::UserDir), "/",
                                    preset_file_namepart}))));
                            auto safe = QString (preset_file_namepart).replace (QLatin1Char ('/'), QLatin1Char ('_'));
                            dialog->selectFile (safe);
                            dialog_set = true;
                        }
                    }
                    else if (base && base[0] != '\0' && strncmp (base, "-.", 2))
                    {
                        const char * iscue = dross ? dross : strstr (filename, "?");
                        StringBuf scheme = uri_get_scheme (filename);
                        if (aud_get_bool (nullptr, "try_local_preset_files") && ! strcmp (scheme, "file"))
                        {
                            StringBuf path = filename_get_parent (uri_to_filename (filename));
                            aud_set_str (nullptr, "_preset_dir", (const char *) path);
                            dialog->setDirectory (QString ((const char *) path));
                        }
                        if (iscue && base[0] != '?')  // WE'RE A CUE-SHEET FILE:
                        {
                            /* JWT:SONGS FROM CUE FILES HAVE A TRAILING "?<cue#>" THAT'S NOT ON THE FILENAME IN output.cc
                                SO WE HAVE TO STRIP IT OFF THE "filename" HERE, BUT ONLY IF WE'RE A "file://..." SCHEME,
                                LEST WE WHACK OFF A URL LIKE "https://www.youtube.com/watch?t=4&v=BaW_jenozKc"!
                                THE DRAWBACK W/THIS IS THAT ALL CUES OF THE SAME BASE FILE NAME WILL HAVE THE SAME
                                EQ. PRESET, BUT THE ALTERNATIVE IS THAT EQ. PRESETS WON'T WORK AT ALL FOR CUE-BASED
                                FILES, SINCE WE DON'T SEEM TO HAVE THE <cue#> IN output.cc for output_open_audio()!
                            */
                            if (dross || ! strcmp (scheme, "file"))
                            {
                                int ln = iscue - base;
                                String preset_file_namepart = String (str_concat ({str_encode_percent (base, ln), ".preset"}));
                                aud_set_str (nullptr, "_eq_last_preset_filename", String (filename_to_uri
                                        (str_concat ({aud_get_str (nullptr, "_preset_dir"), "/",
                                        preset_file_namepart}))));
                                auto safe = QString (preset_file_namepart).replace (QLatin1Char ('/'), QLatin1Char ('_'));
                                dialog->selectFile (safe);
                                dialog_set = true;
                            }
                        }
                        if (! dialog_set)
                        {
                            String preset_file_namepart = String (str_concat ({str_encode_percent (base, -1), ".preset"}));
                            if (! strcmp (scheme, "cdda") || ! strcmp (scheme, "dvd"))
                            {
                                String playingdiskid = aud_get_str (nullptr, "playingdiskid");
                                if (playingdiskid[0])
                                    preset_file_namepart = String (str_concat ({playingdiskid, ".preset"}));
                            }
                            aud_set_str (nullptr, "_eq_last_preset_filename", String (filename_to_uri
                                    (str_concat ({aud_get_str (nullptr, "_preset_dir"), "/",
                                    preset_file_namepart}))));
                            auto safe = QString (preset_file_namepart).replace (QLatin1Char ('/'), QLatin1Char ('_'));
                            dialog->selectFile (safe);
                            dialog_set = true;
                        }
                    }
                }
            }
        }
    }
    if (! dialog_set)
    {
        dialog->selectFile ("<NAMEME!>.preset");
    }
    /* JWT:END OUR PRESET-FILENAME FILLIN CODE */

    QObject::connect (dialog, & QFileDialog::accepted, [dialog, preset] () {
        auto urls = dialog->selectedUrls ();
        if (urls.size () != 1)
            return;

        auto filename = urls[0].toEncoded ();

        if (export_file (filename, preset))
            dialog->deleteLater ();
        else
            aud_ui_show_error (str_printf (_("Error saving %s."), filename.constData ()));
    });

    window_bring_to_front (dialog);
}

static QDialog * create_preset_win ()
{
    auto win = new QDialog;
    win->setAttribute (Qt::WA_DeleteOnClose);
    win->setWindowTitle (_("Equalizer Presets - Fauxdacious"));
    win->setContentsMargins (margins.TwoPt);

    auto edit = new QLineEdit;
    auto save_btn = new QPushButton (_("Save Preset"));
    save_btn->setIcon (get_icon ("document-save"));
    save_btn->setDisabled (true);

    auto hbox = make_hbox (nullptr);
    hbox->addWidget (edit);
    hbox->addWidget (save_btn);

    /* JWT:ADD OUR CHECKBOXES: */
    auto localsave_checkbox = new QCheckBox (_("Use entry's local directory for preset files, if file."));
    auto save_as_dirdefault_checkbox = new QCheckBox (_("Save as Directory-wide Preset, IF file & above box checked."));
    auto urlbase_checkbox = new QCheckBox (_("Use only base site name for URLs."));
    auto nameonly_checkbox = new QCheckBox (_("Exclude arg. lists from URLs to create preset filename."));
    auto save_effects2_checkbox = new QCheckBox (_("Save Effects (plugins) with presets."));
    /* JWT:END ADD OUR CHECKBOXES: */

    auto import_btn = new QPushButton (_("Import"));
    import_btn->setIcon (get_icon ("document-open"));

    auto export_btn = new QPushButton (_("Export"));
    export_btn->setIcon (get_icon ("document-save"));

    auto view = new PresetView (export_btn);

    auto hbox2 = make_hbox (nullptr);
    hbox2->addWidget (import_btn);
    hbox2->addWidget (export_btn);
    hbox2->addStretch (1);

    auto remove_btn = new QPushButton (_("Delete Selected"));
    remove_btn->setIcon (get_icon ("edit-delete"));
    // remove_btn->setDisabled (true);

    auto revert_btn = new QPushButton (_("Revert Changes"));
    revert_btn->setIcon (get_icon ("edit-undo"));
    revert_btn->setDisabled (true);

    auto close_btn = new QPushButton (_("Close"));
    close_btn->setIcon (get_icon ("window-close"));

    auto hbox3 = make_hbox (nullptr);
    hbox3->addWidget (remove_btn);
    hbox3->addStretch (1);
    hbox3->addWidget (revert_btn);
    hbox3->addWidget (close_btn);

    auto vbox = make_vbox (win);
    vbox->addLayout (hbox);
    /* JWT:OUR CHECKBOXES: */
    vbox->addWidget (localsave_checkbox);
    String eqpreset_dir_default_file = aud_get_str (nullptr, "eqpreset_dir_default_file");
    if (eqpreset_dir_default_file && eqpreset_dir_default_file[0])
        vbox->addWidget (save_as_dirdefault_checkbox);
    else
        aud_set_bool (nullptr, "_save_as_dirdefault", false);
    vbox->addWidget (urlbase_checkbox);
    vbox->addWidget (nameonly_checkbox);
    bool eqpreset_use_effects = aud_get_bool (nullptr, "eqpreset_use_effects");
    if (eqpreset_use_effects)
        vbox->addWidget (save_effects2_checkbox);
    else
        aud_set_bool (nullptr, "eqpreset_save_effects", false);
    /* JWT:END OUR CHECKBOXES */
    vbox->addWidget (view);
    vbox->addLayout (hbox2);
    vbox->addLayout (hbox3);

    auto pmodel = view->pmodel ();

    /* JWT:OUR CHECKBOXES: */
    localsave_checkbox->setCheckState (aud_get_bool (nullptr, "try_local_preset_files") 
            ? Qt::Checked : Qt::Unchecked);
    QObject::connect (localsave_checkbox, & QCheckBox::stateChanged, [] (int state) {
        aud_set_bool (nullptr, "try_local_preset_files", (state == Qt::Checked));
    });

    if (eqpreset_dir_default_file && eqpreset_dir_default_file[0])
    {
        save_as_dirdefault_checkbox->setCheckState (aud_get_bool (nullptr, "_save_as_dirdefault") 
                ? Qt::Checked : Qt::Unchecked);
        QObject::connect (save_as_dirdefault_checkbox, & QCheckBox::stateChanged, [] (int state) {
            aud_set_bool (nullptr, "_save_as_dirdefault", (state == Qt::Checked));
        });
    }

    urlbase_checkbox->setCheckState (aud_get_bool (nullptr, "eqpreset_use_url_sitename")
            ? Qt::Checked : Qt::Unchecked);
    QObject::connect (urlbase_checkbox, & QCheckBox::stateChanged, [] (int state) {
        aud_set_bool (nullptr, "eqpreset_use_url_sitename", (state == Qt::Checked));
    });

    nameonly_checkbox->setCheckState (aud_get_bool (nullptr, "eqpreset_nameonly") 
            ? Qt::Checked : Qt::Unchecked);
    QObject::connect (nameonly_checkbox, & QCheckBox::stateChanged, [] (int state) {
        aud_set_bool (nullptr, "eqpreset_nameonly", (state == Qt::Checked));
    });

    if (eqpreset_use_effects)
    {
        save_effects2_checkbox->setCheckState (aud_get_bool (nullptr, "eqpreset_save_effects") 
                ? Qt::Checked : Qt::Unchecked);
        QObject::connect (save_effects2_checkbox, & QCheckBox::stateChanged, [] (int state) {
            aud_set_bool (nullptr, "OUR CHECKBOXES", (state == Qt::Checked));
        });
    }

    /* JWT:END OUR CHECKBOXES */

    QObject::connect (edit, & QLineEdit::textChanged, [save_btn] (const QString & text) {
        save_btn->setEnabled (! text.isEmpty ());
    });

    QObject::connect (save_btn, & QPushButton::clicked, [view, pmodel, edit, revert_btn] () {
        auto added = pmodel->add_preset (edit->text ().toUtf8 ());
        view->setCurrentIndex (added);
        pmodel->save_all();
        revert_btn->setDisabled (false);
    });

    QObject::connect (import_btn, & QPushButton::clicked,
                     [win, view, revert_btn]() {
                         show_import_dialog(win, view, revert_btn);
                     }
    );

    QObject::connect (export_btn, & QPushButton::clicked, [win, view] () {
        auto preset = view->preset_for_export ();
        // JWT:USE CURRENT INSTEAD OF FAIL:  g_return_if_fail (preset);
        EqualizerPreset current_preset;
        if (preset == nullptr)
        {
            aud_eq_update_preset (current_preset);
            preset = & current_preset;
        }
        if (preset != nullptr)
            show_export_dialog (win, * preset);
    });

    QObject::connect(pmodel, &PresetModel::rowsRemoved, [pmodel, revert_btn] () {
        pmodel->save_all ();
        revert_btn->setDisabled (false);
    });

    QObject::connect (revert_btn, & QPushButton::clicked, [pmodel, revert_btn] () {
        pmodel->revert_all();
        pmodel->save_all();
        revert_btn->setDisabled (true);
    });

    QObject::connect (remove_btn, & QPushButton::clicked, [win, view] () {
        view->removeSelRows ();
    });

    QObject::connect (close_btn, & QPushButton::clicked, win, & QObject::deleteLater);

    return win;
}

static QPointer<QDialog> s_preset_win;

EXPORT void eq_presets_show ()
{
    if (! s_preset_win)
        s_preset_win = create_preset_win ();

    window_bring_to_front (s_preset_win);
}

EXPORT void eq_presets_hide ()
{
    delete s_preset_win;
}

} // namespace audqt
