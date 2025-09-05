/*
 * info-widget.h
 * Copyright 2006-2014 William Pitcock, Tomasz Moń, Eugene Zagidullin,
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

#ifndef LIBAUDQT_INFO_WIDGET_H
#define LIBAUDQT_INFO_WIDGET_H

#include <QTreeView>
#include <libfauxdqt/export.h>

class PluginHandle;
class Tuple;

namespace audqt {

class InfoModel;

class LIBAUDQT_PUBLIC InfoWidget : public QTreeView
{
public:
    InfoWidget (QWidget * parent = nullptr);
    ~InfoWidget ();

    void fillInfo (int playlist, int entry, const char * filename, const Tuple & tuple,
     PluginHandle * decoder, bool updating_enabled);
    void linkEnabled (QWidget * widget);
    bool updateFile ();
    void show_coverart_dialog (QDialog * parent);
    bool setData (const QModelIndex & index, const QVariant & value, int role);
    QModelIndex createModelIndex (int row, int column);
    InfoModel * m_model;

protected:
    void keyPressEvent (QKeyEvent * event) override;
};

static QStringList comboItems;
} // namespace audqt

#endif // LIBAUDQT_INFO_WIDGET_H
