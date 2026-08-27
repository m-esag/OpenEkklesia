#ifndef ICONMANAGER_H
#define ICONMANAGER_H

#include <QIcon>
#include <QPixmap>
#include <QColor>
#include <QPainter>
#include <QRectF>
#include <QString>
#include <QMap>

class IconManager {
public:
    static IconManager& instance();

    // Returns a QIcon dynamically rendered with the requested Lucide vector glyph, color, and size
    QIcon getIcon(const QString& name, const QColor& color = QColor(), int size = 20);

    // Convenience helpers that dynamically pick the WCAG contrast-safe icon color for primary/danger buttons
    QIcon getPrimaryIcon(const QString& name, int size = 14);
    QIcon getDangerIcon(const QString& name, int size = 14);

    // Returns a QPixmap dynamically rendered with the requested Lucide vector glyph
    QPixmap getPixmap(const QString& name, const QColor& color = QColor(), int size = 20);

    // Directly renders the Lucide vector glyph into an active QPainter (for canvas nodes, delegates, etc.)
    void paintIcon(QPainter* painter, const QString& name, const QRectF& targetRect, const QColor& color = QColor(), double strokeWidth = 2.0);

    // Clears cached pixmaps and icons (e.g. on major theme switches)
    void clearCache();

private:
    IconManager() = default;
    ~IconManager() = default;
    IconManager(const IconManager&) = delete;
    IconManager& operator=(const IconManager&) = delete;

    QMap<QString, QPixmap> m_pixmapCache;
    QMap<QString, QIcon> m_iconCache;

    void drawLucidePath(QPainter* painter, const QString& name);
};

#endif // ICONMANAGER_H
