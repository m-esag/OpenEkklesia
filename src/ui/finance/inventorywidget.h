#ifndef INVENTORYWIDGET_H
#define INVENTORYWIDGET_H

#include <QWidget>
#include <QTreeView>
#include <QTableView>
#include <QStandardItemModel>
#include <QSqlQueryModel>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>

class InventoryWidget : public QWidget {
    Q_OBJECT

public:
    explicit InventoryWidget(QWidget *parent = nullptr);
    ~InventoryWidget();

    void refreshTree();
    void selectAssetByBarcode(const QString& barcode);
    void applyRole(const QString& role);

private slots:
    void onItemSelectionChanged(const QModelIndex& current, const QModelIndex& previous);
    void onCheckoutClicked();
    void onCheckinClicked();
    void onAddAsset();

private:
    void setupUI();
    void showAssetDetails(int assetId);
    void refreshHistoryTable(int assetId);

    QTreeView* m_treeView;
    QStandardItemModel* m_treeModel;

    // Details panel elements
    QWidget* m_detailsCard;
    QLabel* m_lblName;
    QLabel* m_lblLocation;
    QLabel* m_lblSerial;
    QLabel* m_lblBarcode;
    QLabel* m_lblCondition;
    QLabel* m_lblStatus;

    QPushButton* m_btnCheckout;
    QPushButton* m_btnCheckin;

    QTableView* m_historyView;
    QSqlQueryModel* m_historyModel;

    int m_selectedAssetId;
    QString m_selectedAssetBarcode;
    bool m_selectedAssetIsCheckedOut;
};

#endif // INVENTORYWIDGET_H
