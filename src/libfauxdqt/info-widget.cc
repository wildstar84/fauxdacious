/*
 * info-widget.h
 * Copyright 2006-2022 René Bertin, Thomas Lange, John Lindgren,
 *                     Ariadne Conill, Tomasz Moń, and Eugene Zagidullin
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

#include "info-widget.h"
#include "libfauxdqt-internal.h"
#include "libfauxdqt.h"

#include <QHeaderView>
#include <QKeyEvent>
#include <QPointer>

#include <libfauxdcore/i18n.h>
#include <libfauxdcore/probe.h>
#include <libfauxdcore/runtime.h>
#include <libfauxdcore/tuple.h>
#include <libfauxdcore/drct.h>
#include <libfauxdcore/vfs.h>
#include <libfauxdcore/interface.h>
#include <libfauxdcore/playlist.h>
#include <libfauxdcore/audstrings.h>
#include <libfauxdcore/runtime.h>
#include <libfauxdcore/hook.h>

namespace audqt {

struct TupleFieldMap {
    const char * name;
    Tuple::Field field;
    bool editable;
};

/* JWT:SEE PROGRAMMER NOTE FURTHER DOWN IF ADDING/REMOVING/REORDERING LIST BELOW!: */
static const TupleFieldMap tuple_field_map[] = {
    {N_("Metadata"), Tuple::Invalid, false},
    {N_("Title"), Tuple::Title, true},
    {N_("Artist"), Tuple::Artist, true},
    {N_("Album"), Tuple::Album, true},
    {N_("Album Artist"), Tuple::AlbumArtist, true},
    {N_("Track Number"), Tuple::Track, true},
    {N_("Genre"), Tuple::Genre, true},
    {N_("Comment"), Tuple::Comment, true},
    {N_("Description"), Tuple::Description, true},
    {N_("Composer"), Tuple::Composer, true},
    {N_("Performer"), Tuple::Performer, true},
    {N_("Recording Year"), Tuple::Year, true},
    {N_("Recording Date"), Tuple::Date, true},
    {N_("Publisher"), Tuple::Publisher, true},
    {N_("Catalog Number"), Tuple::CatalogNum, true},

    {nullptr, Tuple::Invalid, false},
    {N_("Technical"), Tuple::Invalid, false},
    {N_("Length"), Tuple::Length, false},
    {N_("Codec"), Tuple::Codec, false},
    {N_("Quality"), Tuple::Quality, false},
    {N_("Bitrate"), Tuple::Bitrate, false},
    {N_("Channels"), Tuple::Channels, false},
    {N_("MusicBrainz ID"), Tuple::MusicBrainzID, false}
};

static const TupleFieldMap * to_field_map (const QModelIndex & index)
{
    int row = index.row ();
    if (row < 0 || row >= aud::n_elems (tuple_field_map))
        return nullptr;

    return & tuple_field_map[row];
}

static QModelIndex sibling_field_index (const QModelIndex & current, int direction)
{
    QModelIndex index = current;

    while (1)
    {
        index = index.sibling (index.row () + direction, index.column ());
        auto map = to_field_map (index);

        if (! map)
            return QModelIndex ();
        if (map->field != Tuple::Invalid)
            return index;
    }
}

class InfoModel : public QAbstractTableModel
{
public:
    enum {
        Col_Name = 0,
        Col_Value = 1
    };

    InfoModel (QObject * parent = nullptr) :
        QAbstractTableModel (parent) {}

    int rowCount (const QModelIndex & parent) const override
        { return parent.isValid () ? 0 : aud::n_elems (tuple_field_map); }
    int columnCount (const QModelIndex &) const override
        { return 2; }

    QVariant data (const QModelIndex & index, int role) const override;
    bool setData (const QModelIndex & index, const QVariant & value, int role) override;
    QModelIndex createModelIndex (int row, int column);
    Qt::ItemFlags flags (const QModelIndex & index) const override;

    void setTupleData (const Tuple & tuple, String filename, PluginHandle * plugin)
    {
        m_tuple = tuple.ref ();
        m_filename = filename;
        m_plugin = plugin;
        setDirty (false);
    }

    void linkEnabled (QWidget * widget)
    {
        widget->setEnabled (m_dirty);
        m_linked_widgets.append (widget);
    }

    bool updateFile () const;

private:
    void setDirty (bool dirty)
    {
        m_dirty = dirty;
        for (auto & widget : m_linked_widgets)
        {
            if (widget)
                widget->setEnabled (dirty);
        }
    }

    Tuple m_tuple;
    String m_filename;
    PluginHandle * m_plugin = nullptr;
    bool m_dirty = false;
    QList<QPointer<QWidget>> m_linked_widgets;
};

EXPORT InfoWidget::InfoWidget (QWidget * parent) :
    QTreeView (parent),
    m_model (new InfoModel (this))
{
    setModel (m_model);
    header ()->hide ();
    setIndentation (0);
    resizeColumnToContents (0);
    setContextMenuPolicy (Qt::CustomContextMenu);

    connect (this, & QWidget::customContextMenuRequested, [this] (const QPoint & pos)
    {
        auto index = indexAt (pos);
        if (index.column () != InfoModel::Col_Value)
            return;
        auto text = m_model->data (index, Qt::DisplayRole).toString ();
        if (! text.isEmpty ())
            show_copy_context_menu (this, mapToGlobal (pos), text);
    });
}

EXPORT InfoWidget::~InfoWidget () {}

EXPORT void InfoWidget::fillInfo (int playlist, int entry, const char * filename, const Tuple & tuple,
 PluginHandle * decoder, bool updating_enabled)
{
    m_model->setTupleData (tuple, String (filename), decoder);
    reset ();

    setEditTriggers (updating_enabled ? QAbstractItemView::CurrentChanged :
                                        QAbstractItemView::NoEditTriggers);

    auto initial_index = m_model->index (1 /* title */, InfoModel::Col_Value);
    setCurrentIndex (initial_index);
    if (updating_enabled)
        edit (initial_index);
}

EXPORT void InfoWidget::linkEnabled (QWidget * widget)
{
    m_model->linkEnabled (widget);
}

EXPORT bool InfoWidget::updateFile ()
{
    return m_model->updateFile ();
}

/* JWT:POP UP IMAGE FILE SELECTOR WHEN NEW [Art Lookup] BUTTON PRESSED: */
EXPORT void InfoWidget::show_coverart_dialog (QDialog * parent)
{
    const char * name_filter = N_("Image files (*.gif *.jpg *.jpeg *.png)");
    auto dialog = new QFileDialog (parent, _("Select Cover Art File - Fauxdacious"));

    dialog->setAttribute (Qt::WA_DeleteOnClose);
    dialog->setFileMode (QFileDialog::ExistingFile);
    dialog->setLabelText (QFileDialog::Accept, _("Load"));
    dialog->setNameFilter (_(name_filter));
    dialog->setDirectory (QString (aud_get_path (AudPath::UserDir)));

    /* JWT:DON'T USE DEFAULT NATIVE DIALOG IF DARK THEME OR ICON-THEME IS SET (WILL IGNORE DARK THEME/ICONS)!: */
    int use_native_sysdialogs = aud_get_int ("audqt", "use_native_sysdialogs");
    String icon_theme = aud_get_str ("audqt", "icon_theme");
    if (use_native_sysdialogs == 2 || (use_native_sysdialogs == 1
            && (!strcmp (aud_get_str ("audqt", "theme"), "dark") || (icon_theme && icon_theme[0]))))
        dialog->setOption(QFileDialog::DontUseNativeDialog);

    QObject::connect (dialog, & QFileDialog::accepted, [this, dialog, parent] () {
        auto urls = dialog->selectedUrls ();
        String error;

        if (urls.size () != 1)
            return;

        auto coverart_fid = urls[0].toEncoded ();

        if (coverart_fid.length ())
        {
            /* JWT:PROGRAMMER NOTE:  THE 1ST (ZERO-BASED Y) INDEX# (JUST BELOW & 19 LINES BELOW) *MUST* BE
               ADJUSTED WHENEVER THE EDIT-BOX ROWS ARE ADDED TO, REMOVED, OR RE-ORDERED!:
            */
            auto prev_comment = m_model->data (this->createModelIndex (7, 1), Qt::DisplayRole).toString ();
            if (! prev_comment.isEmpty ())
            {
                int album_img_index = prev_comment.indexOf (";file:");
                if (album_img_index >= 0)
                {
                    /* COMMENT FIELD HAS AN ALBUM-ART (2ND) IMAGE: KEEP IT BUT REPLACE
                        WHAT'S BEFORE IT WITH SELECTED IMAGE FILE: */
                    if (album_img_index > 0)
                        prev_comment.remove(0, album_img_index);
                    prev_comment.prepend (coverart_fid);
                }
                else
                    prev_comment = coverart_fid;
            }
            else
                prev_comment = coverart_fid;

            /* JWT:PUT THE IMAGE FILE INTO THE Comment FIELD & UPDATE FIELD AND IMAGE DISPLAYED: */
            m_model->setData (this->createModelIndex (7, 1), prev_comment.toUtf8 ().constData (), Qt::EditRole);
            hook_call ("image change", aud::to_ptr (coverart_fid.constData ()));
            dialog->deleteLater ();

            return;
        }
        else
        {
            aud_ui_show_error (str_printf (_("Error loading %s."), coverart_fid.constData ()));
        }
    });

    window_bring_to_front (dialog);
}

EXPORT bool InfoWidget::setData (const QModelIndex & index, const QVariant & value, int role)
{
    return m_model->setData (index, value, role);
}

EXPORT QModelIndex InfoWidget::createModelIndex (int row, int column)
{
    return m_model->QAbstractTableModel::index (row, column);
}

void InfoWidget::keyPressEvent (QKeyEvent * event)
{
    if (event->type () == QEvent::KeyPress &&
        (event->key () == Qt::Key_Up || event->key () == Qt::Key_Down))
    {
        auto index = sibling_field_index (currentIndex (), (event->key () == Qt::Key_Up) ? -1 : 1);
        if (index.isValid ())
            setCurrentIndex (index);

        event->accept ();
        return;
    }

    QTreeView::keyPressEvent (event);
}

bool InfoModel::updateFile () const
{
    bool success = false;

    if (! m_dirty)
        return true;

    /* JWT:IF RECORDING ON, USE THE FILE BEING RECORDED TO (BUT WILL FAIL OVER TO USER TAG-FILE)! */
    if (aud_get_bool (nullptr, "record"))
    {
        String recording_file = aud_get_str ("filewriter", "_record_fid");
        if (recording_file && recording_file[0])
        {
            AUDDBG ("-infowin_update_tuple: RECORDING ON - SAVE TO (%s)!\n", (const char *) recording_file);
            String error;
            VFSFile file (recording_file, "r");
            PluginHandle * out_plugin = aud_file_find_decoder (recording_file, true, file, & error);

            success = aud_file_write_tuple (recording_file, out_plugin, m_tuple);
        }
    }
    else
        success = aud_file_write_tuple (m_filename, m_plugin, m_tuple);

    return success;
}

bool InfoModel::setData (const QModelIndex & index, const QVariant & value, int role)
{
    if (role != Qt::EditRole)
        return false;

    auto map = to_field_map (index);
    if (! map || map->field == Tuple::Invalid)
        return false;

    auto t = Tuple::field_get_type (map->field);
    auto str = value.toString ();

    bool changed = false;

    if (str.isEmpty ())
    {
        changed = m_tuple.is_set (map->field);
        if (t == Tuple::String)
            m_tuple.set_str (map->field, "");
        else
            m_tuple.set_int (map->field, -1);
        m_tuple.unset (map->field);
    }
    else if (t == Tuple::String)
    {
        auto utf8 = str.toUtf8 ();
        changed = (!m_tuple.is_set (map->field) || m_tuple.get_str (map->field) != utf8);
        m_tuple.set_str (map->field, utf8);
    }
    else /* t == Tuple::Int */
    {
        int val = str.toInt ();
        changed = (!m_tuple.is_set (map->field) || m_tuple.get_int (map->field) != val);
        m_tuple.set_int (map->field, val);
    }

    if (changed)
        setDirty (true);

    emit dataChanged (index, index, {role});
    return true;
}

QVariant InfoModel::data (const QModelIndex & index, int role) const
{
    auto map = to_field_map (index);
    if (! map)
        return QVariant ();

    if (role == Qt::DisplayRole || role == Qt::EditRole)
    {
        if (index.column () == InfoModel::Col_Name)
            return translate_str (map->name);
        else if (index.column () == InfoModel::Col_Value)
        {
            if (map->field == Tuple::Invalid)
                return QVariant ();

            switch (m_tuple.get_value_type (map->field))
            {
            case Tuple::String:
                return QString (m_tuple.get_str (map->field));
            case Tuple::Int:
                /* convert to string so Qt allows clearing the field */
                return QString::number (m_tuple.get_int (map->field));
            default:
                return QVariant ();
            }
        }
    }
    else if (role == Qt::FontRole)
    {
        if (map->field == Tuple::Invalid)
        {
            QFont f;
            f.setBold (true);
            return f;
        }
        return QVariant ();
    }

    return QVariant ();
}

Qt::ItemFlags InfoModel::flags (const QModelIndex & index) const
{
    if (index.column () == InfoModel::Col_Value)
    {
        auto map = to_field_map (index);

        if (! map || map->field == Tuple::Invalid)
            return Qt::ItemNeverHasChildren;
        else if (map->editable)
            return Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled;

        return Qt::ItemIsEnabled;
    }

    return Qt::ItemNeverHasChildren;
}

} // namespace audqt
