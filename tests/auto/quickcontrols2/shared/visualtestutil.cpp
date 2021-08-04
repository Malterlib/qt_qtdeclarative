/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the test suite of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL3$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "visualtestutil.h"

#include <QtQuick/QQuickItem>
#include <QtCore/QDebug>
#include <QtGui/QCursor>
#include <QtCore/QCoreApplication>
#include <QtQml/QQmlFile>
#include <QtTest/qsignalspy.h>
#include <QtTest/QTest>

bool QQuickVisualTestUtil::delegateVisible(QQuickItem *item)
{
    return item->isVisible() && !QQuickItemPrivate::get(item)->culled;
}

QQuickItem *QQuickVisualTestUtil::findVisibleChild(QQuickItem *parent, const QString &objectName)
{
    QQuickItem *item = 0;
    QList<QQuickItem*> items = parent->findChildren<QQuickItem*>(objectName);
    for (int i = 0; i < items.count(); ++i) {
        if (items.at(i)->isVisible() && !QQuickItemPrivate::get(items.at(i))->culled) {
            item = items.at(i);
            break;
        }
    }
    return item;
}

void QQuickVisualTestUtil::dumpTree(QQuickItem *parent, int depth)
{
    static QString padding("                       ");
    for (int i = 0; i < parent->childItems().count(); ++i) {
        QQuickItem *item = qobject_cast<QQuickItem*>(parent->childItems().at(i));
        if (!item)
            continue;
        qDebug() << padding.left(depth*2) << item;
        dumpTree(item, depth+1);
    }
}

void QQuickVisualTestUtil::moveMouseAway(QQuickWindow *window)
{
#if QT_CONFIG(cursor) // Get the cursor out of the way.
    // Using "bottomRight() + QPoint(100, 100)" was causing issues on Ubuntu,
    // where the window was positioned at the bottom right corner of the window
    // (even after centering the window on the screen), so we use another position.
    QCursor::setPos(window->geometry().bottomLeft() + QPoint(0, 10));
#endif

    // make sure hover events from QQuickWindowPrivate::flushFrameSynchronousEvents()
    // do not interfere with the tests
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(window, &leave);
}

void QQuickVisualTestUtil::centerOnScreen(QQuickWindow *window)
{
    const QRect screenGeometry = window->screen()->availableGeometry();
    const QPoint offset = QPoint(window->width() / 2, window->height() / 2);
    window->setFramePosition(screenGeometry.center() - offset);
}

/*!
    \internal

    Finds the delegate at \c index belonging to \c itemView, using the given \c flags.

    If the view needs to be polished, the function will wait for it to be done before continuing,
    and returns \c nullptr if the polish didn't happen.
*/
QQuickItem *QQuickVisualTestUtil::findViewDelegateItem(QQuickItemView *itemView, int index, FindViewDelegateItemFlags flags)
{
    if (QQuickTest::qIsPolishScheduled(itemView)) {
        if (!QQuickTest::qWaitForItemPolished(itemView)) {
            qWarning() << "failed to polish" << itemView;
            return nullptr;
        }
    }

    // Do this after the polish, just in case the count changes after a polish...
    if (index <= -1 || index >= itemView->count()) {
        qWarning() << "index" << index << "is out of bounds for" << itemView;
        return nullptr;
    }

    if (flags.testFlag(FindViewDelegateItemFlag::PositionViewAtIndex))
        itemView->positionViewAtIndex(index, QQuickItemView::Center);

    return itemView->itemAtIndex(index);
}

void QQuickVisualTestUtil::forEachControl(QQmlEngine *engine, const QString &sourcePath, const QString &targetPath, const QStringList &skipList, QQuickVisualTestUtil::ForEachCallback callback)
{
    // We cannot use QQmlComponent to load QML files directly from the source tree.
    // For styles that use internal QML types (eg. material/Ripple.qml), the source
    // dir would be added as an "implicit" import path overriding the actual import
    // path (qtbase/qml/QtQuick/Controls.2/Material). => The QML engine fails to load
    // the style C++ plugin from the implicit import path (the source dir).
    //
    // Therefore we only use the source tree for finding out the set of QML files that
    // a particular style implements, and then we locate the respective QML files in
    // the engine's import path. This way we can use QQmlComponent to load each QML file
    // for benchmarking.

    const QFileInfoList entries = QDir(QQC2_IMPORT_PATH "/" + sourcePath).entryInfoList(QStringList("*.qml"), QDir::Files);
    for (const QFileInfo &entry : entries) {
        QString name = entry.baseName();
        if (!skipList.contains(name)) {
            const auto importPathList = engine->importPathList();
            for (const QString &importPath : importPathList) {
                QString name = entry.dir().dirName() + "/" + entry.fileName();
                QString filePath = importPath + "/" + targetPath + "/" + entry.fileName();
                if (filePath.startsWith(":"))
                    filePath.prepend("qrc");
                if (QFile::exists(filePath)) {
                    callback(name, QUrl::fromLocalFile(filePath));
                    break;
                } else {
                    QUrl url(filePath);
                    filePath = QQmlFile::urlToLocalFileOrQrc(filePath);
                    if (!filePath.isEmpty() && QFile::exists(filePath)) {
                        callback(name, url);
                        break;
                    }
                }
            }
        }
    }
}

void QQuickVisualTestUtil::addTestRowForEachControl(QQmlEngine *engine, const QString &sourcePath, const QString &targetPath, const QStringList &skipList)
{
    forEachControl(engine, sourcePath, targetPath, skipList, [&](const QString &relativePath, const QUrl &absoluteUrl) {
        QTest::newRow(qPrintable(relativePath)) << absoluteUrl;
    });
}

QQuickVisualTestUtil::MnemonicKeySimulator::MnemonicKeySimulator(QWindow *window)
    : m_window(window), m_modifiers(Qt::NoModifier)
{
}

void QQuickVisualTestUtil::MnemonicKeySimulator::press(Qt::Key key)
{
    // QTest::keyPress() but not generating the press event for the modifier key.
    if (key == Qt::Key_Alt)
        m_modifiers |= Qt::AltModifier;
    QTest::simulateEvent(m_window, true, key, m_modifiers, QString(), false);
}

void QQuickVisualTestUtil::MnemonicKeySimulator::release(Qt::Key key)
{
    // QTest::keyRelease() but not generating the release event for the modifier key.
    if (key == Qt::Key_Alt)
        m_modifiers &= ~Qt::AltModifier;
    QTest::simulateEvent(m_window, false, key, m_modifiers, QString(), false);
}

void QQuickVisualTestUtil::MnemonicKeySimulator::click(Qt::Key key)
{
    press(key);
    release(key);
}


bool QQuickVisualTestUtil::verifyButtonClickable(QQuickAbstractButton *button)
{
    if (!button->window()) {
        qWarning() << "button" << button << "doesn't have an associated window";
        return false;
    }

    if (!button->isEnabled()) {
        qWarning() << "button" << button << "is not enabled";
        return false;
    }

    if (!button->isVisible()) {
        qWarning() << "button" << button << "is not visible";
        return false;
    }

    if (button->width() <= 0.0) {
        qWarning() << "button" << button << "must have a width greater than 0";
        return false;
    }

    if (button->height() <= 0.0) {
        qWarning() << "button" << button << "must have a height greater than 0";
        return false;
    }

    return true;
}

bool QQuickVisualTestUtil::clickButton(QQuickAbstractButton *button)
{
    if (!verifyButtonClickable(button))
        return false;

    QSignalSpy spy(button, &QQuickAbstractButton::clicked);
    if (!spy.isValid()) {
        qWarning() << "button" << button << "must have a valid clicked signal";
        return false;
    }

    const QPoint buttonCenter = button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint();
    QTest::mouseClick(button->window(), Qt::LeftButton, Qt::NoModifier, buttonCenter);
    if (spy.count() != 1) {
        qWarning() << "clicked signal of button" << button << "was not emitted after clicking";
        return false;
    }

    return true;
}

bool QQuickVisualTestUtil::doubleClickButton(QQuickAbstractButton *button)
{
    if (!verifyButtonClickable(button))
        return false;

    QSignalSpy spy(button, &QQuickAbstractButton::clicked);
    if (!spy.isValid()) {
        qWarning() << "button" << button << "must have a valid doubleClicked signal";
        return false;
    }

    const QPoint buttonCenter = button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint();
    QTest::mouseDClick(button->window(), Qt::LeftButton, Qt::NoModifier, buttonCenter);
    if (spy.count() != 1) {
        qWarning() << "doubleClicked signal of button" << button << "was not emitted after double-clicking";
        return false;
    }

    return true;
}