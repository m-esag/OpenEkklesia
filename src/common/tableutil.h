#ifndef TABLEUTIL_H
#define TABLEUTIL_H

#include <QTableView>
#include <QHeaderView>
#include <QSettings>

namespace TableUtil {

// Settings key for the user preference (Settings > Interface):
//   true  (default) = columns stretch to fill the table width
//   false           = columns size to their contents (interactive)
inline bool autoColumnSizeEnabled() {
    return QSettings("OpenEkklesia", "CHMS").value("ui/autoColumnSize", true).toBool();
}

// Apply the app-wide table conventions to a view:
//   - consistent column sizing honoring the user's auto-size preference
//   - column sorting (works out of the box for QSqlTableModel-backed views;
//     for setQuery/QSqlQueryModel-backed tables wrap them in a proxy first,
//     then call applyStandardTableSetup)
// Call once, after setModel(). Safe to call again on model reset.
inline void applyStandardTableSetup(QTableView* view, bool interactiveSorting = true) {
    if (!view) return;

    QHeaderView* hdr = view->horizontalHeader();
    if (autoColumnSizeEnabled()) {
        hdr->setSectionResizeMode(QHeaderView::Stretch);
    } else {
        hdr->setSectionResizeMode(QHeaderView::Interactive);
        hdr->setSectionResizeMode(0, QHeaderView::ResizeToContents); // id/date columns stay compact
    }
    hdr->setStretchLastSection(true);

    if (interactiveSorting) {
        view->setSortingEnabled(true);
        // Sort by the first visible column initially, ascending.
        if (view->model() && view->model()->columnCount() > 0) {
            hdr->setSortIndicator(0, Qt::AscendingOrder);
        }
    }
    view->setAlternatingRowColors(true);
}

} // namespace TableUtil

#endif // TABLEUTIL_H
