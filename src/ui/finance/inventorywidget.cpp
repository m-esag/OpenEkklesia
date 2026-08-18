#include "inventorywidget.h"
#include "../database/databasemanager.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QHeaderView>
#include <QDialog>
#include <QComboBox>
#include <QMessageBox>
#include <QCompleter>
#include <QDialogButtonBox>

InventoryWidget::InventoryWidget(QWidget *parent)
    : QWidget(parent), m_selectedAssetId(-1), m_selectedAssetIsCheckedOut(false)
{
    setupUI();
    refreshTree();
}

InventoryWidget::~InventoryWidget() {}

void InventoryWidget::setupUI() {
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(15);

    // Left Panel: Tree View
    QWidget* leftPanel = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout* titleRow = new QHBoxLayout();
    QLabel* treeLabel = new QLabel("Equipment Inventory", this);
    treeLabel->setObjectName("detailHeader");
    QPushButton* btnAddAsset = new QPushButton("➕ Add Asset", this);
    btnAddAsset->setObjectName("primaryBtn");
    connect(btnAddAsset, &QPushButton::clicked, this, &InventoryWidget::onAddAsset);
    titleRow->addWidget(treeLabel);
    titleRow->addStretch();
    titleRow->addWidget(btnAddAsset);
    leftLayout->addLayout(titleRow);

    m_treeView = new QTreeView(this);
    m_treeModel = new QStandardItemModel(this);
    m_treeModel->setHorizontalHeaderLabels({"Asset Location / Item"});
    m_treeView->setModel(m_treeModel);
    m_treeView->setHeaderHidden(true);
    m_treeView->setAlternatingRowColors(true);
    connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &InventoryWidget::onItemSelectionChanged);

    leftLayout->addWidget(m_treeView);
    mainLayout->addWidget(leftPanel, 2);

    // Right Panel: Details & Actions
    m_detailsCard = new QWidget(this);
    m_detailsCard->setObjectName("card");
    QVBoxLayout* rightLayout = new QVBoxLayout(m_detailsCard);
    rightLayout->setContentsMargins(15, 15, 15, 15);
    rightLayout->setSpacing(15);

    QLabel* detailTitle = new QLabel("Item Details", this);
    detailTitle->setObjectName("detailHeader");
    rightLayout->addWidget(detailTitle);

    QFormLayout* form = new QFormLayout();
    form->setSpacing(10);

    m_lblName = new QLabel("-", this);
    m_lblName->setObjectName("detailAssetName");
    form->addRow("Name:", m_lblName);

    m_lblLocation = new QLabel("-", this);
    form->addRow("Location:", m_lblLocation);

    m_lblSerial = new QLabel("-", this);
    form->addRow("Serial Number:", m_lblSerial);

    m_lblBarcode = new QLabel("-", this);
    m_lblBarcode->setObjectName("detailAssetBarcode");
    form->addRow("Barcode ID:", m_lblBarcode);

    m_lblCondition = new QLabel("-", this);
    form->addRow("Condition:", m_lblCondition);

    m_lblStatus = new QLabel("-", this);
    m_lblStatus->setObjectName("detailAssetStatus");
    form->addRow("Status:", m_lblStatus);

    rightLayout->addLayout(form);

    // Checkout / Checkin Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    
    m_btnCheckout = new QPushButton("Checkout Gear", this);
    m_btnCheckout->setObjectName("saveBtn");
    m_btnCheckout->setEnabled(false);
    connect(m_btnCheckout, &QPushButton::clicked, this, &InventoryWidget::onCheckoutClicked);
    btnLayout->addWidget(m_btnCheckout);

    m_btnCheckin = new QPushButton("Return / Check In", this);
    m_btnCheckin->setObjectName("primaryBtn");
    m_btnCheckin->setEnabled(false);
    connect(m_btnCheckin, &QPushButton::clicked, this, &InventoryWidget::onCheckinClicked);
    btnLayout->addWidget(m_btnCheckin);

    rightLayout->addLayout(btnLayout);

    // History log
    QLabel* logTitle = new QLabel("Checkout Chain of Custody Log", this);
    logTitle->setObjectName("detailSubHeader");
    rightLayout->addWidget(logTitle);

    m_historyView = new QTableView(this);
    m_historyModel = new QSqlQueryModel(this);
    m_historyView->setModel(m_historyModel);
    m_historyView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_historyView->setAlternatingRowColors(true);
    m_historyView->setMaximumHeight(180);
    rightLayout->addWidget(m_historyView);

    mainLayout->addWidget(m_detailsCard, 3);
}

void InventoryWidget::refreshTree() {
    m_treeModel->clear();
    m_treeModel->setHorizontalHeaderLabels({"Asset Location / Item"});

    QSqlDatabase db = DatabaseManager::instance().database();
    
    QSqlQuery qLoc("SELECT DISTINCT location FROM assets ORDER BY location", db);
    while (qLoc.next()) {
        QString location = qLoc.value(0).toString();
        QStandardItem* locItem = new QStandardItem(location);
        locItem->setSelectable(false);
        locItem->setData(true, Qt::UserRole + 1); // Mark as location parent node
        QFont font = locItem->font();
        font.setBold(true);
        locItem->setFont(font);
        
        QSqlQuery qAsset(db);
        qAsset.prepare("SELECT id, name, condition FROM assets WHERE location = ? ORDER BY name");
        qAsset.addBindValue(location);
        
        if (qAsset.exec()) {
            while (qAsset.next()) {
                int id = qAsset.value(0).toInt();
                QString name = qAsset.value(1).toString();
                QString condition = qAsset.value(2).toString();

                QStandardItem* assetItem = new QStandardItem(QString("%1 (%2)").arg(name, condition));
                assetItem->setData(id, Qt::UserRole); // Store asset id
                assetItem->setData(false, Qt::UserRole + 1); // Not location parent node
                locItem->appendRow(assetItem);
            }
        }
        m_treeModel->appendRow(locItem);
    }
    m_treeView->expandAll();
}

void InventoryWidget::onItemSelectionChanged(const QModelIndex& current, const QModelIndex&) {
    if (!current.isValid()) {
        showAssetDetails(-1);
        return;
    }
    
    bool isLoc = current.data(Qt::UserRole + 1).toBool();
    if (isLoc) {
        showAssetDetails(-1);
        return;
    }

    int assetId = current.data(Qt::UserRole).toInt();
    showAssetDetails(assetId);
}

void InventoryWidget::showAssetDetails(int assetId) {
    m_selectedAssetId = assetId;
    if (assetId == -1) {
        m_lblName->setText("-");
        m_lblLocation->setText("-");
        m_lblSerial->setText("-");
        m_lblBarcode->setText("-");
        m_lblCondition->setText("-");
        m_lblStatus->setText("-");
        m_lblStatus->setProperty("state", "empty");
        m_lblStatus->style()->polish(m_lblStatus);
        m_btnCheckout->setEnabled(false);
        m_btnCheckin->setEnabled(false);
        m_historyModel->clear();
        return;
    }

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    query.prepare("SELECT name, location, serial_number, condition, barcode FROM assets WHERE id = ?");
    query.addBindValue(assetId);
    
    if (query.exec() && query.next()) {
        m_lblName->setText(query.value(0).toString());
        m_lblLocation->setText(query.value(1).toString());
        m_lblSerial->setText(query.value(2).toString());
        m_selectedAssetBarcode = query.value(4).toString();
        m_lblBarcode->setText(m_selectedAssetBarcode);
        m_lblCondition->setText(query.value(3).toString());

        // Check if checked out
        QSqlQuery qLog(db);
        qLog.prepare("SELECT p.first_name || ' ' || p.last_name, c.checkout_date "
                     "FROM checkout_logs c "
                     "JOIN people p ON c.person_id = p.id "
                     "WHERE c.asset_id = ? AND c.return_date IS NULL");
        qLog.addBindValue(assetId);
        
        if (qLog.exec() && qLog.next()) {
            QString name = qLog.value(0).toString();
            QString date = qLog.value(1).toString();
            m_lblStatus->setText(QString("Checked out to %1 (%2)").arg(name, date));
            m_lblStatus->setProperty("state", "checkedout");
            m_lblStatus->style()->polish(m_lblStatus);
            m_selectedAssetIsCheckedOut = true;
            m_btnCheckout->setEnabled(false);
            m_btnCheckin->setEnabled(true);
        } else {
            m_lblStatus->setText("Available in Location");
            m_lblStatus->setProperty("state", "available");
            m_lblStatus->style()->polish(m_lblStatus);
            m_selectedAssetIsCheckedOut = false;
            m_btnCheckout->setEnabled(true);
            m_btnCheckin->setEnabled(false);
        }

        refreshHistoryTable(assetId);
    }
}

void InventoryWidget::refreshHistoryTable(int assetId) {
    QString qStr = QString(
        "SELECT p.first_name || ' ' || p.last_name AS \"Borrower\", "
        "       c.checkout_date AS \"Checkout Date\", "
        "       COALESCE(c.return_date, 'Active') AS \"Return Date\", "
        "       c.notes AS \"Notes\" "
        "FROM checkout_logs c "
        "JOIN people p ON c.person_id = p.id "
        "WHERE c.asset_id = %1 "
        "ORDER BY c.checkout_date DESC, c.id DESC").arg(assetId);
    m_historyModel->setQuery(qStr);
}

void InventoryWidget::onCheckoutClicked() {
    if (m_selectedAssetId == -1 || m_selectedAssetIsCheckedOut) return;

    QDialog dlg(this);
    dlg.setWindowTitle("Checkout Equipment");
    dlg.resize(350, 180);

    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    QFormLayout* form = new QFormLayout();

    QLineEdit* memberEdit = new QLineEdit(&dlg);
    memberEdit->setPlaceholderText("Type name to search...");

    // Auto-complete members
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery qPeople("SELECT id, first_name || ' ' || last_name FROM people", db);
    QStringList names;
    QMap<QString, int> nameToId;
    while (qPeople.next()) {
        int id = qPeople.value(0).toInt();
        QString fullName = qPeople.value(1).toString();
        names << fullName;
        nameToId[fullName] = id;
    }

    QCompleter* completer = new QCompleter(names, &dlg);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    memberEdit->setCompleter(completer);

    form->addRow("Borrower Name:", memberEdit);

    QLineEdit* notesEdit = new QLineEdit(&dlg);
    notesEdit->setPlaceholderText("E.g. Youth beach trip...");
    form->addRow("Checkout Notes:", notesEdit);

    lay->addLayout(form);
    lay->addSpacing(15);

    QHBoxLayout* actions = new QHBoxLayout();
    QPushButton* btnCancel = new QPushButton("Cancel", &dlg);
    btnCancel->setObjectName("secondaryBtn");
    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    actions->addWidget(btnCancel);

    QPushButton* btnConfirm = new QPushButton("Confirm Checkout", &dlg);
    btnConfirm->setObjectName("saveBtn");
    connect(btnConfirm, &QPushButton::clicked, &dlg, [&]() {
        QString selectedName = memberEdit->text().trimmed();
        if (!nameToId.contains(selectedName)) {
            QMessageBox::warning(&dlg, "Invalid Person", "Please select a valid member from the directory auto-complete list.");
            return;
        }
        int personId = nameToId[selectedName];
        QString notes = notesEdit->text().trimmed();

        QSqlQuery insert(db);
        insert.prepare("INSERT INTO checkout_logs (asset_id, person_id, checkout_date, notes) VALUES (?, ?, date('now'), ?)");
        insert.addBindValue(m_selectedAssetId);
        insert.addBindValue(personId);
        insert.addBindValue(notes);
        
        if (insert.exec()) {
            dlg.accept();
        } else {
            QMessageBox::critical(&dlg, "DB Error", "Failed to register checkout: " + insert.lastError().text());
        }
    });
    actions->addWidget(btnConfirm);
    lay->addLayout(actions);

    if (dlg.exec() == QDialog::Accepted) {
        showAssetDetails(m_selectedAssetId);
    }
}

void InventoryWidget::onCheckinClicked() {
    if (m_selectedAssetId == -1 || !m_selectedAssetIsCheckedOut) return;

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    query.prepare("UPDATE checkout_logs SET return_date = date('now') WHERE asset_id = ? AND return_date IS NULL");
    query.addBindValue(m_selectedAssetId);
    
    if (query.exec()) {
        showAssetDetails(m_selectedAssetId);
    } else {
        QMessageBox::critical(this, "DB Error", "Failed to check gear back in: " + query.lastError().text());
    }
}

void InventoryWidget::selectAssetByBarcode(const QString& barcode) {
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    query.prepare("SELECT id FROM assets WHERE barcode = ?");
    query.addBindValue(barcode);
    
    if (query.exec() && query.next()) {
        int assetId = query.value(0).toInt();
        
        // Find in tree and select
        for (int r = 0; r < m_treeModel->rowCount(); ++r) {
            QStandardItem* locItem = m_treeModel->item(r);
            for (int c = 0; c < locItem->rowCount(); ++c) {
                QStandardItem* child = locItem->child(c);
                if (child->data(Qt::UserRole).toInt() == assetId) {
                    QModelIndex index = child->index();
                    m_treeView->setCurrentIndex(index);
                    m_treeView->scrollTo(index);
                    showAssetDetails(assetId);
                    return;
                }
            }
        }
    }
}

void InventoryWidget::applyRole(const QString& role) {
    if (role == "readonly") {
        m_btnCheckout->setEnabled(false);
        m_btnCheckin->setEnabled(false);
    }
}

void InventoryWidget::onAddAsset() {
    QDialog dlg(this);
    dlg.setWindowTitle("Register New Asset");
    dlg.resize(360, 280);

    QFormLayout* layout = new QFormLayout(&dlg);
    layout->setSpacing(12);

    QLineEdit* nameEdit = new QLineEdit(&dlg);
    QLineEdit* locationEdit = new QLineEdit(&dlg);
    QLineEdit* serialEdit = new QLineEdit(&dlg);
    QLineEdit* barcodeEdit = new QLineEdit(&dlg);
    QComboBox* conditionCombo = new QComboBox(&dlg);
    conditionCombo->addItems({"Good", "Fair", "Needs Repair", "Retired"});

    layout->addRow("Asset Name:", nameEdit);
    layout->addRow("Location:", locationEdit);
    layout->addRow("Serial Number:", serialEdit);
    layout->addRow("Barcode ID:", barcodeEdit);
    layout->addRow("Condition:", conditionCombo);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addRow(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        QString name = nameEdit->text().trimmed();
        QString location = locationEdit->text().trimmed();
        QString serial = serialEdit->text().trimmed();
        QString barcode = barcodeEdit->text().trimmed();
        QString condition = conditionCombo->currentText();

        if (name.isEmpty() || location.isEmpty()) {
            QMessageBox::warning(this, "Validation Error", "Name and Location are required.");
            return;
        }

        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery query(db);
        // Note: checkout state is derived from checkout_logs (return_date IS NULL),
        // so there is no is_checked_out column on assets.
        query.prepare("INSERT INTO assets (name, location, serial_number, barcode, condition) "
                      "VALUES (?, ?, ?, ?, ?)");
        query.addBindValue(name);
        query.addBindValue(location);
        query.addBindValue(serial.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : serial);
        query.addBindValue(barcode.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : barcode);
        query.addBindValue(condition);

        if (query.exec()) {
            refreshTree();
            QMessageBox::information(this, "Success", "New asset registered successfully.");
        } else {
            QMessageBox::critical(this, "Database Error", "Failed to register asset: " + query.lastError().text());
        }
    }
}
