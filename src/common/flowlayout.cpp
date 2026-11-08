#include "flowlayout.h"

#include <QWidget>

FlowLayout::FlowLayout(QWidget* parent, int margin, int spacing)
    : QLayout(parent), m_hSpace(-1), m_vSpace(-1)
{
    setContentsMargins(margin, margin, margin, margin);
    m_hSpace = spacing;
    m_vSpace = spacing;
}

FlowLayout::FlowLayout(int spacing)
    : m_hSpace(spacing), m_vSpace(spacing)
{
}

FlowLayout::~FlowLayout()
{
    qDeleteAll(m_items);
}

void FlowLayout::addItem(QLayoutItem* item)
{
    m_items.append(item);
}

int FlowLayout::count() const
{
    return m_items.size();
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return m_items.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    return (index >= 0 && index < m_items.size()) ? m_items.takeAt(index) : nullptr;
}

int FlowLayout::heightForWidth(int width) const
{
    const int height = doLayout(QRect(0, 0, width, 0), true);
    return height;
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const
{
    return minimumSize();
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* item : m_items)
        size = size.expandedTo(item->minimumSize());

    const QMargins margins = contentsMargins();
    size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    return size;
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
    int x = rect.x();
    int y = rect.y();
    int lineHeight = 0;

    for (QLayoutItem* item : m_items) {
        const QWidget* wid = item->widget();
        const int spaceX = m_hSpace >= 0 ? m_hSpace
                                         : smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
        const int spaceY = m_vSpace >= 0 ? m_vSpace
                                         : smartSpacing(QStyle::PM_LayoutVerticalSpacing);
        int nextX = x + item->sizeHint().width() + spaceX;
        if (nextX - spaceX > rect.right() && lineHeight > 0) {
            x = rect.x();
            y = y + lineHeight + spaceY;
            nextX = x + item->sizeHint().width() + spaceX;
            lineHeight = 0;
        }

        if (!testOnly)
            item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));

        x = nextX;
        lineHeight = qMax(lineHeight, item->sizeHint().height());
        Q_UNUSED(wid);
    }
    return y + lineHeight - rect.y() + contentsMargins().bottom();
}

int FlowLayout::smartSpacing(QStyle::PixelMetric pm) const
{
    QObject* parent = this->parent();
    if (!parent) {
        return -1;
    }
    if (parent->isWidgetType()) {
        const QWidget* pw = static_cast<const QWidget*>(parent);
        return pw->style()->pixelMetric(pm, nullptr, pw);
    }
    return static_cast<QLayout*>(parent)->spacing();
}
