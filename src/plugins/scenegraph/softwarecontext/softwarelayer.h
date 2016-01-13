/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt Quick 2D Renderer module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 or (at your option) any later version
** approved by the KDE Free Qt Foundation. The licenses are as published by
** the Free Software Foundation and appearing in the file LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef SOFTWARELAYER_H
#define SOFTWARELAYER_H

#include <private/qsgadaptationlayer_p.h>
#include <private/qsgcontext_p.h>

namespace SoftwareContext {
class PixmapRenderer;
}

class SoftwareLayer : public QSGLayer
{
    Q_OBJECT
public:
    SoftwareLayer(QSGRenderContext *renderContext);
    ~SoftwareLayer();

    const QPixmap &pixmap() const { return m_pixmap; }

    // QSGTexture interface
public:
    virtual int textureId() const;
    virtual QSize textureSize() const;
    virtual bool hasAlphaChannel() const;
    virtual bool hasMipmaps() const;
    virtual void bind();

    // QSGDynamicTexture interface
public:
    virtual bool updateTexture();

    // QSGLayer interface
public:
    virtual void setItem(QSGNode *item);
    virtual void setRect(const QRectF &rect);
    virtual void setSize(const QSize &size);
    virtual void scheduleUpdate();
    virtual QImage toImage() const;
    virtual void setLive(bool live);
    virtual void setRecursive(bool recursive);
    virtual void setFormat(GLenum);
    virtual void setHasMipmaps(bool);
    virtual void setDevicePixelRatio(qreal ratio);
    virtual void setMirrorHorizontal(bool mirror);
    virtual void setMirrorVertical(bool mirror);

public slots:
    virtual void markDirtyTexture();
    virtual void invalidated();

private:
    void grab();

    QSGNode *m_item;
    QSGRenderContext *m_context;
    SoftwareContext::PixmapRenderer *m_renderer;
    QRectF m_rect;
    QSize m_size;
    QPixmap m_pixmap;
    qreal m_device_pixel_ratio;
    bool m_mirrorHorizontal;
    bool m_mirrorVertical;
    bool m_live;
    bool m_grab;
    bool m_recursive;
    bool m_dirtyTexture;
};

#endif // SOFTWARELAYER_H
