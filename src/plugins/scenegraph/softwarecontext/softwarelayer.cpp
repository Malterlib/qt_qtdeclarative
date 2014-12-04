/****************************************************************************
**
** Copyright (C) 2014 Digia Plc
** All rights reserved.
** For any questions to Digia, please use contact form at http://qt.digia.com
**
** This file is part of the Qt SceneGraph Raster Add-on.
**
** $QT_BEGIN_LICENSE$
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.
**
** If you have questions regarding the use of this file, please use
** contact form at http://qt.digia.com
** $QT_END_LICENSE$
**
****************************************************************************/
#include "softwarelayer.h"

#include "context.h"

SoftwareLayer::SoftwareLayer(QSGRenderContext *renderContext)
    : m_item(0)
    , m_context(renderContext)
    , m_renderer(0)
    , m_device_pixel_ratio(1)
    , m_live(true)
    , m_grab(true)
    , m_recursive(false)
    , m_dirtyTexture(true)
{

}

SoftwareLayer::~SoftwareLayer()
{
    invalidated();
}

int SoftwareLayer::textureId() const
{
    return 0;
}

QSize SoftwareLayer::textureSize() const
{
    return m_pixmap.size();
}

bool SoftwareLayer::hasAlphaChannel() const
{
    return m_pixmap.hasAlphaChannel();
}

bool SoftwareLayer::hasMipmaps() const
{
    return false;
}

void SoftwareLayer::bind()
{
}

bool SoftwareLayer::updateTexture()
{
    bool doGrab = (m_live || m_grab) && m_dirtyTexture;
    if (doGrab)
        grab();
    if (m_grab)
        emit scheduledUpdateCompleted();
    m_grab = false;
    return doGrab;
}

void SoftwareLayer::setItem(QSGNode *item)
{
    if (item == m_item)
        return;
    m_item = item;

    if (m_live && !m_item)
        m_pixmap = QPixmap();

    markDirtyTexture();
}

void SoftwareLayer::setRect(const QRectF &rect)
{
    if (rect == m_rect)
        return;
    m_rect = rect;
    markDirtyTexture();
}

void SoftwareLayer::setSize(const QSize &size)
{
    if (size == m_size)
        return;
    m_size = size;

    if (m_live && m_size.isNull())
        m_pixmap = QPixmap();

    markDirtyTexture();
}

void SoftwareLayer::scheduleUpdate()
{
    if (m_grab)
        return;
    m_grab = true;
    if (m_dirtyTexture) {
        emit updateRequested();
    }
}

QImage SoftwareLayer::toImage() const
{
    return m_pixmap.toImage();
}

void SoftwareLayer::setLive(bool live)
{
    if (live == m_live)
        return;
    m_live = live;

    if (m_live && (!m_item || m_size.isNull()))
        m_pixmap = QPixmap();

    markDirtyTexture();
}

void SoftwareLayer::setRecursive(bool recursive)
{
    m_recursive = recursive;
}

void SoftwareLayer::setFormat(GLenum)
{
}

void SoftwareLayer::setHasMipmaps(bool)
{
}

void SoftwareLayer::setDevicePixelRatio(qreal ratio)
{
    m_device_pixel_ratio = ratio;
}

void SoftwareLayer::markDirtyTexture()
{
    m_dirtyTexture = true;
    if (m_live || m_grab) {
        emit updateRequested();
    }
}

void SoftwareLayer::invalidated()
{
    delete m_renderer;
    m_renderer = 0;
}

void SoftwareLayer::grab()
{
    if (!m_item || m_size.isNull()) {
        m_pixmap = QPixmap();
        m_dirtyTexture = false;
        return;
    }
    QSGNode *root = m_item;
    while (root->firstChild() && root->type() != QSGNode::RootNodeType)
        root = root->firstChild();
    if (root->type() != QSGNode::RootNodeType)
        return;

    if (!m_renderer) {
        m_renderer = new SoftwareContext::PixmapRenderer(m_context);
        connect(m_renderer, SIGNAL(sceneGraphChanged()), this, SLOT(markDirtyTexture()));
    }
    m_renderer->setDevicePixelRatio(m_device_pixel_ratio);
    m_renderer->setRootNode(static_cast<QSGRootNode *>(root));

    if (m_pixmap.size() != m_size)
        m_pixmap = QPixmap(m_size);

    // Render texture.
    root->markDirty(QSGNode::DirtyForceUpdate); // Force matrix, clip and opacity update.
    m_renderer->nodeChanged(root, QSGNode::DirtyForceUpdate); // Force render list update.

    m_dirtyTexture = false;

    m_renderer->setDeviceRect(m_size);
    m_renderer->setViewportRect(m_size);
    m_renderer->m_projectionRect = m_rect.toRect();
    m_renderer->setClearColor(Qt::transparent);

    m_renderer->renderScene();
    m_renderer->render(&m_pixmap);

    root->markDirty(QSGNode::DirtyForceUpdate); // Force matrix, clip, opacity and render list update.

    if (m_recursive)
        markDirtyTexture(); // Continuously update if 'live' and 'recursive'.
}
