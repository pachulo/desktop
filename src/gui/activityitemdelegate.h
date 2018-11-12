/*
 * Copyright (C) by Klaas Freitag <freitag@kde.org>
 * Copyright (C) by Olivier Goffart <ogoffart@woboq.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#pragma once
#include <QStyledItemDelegate>
#include <QMouseEvent>

class QMouseEvent;

namespace OCC {

/**
 * @brief The ActivityItemDelegate class
 * @ingroup gui
 */
class ActivityItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    enum datarole { ActionIconRole = Qt::UserRole + 1,
        UserIconRole,
        AccountRole,
        ObjectTypeRole,
        ActionsLinksRole,
        ActionTextRole,
        ActionRole,
        MessageRole,
        PathRole,
        LinkRole,
        PointInTimeRole,
        AccountConnectedRole,
        SyncFileStatusRole };

    void paint(QPainter *, const QStyleOptionViewItem &, const QModelIndex &) const override;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
        const QModelIndex &index) override;

    static int rowHeight();
    static int iconHeight();

signals:
    void primaryButtonClickedOnItemView(const QModelIndex &index);
    void secondaryButtonClickedOnItemView(const QModelIndex &index);

private:
    static int _margin;
    static int _iconHeight;
    static int _primaryButtonWidth;
    static int _secondaryButtonWidth;
    static int _spaceBetweenButtons;
    static int _timeWidth;
    static int _buttonHeight;
    static const QString _remote_share;
    static const QString _call;
};

} // namespace OCC
