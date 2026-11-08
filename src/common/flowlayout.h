#ifndef OE_FLOWLAYOUT_H
#define OE_FLOWLAYOUT_H

#include <QLayout>
#include <QStyle>
#include <QWidget>
#include <QList>

// Wrapping layout for toolbar/filter rows: child widgets flow onto new lines
// when the container is narrow instead of being clipped mid-word (Qt's
// canonical FlowLayout example, adapted for this project). Use it anywhere a
// horizontal row of controls must survive small window widths — e.g. the
// Connection Graph toolbar and the People directory filter row.
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr, int margin = 0, int spacing = -1);
    explicit FlowLayout(int spacing);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;
    void setGeometry(const QRect& rect) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;
    Qt::Orientations expandingDirections() const override { return Qt::Orientations(); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

private:
    int doLayout(const QRect& rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem*> m_items;
    int m_hSpace;
    int m_vSpace;
};

#endif // OE_FLOWLAYOUT_H
