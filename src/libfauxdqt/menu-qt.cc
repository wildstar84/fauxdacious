/*
 * menu.h
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

#include "libfauxdqt.h"
#include "libfauxdqt/menu.h"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>

#include <libfauxdcore/hook.h>
#include <libfauxdcore/runtime.h>

namespace audqt {

class MenuAction : public QAction
{
public:
    MenuAction (const MenuItem & item, const char * domain, QWidget * parent);

private:
    void toggle (bool checked);
    void update ();

    const MenuItem & m_item;
    HookReceiver<MenuAction> m_hook{this, &MenuAction::update};
};

MenuAction::MenuAction (const MenuItem & item, const char * domain, QWidget * parent) :
    QAction (parent),
    m_item (item)
{
    if (item.sep)
    {
        setSeparator (true);
        return;
    }

    setText (translate_str (item.text.name, domain));

    /* JWT:DON'T APPLY MAIN MENU SHORTCUTS TO OTHER WINDOWS! (ie. Ctrl-C)!!
       (WE DO NOT WANT GLOBAL APPLICATION-WIDE SHORTCUTS B/C PLUGIN WINDOWS OFTEN
       HAVE THEIR OWN KEYBOARD SHORTCUTS THAT WE DON'T WANT OVERRIDDEN,
       IE. playback-history-qt, WHICH USES Ctrl-A & Ctrl-C WHICH WERE GETTING
       "EATEN" BY THE MAIN PLAYLIST WINDOW'S CONTEXT MENU!:
    */
    setShortcutContext (Qt::WidgetShortcut);  // JWT:SEE https://doc.qt.io/qt-6/qt.html#ShortcutContext-enum

    if (item.cfg.name)
    {
        setCheckable (true);
        setChecked (aud_get_bool (item.cfg.sect, item.cfg.name));

        QObject::connect (this, & QAction::toggled, this, & MenuAction::toggle);

        if (item.cfg.hook)
            m_hook.connect (item.cfg.hook);
    }
    else if (item.func)
        QObject::connect (this, & QAction::triggered, item.func);
    else if (item.items.len)
        setMenu (menu_build (item.items, domain, parent));
    else if (item.submenu)
        setMenu (item.submenu ());

#ifndef Q_OS_MAC
    if (item.text.icon)
        setIcon (audqt::get_icon (item.text.icon));
#endif

    if (item.text.shortcut)
        setShortcut (QString (item.text.shortcut));

    if (parent)
        parent->addAction (this);
}

void MenuAction::toggle (bool checked)
{
    if (aud_get_bool (m_item.cfg.sect, m_item.cfg.name) != checked)
    {
        aud_set_bool (m_item.cfg.sect, m_item.cfg.name, checked);

        if (m_item.func)
            m_item.func ();
    }
}

void MenuAction::update ()
{
    setChecked (aud_get_bool (m_item.cfg.sect, m_item.cfg.name));
}

EXPORT QAction * menu_action (const MenuItem & menu_item, const char * domain, QWidget * parent)
{
    return new MenuAction (menu_item, domain, parent);
}

EXPORT QMenu * menu_build (ArrayRef<MenuItem> menu_items, const char * domain, QWidget * parent)
{
    QMenu * m = new QMenu (parent);

    for (auto & it : menu_items)
        m->addAction (new MenuAction (it, domain, parent));

    return m;
}

EXPORT QMenuBar * menubar_build (ArrayRef<MenuItem> menu_items, const char * domain, QWidget * parent)
{
#ifdef Q_OS_MAC
    QMenuBar * m = new QMenuBar (nullptr);
#else
    QMenuBar * m = new QMenuBar (parent);
    m->setContextMenuPolicy (Qt::PreventContextMenu);
#endif

    for (auto & it : menu_items)
        m->addAction (new MenuAction (it, domain, parent));

    return m;
}

} // namespace audqt
