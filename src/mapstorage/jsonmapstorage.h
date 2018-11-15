#pragma once
/************************************************************************
**
** Authors:   Ulf Hermann <ulfonk_mennhar@gmx.de> (Alve),
**            Marek Krejza <krejza@gmail.com> (Caligor),
**            Nils Schimmelmann <nschimme@gmail.com> (Jahara)
**
** This file is part of the MMapper project.
** Maintained by Nils Schimmelmann <nschimme@gmail.com>
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the:
** Free Software Foundation, Inc.
** 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**
************************************************************************/

#ifndef JSONMAPSTORAGE_H
#define JSONMAPSTORAGE_H

#include <QString>
#include <QtCore>

#include "abstractmapstorage.h"

class MapData;
class QJsonArray;
class QJsonObject;
class QObject;

/*! \brief JSON export for web clients.
 *
 * This saves to a directory the following files:
 * - v1/arda.json (global metadata like map size).
 * - v1/roomindex/ss.json (room sums -> zone coords).
 * - v1/zone/xx-yy.json (full info on the NxN rooms zone at coords xx,yy).
 */
class JsonMapStorage : public AbstractMapStorage
{
    Q_OBJECT

public:
    explicit JsonMapStorage(MapData &, const QString &, QObject *parent = nullptr);
    ~JsonMapStorage() override;

public:
    explicit JsonMapStorage() = delete;

private:
    virtual bool canLoad() const override { return false; }
    virtual bool canSave() const override { return true; }

    virtual void newData() override;
    virtual bool loadData() override;
    virtual bool saveData(bool baseMapOnly) override;
    virtual bool mergeData() override;

    // void saveMark(InfoMark * mark, QJsonObject &jRoom, const JsonRoomIdsCache &jRoomIds);
};

#endif
