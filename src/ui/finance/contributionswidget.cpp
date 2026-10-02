#include "contributionswidget.h"
#include "../database/databasemanager.h"
#include "../common/appcurrency.h"
#include "thememanager.h"
#include "../core/iconmanager.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QCompleter>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QInputDialog>
#include <QMenu>
#include <QListWidget>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QTextStream>
#include <QColor>
#include <QClipboard>
#include <QGuiApplication>
#include <QFileDialog>
#include <QSplitter>
#include <QGroupBox>
#include <QSettings>

ContributionsWidget::ContributionsWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    setupModels();
    refreshBatchesList();
}

ContributionsWidget::~ContributionsWidget() {}

void ContributionsWidget::setupUI() {
    QHBoxLayout* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(14);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(4);

    // =========================================================================
    // LEFT MAIN CANVAS (Batch Workspace)
    // =========================================================================
    QWidget* mainWorkspace = new QWidget(this);
    ThemeTokens tok = ThemeManager::instance().currentTokens();
    QVBoxLayout* mainLayout = new QVBoxLayout(mainWorkspace);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    // 1. Top Batch Header Bar
    QFrame* headerCard = new QFrame(mainWorkspace);
    headerCard->setObjectName("card");
    headerCard->setStyleSheet("QFrame#card { border-radius: 8px; padding: 10px; }");
    QVBoxLayout* headerLayout = new QVBoxLayout(headerCard);
    headerLayout->setContentsMargins(14, 12, 14, 12);
    headerLayout->setSpacing(10);

    QHBoxLayout* titleRow = new QHBoxLayout();
    m_txtBatchName = new QLineEdit(headerCard);
    m_txtBatchName->setPlaceholderText("Batch Name (e.g. Sunday Morning Service)");
    m_txtBatchName->setStyleSheet("font-size: 16px; font-weight: bold; padding: 6px 10px; border-radius: 6px;");
    connect(m_txtBatchName, &QLineEdit::editingFinished, this, &ContributionsWidget::onSaveBatchHeaderDetails);

    m_batchDateEdit = new QDateEdit(QDate::currentDate(), headerCard);
    m_batchDateEdit->setCalendarPopup(true);
    m_batchDateEdit->setDisplayFormat("yyyy-MM-dd");
    m_batchDateEdit->setStyleSheet("font-size: 13px; font-weight: bold; padding: 6px 10px; border-radius: 6px;");
    connect(m_batchDateEdit, &QDateEdit::dateChanged, this, &ContributionsWidget::onSaveBatchHeaderDetails);

    m_lblBatchStatusBadge = new QLabel("In Progress", headerCard);
    m_lblBatchStatusBadge->setStyleSheet("padding: 4px 10px; border-radius: 12px; font-size: 11px; font-weight: bold; background-color: rgba(249, 226, 175, 0.2); color: #f9e2af;");

    titleRow->addWidget(m_txtBatchName, 3);
    titleRow->addWidget(m_batchDateEdit, 1);
    titleRow->addWidget(m_lblBatchStatusBadge);
    titleRow->addStretch();

    m_btnBatchDefaults = new QPushButton(" Defaults", headerCard);
    m_btnBatchDefaults->setIcon(IconManager::instance().getIcon("settings", QColor(), 14));
    m_btnBatchDefaults->setObjectName("secondaryBtn");
    m_btnBatchDefaults->setToolTip("Configure default date, payment method, and fund for this batch");
    connect(m_btnBatchDefaults, &QPushButton::clicked, this, &ContributionsWidget::onBatchDefaultsClicked);

    m_btnDeleteBatch = new QPushButton(" Delete Batch", headerCard);
    m_btnDeleteBatch->setIcon(IconManager::instance().getDangerIcon("trash-2", 14));
    m_btnDeleteBatch->setObjectName("dangerBtn");
    connect(m_btnDeleteBatch, &QPushButton::clicked, this, &ContributionsWidget::onDeleteBatchClicked);

    m_btnCommitBatch = new QPushButton(" Commit Batch", headerCard);
    m_btnCommitBatch->setIcon(IconManager::instance().getPrimaryIcon("lock", 14));
    m_btnCommitBatch->setObjectName("saveBtn");
    connect(m_btnCommitBatch, &QPushButton::clicked, this, &ContributionsWidget::onToggleBatchCommit);

    titleRow->addWidget(m_btnBatchDefaults);
    titleRow->addWidget(m_btnDeleteBatch);
    titleRow->addWidget(m_btnCommitBatch);

    headerLayout->addLayout(titleRow);

    m_lblBatchMeta = new QLabel("Batch Details • 0 donations recorded", headerCard);
    m_lblBatchMeta->setStyleSheet("color: palette(placeholderText); font-size: 12px;");
    headerLayout->addWidget(m_lblBatchMeta);

    mainLayout->addWidget(headerCard);

    // 2. Planning Center-Style Inline Multi-Fund Donation Entry Card
    m_entryCard = new QFrame(mainWorkspace);
    m_entryCard->setObjectName("card");
    m_entryCard->setStyleSheet("QFrame#card { border-radius: 8px; padding: 14px; }");
    QVBoxLayout* entryLayout = new QVBoxLayout(m_entryCard);
    entryLayout->setContentsMargins(16, 14, 16, 14);
    entryLayout->setSpacing(12);

    QHBoxLayout* donorRow = new QHBoxLayout();
    QLabel* lblDonor = new QLabel("Contributor:", m_entryCard);
    lblDonor->setStyleSheet("font-weight: bold; font-size: 13px;");
    m_personCombo = new QComboBox(m_entryCard);
    m_personCombo->setEditable(true);
    m_personCombo->setStyleSheet("padding: 6px 10px; font-size: 13px;");

    m_lblTotalHeadline = new QLabel(appCurrencySymbol() + " 0.00", m_entryCard);
    m_lblTotalHeadline->setStyleSheet(QString("font-size: 22px; font-weight: bold; color: %1; padding-left: 15px;").arg(tok.accentSuccess.name()));

    donorRow->addWidget(lblDonor);
    donorRow->addWidget(m_personCombo, 1);
    donorRow->addWidget(m_lblTotalHeadline);
    entryLayout->addLayout(donorRow);

    // Dynamic Multi-Fund Split Lines Container
    m_splitsContainer = new QWidget(m_entryCard);
    m_splitsLayout = new QVBoxLayout(m_splitsContainer);
    m_splitsLayout->setContentsMargins(0, 0, 0, 0);
    m_splitsLayout->setSpacing(6);
    entryLayout->addWidget(m_splitsContainer);

    QHBoxLayout* splitActionsRow = new QHBoxLayout();
    m_btnAddSplit = new QPushButton(" Add Fund Split", m_entryCard);
    m_btnAddSplit->setIcon(IconManager::instance().getIcon("plus", QColor(), 14));
    m_btnAddSplit->setObjectName("secondaryBtn");
    m_btnAddSplit->setStyleSheet("font-size: 11px; padding: 4px 10px;");
    connect(m_btnAddSplit, &QPushButton::clicked, this, [this]() { onAddSplitRow(0.0, -1); });
    splitActionsRow->addWidget(m_btnAddSplit);
    splitActionsRow->addStretch();
    entryLayout->addLayout(splitActionsRow);

    // Metadata Row: Date, Method, Check #, Note
    QHBoxLayout* metaRow = new QHBoxLayout();
    metaRow->setSpacing(10);

    m_dateEdit = new QDateEdit(QDate::currentDate(), m_entryCard);
    m_dateEdit->setCalendarPopup(true);
    m_dateEdit->setDisplayFormat("yyyy-MM-dd");

    m_methodCombo = new QComboBox(m_entryCard);
    m_methodCombo->addItems({"Check", "Cash", "Card / Online", "Bank Transfer / ACH"});

    m_checkNumEdit = new QLineEdit(m_entryCard);
    m_checkNumEdit->setPlaceholderText("Check # (optional)");
    m_checkNumEdit->setMaximumWidth(140);

    m_noteEdit = new QLineEdit(m_entryCard);
    m_noteEdit->setPlaceholderText("Optional note / envelope memo");

    metaRow->addWidget(new QLabel("Date:", m_entryCard));
    metaRow->addWidget(m_dateEdit);
    metaRow->addWidget(new QLabel("Method:", m_entryCard));
    metaRow->addWidget(m_methodCombo);
    metaRow->addWidget(m_checkNumEdit);
    metaRow->addWidget(m_noteEdit, 1);

    entryLayout->addLayout(metaRow);

    QHBoxLayout* entryActions = new QHBoxLayout();
    m_btnSave = new QPushButton(" Save & Next Donation (Enter)", m_entryCard);
    m_btnSave->setIcon(IconManager::instance().getPrimaryIcon("check", 14));
    m_btnSave->setObjectName("primaryBtn");
    m_btnSave->setMinimumHeight(36);
    m_btnSave->setStyleSheet("font-weight: bold; font-size: 13px; padding: 8px 18px;");
    connect(m_btnSave, &QPushButton::clicked, this, &ContributionsWidget::onSaveContribution);

    m_btnClearForm = new QPushButton(" Clear Form (Esc)", m_entryCard);
    m_btnClearForm->setIcon(IconManager::instance().getIcon("refresh-cw", QColor(), 14));
    m_btnClearForm->setObjectName("secondaryBtn");
    m_btnClearForm->setMinimumHeight(36);
    connect(m_btnClearForm, &QPushButton::clicked, this, &ContributionsWidget::onResetDonationForm);

    entryActions->addWidget(m_btnSave);
    entryActions->addWidget(m_btnClearForm);
    entryActions->addStretch();
    entryLayout->addLayout(entryActions);

    mainLayout->addWidget(m_entryCard);

    // 3. Real-Time Fund Allocation Pill Strip
    m_pillsContainer = new QWidget(mainWorkspace);
    m_pillsLayout = new QHBoxLayout(m_pillsContainer);
    m_pillsLayout->setContentsMargins(0, 0, 0, 0);
    m_pillsLayout->setSpacing(8);
    mainLayout->addWidget(m_pillsContainer);

    // 4. Batch Donations Ledger Table
    QFrame* tableCard = new QFrame(mainWorkspace);
    tableCard->setObjectName("card");
    QVBoxLayout* tableLay = new QVBoxLayout(tableCard);
    tableLay->setContentsMargins(10, 10, 10, 10);
    tableLay->setSpacing(8);

    m_historyTable = new QTableView(tableCard);
    m_historyTable->setContextMenuPolicy(Qt::CustomContextMenu);
    m_historyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_historyTable->verticalHeader()->setVisible(false);
    m_historyTable->setAlternatingRowColors(true);
    connect(m_historyTable, &QTableView::customContextMenuRequested, this, &ContributionsWidget::onHistoryContextMenu);
    tableLay->addWidget(m_historyTable, 1);

    QHBoxLayout* tableFooter = new QHBoxLayout();
    m_lblBatchTotalSummary = new QLabel("0 donations recorded in this batch", tableCard);
    m_lblBatchTotalSummary->setStyleSheet("font-weight: bold; font-size: 13px; color: #a6e3a1;");
    tableFooter->addWidget(m_lblBatchTotalSummary);
    tableFooter->addStretch();

    m_btnManageFunds = new QPushButton(" Manage Funds", tableCard);
    m_btnManageFunds->setIcon(IconManager::instance().getIcon("tag", QColor(), 14));
    m_btnManageFunds->setObjectName("secondaryBtn");
    connect(m_btnManageFunds, &QPushButton::clicked, this, &ContributionsWidget::onManageFunds);
    tableFooter->addWidget(m_btnManageFunds);

    m_btnGnuCashExport = new QPushButton(" Export for GnuCash (CSV)", tableCard);
    m_btnGnuCashExport->setIcon(IconManager::instance().getIcon("download", ThemeManager::textOn(QColor("#74c7ec")), 14));
    m_btnGnuCashExport->setObjectName("accentBtnCyan");
    connect(m_btnGnuCashExport, &QPushButton::clicked, this, &ContributionsWidget::onExportGnuCashCsv);
    tableFooter->addWidget(m_btnGnuCashExport);

    tableLay->addLayout(tableFooter);
    mainLayout->addWidget(tableCard, 1);

    splitter->addWidget(mainWorkspace);

    // =========================================================================
    // RIGHT DRAWER (Batches Navigation Drawer)
    // =========================================================================
    QWidget* rightDrawer = new QWidget(this);
    rightDrawer->setMinimumWidth(280);
    rightDrawer->setMaximumWidth(360);
    QVBoxLayout* drawerLayout = new QVBoxLayout(rightDrawer);
    drawerLayout->setContentsMargins(0, 0, 0, 0);
    drawerLayout->setSpacing(10);

    QFrame* drawerCard = new QFrame(rightDrawer);
    drawerCard->setObjectName("card");
    QVBoxLayout* drawerCardLay = new QVBoxLayout(drawerCard);
    drawerCardLay->setContentsMargins(12, 12, 12, 12);
    drawerCardLay->setSpacing(10);

    // Tabs: In Progress vs Committed
    QHBoxLayout* tabLayout = new QHBoxLayout();
    m_tabInProgress = new QPushButton(" In Progress", drawerCard);
    m_tabInProgress->setIcon(IconManager::instance().getIcon("sparkles", ThemeManager::textOn(tok.accentPrimary), 14));
    m_tabCommitted = new QPushButton(" Committed", drawerCard);
    m_tabCommitted->setIcon(IconManager::instance().getIcon("check", tok.accentSuccess, 14));
    m_tabInProgress->setCheckable(true);
    m_tabCommitted->setCheckable(true);
    m_tabInProgress->setChecked(true);
    m_tabInProgress->setStyleSheet(QString("font-weight: bold; background-color: %1; color: %2; border-radius: 6px; padding: 6px;")
        .arg(tok.accentPrimary.name(), ThemeManager::textOn(tok.accentPrimary).name()));
    m_tabCommitted->setStyleSheet("padding: 6px; border-radius: 6px;");

    tabLayout->addWidget(m_tabInProgress);
    tabLayout->addWidget(m_tabCommitted);
    drawerCardLay->addLayout(tabLayout);

    m_btnNewBatch = new QPushButton(" New Batch", drawerCard);
    m_btnNewBatch->setIcon(IconManager::instance().getPrimaryIcon("plus", 14));
    m_btnNewBatch->setObjectName("primaryBtn");
    m_btnNewBatch->setMinimumHeight(34);
    connect(m_btnNewBatch, &QPushButton::clicked, this, &ContributionsWidget::onNewBatchClicked);
    drawerCardLay->addWidget(m_btnNewBatch);

    m_txtBatchSearch = new QLineEdit(drawerCard);
    m_txtBatchSearch->setPlaceholderText("Filter batches...");
    m_txtBatchSearch->setClearButtonEnabled(true);
    connect(m_txtBatchSearch, &QLineEdit::textChanged, this, &ContributionsWidget::onBatchFilterChanged);
    drawerCardLay->addWidget(m_txtBatchSearch);

    // Create batches list widget BEFORE wireup
    m_batchesList = new QListWidget(drawerCard);
    m_batchesList->setStyleSheet("QListWidget { border: none; background: transparent; }"
                                 "QListWidget::item { padding: 8px 10px; border-radius: 6px; margin-bottom: 4px; border: 1px solid palette(mid); }"
                                 "QListWidget::item:selected { background-color: palette(highlight); color: palette(highlightedText); font-weight: bold; }"
                                 "QListWidget::item:hover:!selected { background-color: palette(mid); }");
    connect(m_batchesList, &QListWidget::itemClicked, this, &ContributionsWidget::onBatchSelected);
    m_batchesList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_batchesList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* item = m_batchesList->itemAt(pos);
        if (!item) return;
        int bId = item->data(Qt::UserRole).toInt();
        if (bId <= 0) return;
        m_batchesList->setCurrentItem(item);
        loadBatch(bId);

        const bool isReadOnly = (m_userRole == "readonly" || m_userRole == "guest");
        const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";

        QMenu menu(this);
        QAction* actToggleCommit = menu.addAction(IconManager::instance().getIcon(m_batchStatus == "Committed" ? "unlock" : "package", QColor(), 14),
                                                  m_batchStatus == "Committed" ? "Reopen Batch for Editing" : "Commit & Lock Batch");
        QAction* actRename = menu.addAction(IconManager::instance().getIcon("edit-3", QColor(), 14), "Rename Batch...");
        QAction* actExport = menu.addAction(IconManager::instance().getIcon("file-text", QColor(), 14), "Export Batch to GnuCash CSV");
        menu.addSeparator();
        QAction* actDelete = menu.addAction(IconManager::instance().getDangerIcon("trash-2", 14), "Delete Batch...");

        if (isReadOnly) {
            actToggleCommit->setEnabled(false);
            actToggleCommit->setToolTip(roTooltip);
            actRename->setEnabled(false);
            actRename->setToolTip(roTooltip);
            actDelete->setEnabled(false);
            actDelete->setToolTip(roTooltip);
        }

        QAction* sel = menu.exec(m_batchesList->viewport()->mapToGlobal(pos));
        if (!sel) return;

        if (sel == actToggleCommit && !isReadOnly) {
            onToggleBatchCommit();
        } else if (sel == actRename && !isReadOnly) {
            // Renames respect the commit lock like the header editor does.
            QSqlQuery st;
            st.prepare("SELECT status FROM giving_batches WHERE id = ?");
            st.addBindValue(bId);
            QString stVal;
            if (st.exec() && st.next()) stVal = st.value(0).toString();
            if (stVal == "Committed") {
                QMessageBox::information(this, "Batch Locked",
                    "This batch is Committed. Reopen it to In Progress before renaming.");
                return;
            }
            bool ok = false;
            QString curName = m_txtBatchName ? m_txtBatchName->text() : "Batch";
            QString newName = QInputDialog::getText(this, "Rename Batch", "Batch Name:", QLineEdit::Normal, curName, &ok);
            if (ok && !newName.trimmed().isEmpty()) {
                QSqlQuery uq;
                uq.prepare("UPDATE giving_batches SET batch_name = ? WHERE id = ?");
                uq.addBindValue(newName.trimmed());
                uq.addBindValue(bId);
                uq.exec();
                loadBatch(bId);
                refreshBatchesList();
            }
        } else if (sel == actExport) {
            onExportGnuCashCsv();
        } else if (sel == actDelete && !isReadOnly) {
            onDeleteBatchClicked();
        }
    });
    drawerCardLay->addWidget(m_batchesList, 1);

    auto updateTabs = [this](bool inProgress) {
        if (!m_batchesList || !m_tabInProgress || !m_tabCommitted) return;
        m_tabInProgress->setChecked(inProgress);
        m_tabCommitted->setChecked(!inProgress);
        ThemeTokens curTok = ThemeManager::instance().currentTokens();
        m_tabInProgress->setStyleSheet(inProgress ? QString("font-weight: bold; background-color: %1; color: %2; border-radius: 6px; padding: 6px;")
            .arg(curTok.accentPrimary.name(), ThemeManager::textOn(curTok.accentPrimary).name()) : "padding: 6px; border-radius: 6px;");
        m_tabCommitted->setStyleSheet(!inProgress ? QString("font-weight: bold; background-color: %1; color: %2; border-radius: 6px; padding: 6px;")
            .arg(curTok.accentSuccess.name(), ThemeManager::textOn(curTok.accentSuccess).name()) : "padding: 6px; border-radius: 6px;");
        refreshBatchesList();
    };

    connect(m_tabInProgress, &QPushButton::clicked, this, [updateTabs]() { updateTabs(true); });
    connect(m_tabCommitted, &QPushButton::clicked, this, [updateTabs]() { updateTabs(false); });

    drawerLayout->addWidget(drawerCard);
    splitter->addWidget(rightDrawer);

    splitter->setSizes({700, 300});
    rootLayout->addWidget(splitter);
}

void ContributionsWidget::setupModels() {
    m_historyModel = new QSqlQueryModel(this);
    m_historyTable->setModel(m_historyModel);

    // Initial split row
    onAddSplitRow(0.0, -1);
    refreshContributions();
}

void ContributionsWidget::onAddSplitRow(double initialAmount, int fundId) {
    if (!m_splitsContainer || !m_splitsLayout) return;

    FundSplitRow row;
    row.container = new QWidget(m_splitsContainer);
    QHBoxLayout* rowLay = new QHBoxLayout(row.container);
    rowLay->setContentsMargins(0, 0, 0, 0);
    rowLay->setSpacing(8);

    row.amountSpin = new QDoubleSpinBox(row.container);
    row.amountSpin->setRange(0.00, 999999.00);
    row.amountSpin->setDecimals(2);
    row.amountSpin->setPrefix(appCurrencySymbol() + " ");
    row.amountSpin->setValue(initialAmount);
    row.amountSpin->setMinimumWidth(140);
    connect(row.amountSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ContributionsWidget::onUpdateSplitTotals);

    row.fundCombo = new QComboBox(row.container);
    for (const auto& f : m_fundsList) {
        row.fundCombo->addItem(f.second, f.first);
    }
    if (fundId > 0) {
        int idx = row.fundCombo->findData(fundId);
        if (idx >= 0) row.fundCombo->setCurrentIndex(idx);
    }

    row.btnRemove = new QPushButton(row.container);
    row.btnRemove->setIcon(IconManager::instance().getDangerIcon("trash-2", 14));
    row.btnRemove->setObjectName("ghostDangerBtn");
    row.btnRemove->setToolTip("Remove this split line");
    row.btnRemove->setMaximumWidth(32);
    QWidget* c = row.container;
    connect(row.btnRemove, &QPushButton::clicked, this, [this, c]() { onRemoveSplitRow(c); });

    rowLay->addWidget(row.amountSpin);
    rowLay->addWidget(new QLabel("to", row.container));
    rowLay->addWidget(row.fundCombo, 1);
    rowLay->addWidget(row.btnRemove);

    m_splitsLayout->addWidget(row.container);
    m_splitRows.append(row);

    // Only show delete button if multiple splits exist
    for (auto& r : m_splitRows) {
        if (r.btnRemove) r.btnRemove->setVisible(m_splitRows.size() > 1);
    }

    onUpdateSplitTotals();
    updateTabOrder();
}

void ContributionsWidget::onRemoveSplitRow(QWidget* container) {
    for (int i = 0; i < m_splitRows.size(); ++i) {
        if (m_splitRows[i].container == container) {
            m_splitsLayout->removeWidget(container);
            container->deleteLater();
            m_splitRows.removeAt(i);
            break;
        }
    }
    for (auto& r : m_splitRows) {
        if (r.btnRemove) r.btnRemove->setVisible(m_splitRows.size() > 1);
    }
    onUpdateSplitTotals();
    updateTabOrder();
}

void ContributionsWidget::onUpdateSplitTotals() {
    double total = 0.0;
    for (const auto& r : m_splitRows) {
        if (r.amountSpin) total += r.amountSpin->value();
    }
    if (m_lblTotalHeadline) {
        m_lblTotalHeadline->setText(QString("%1 %2").arg(appCurrencySymbol(), QString::number(total, 'f', 2)));
    }
}

void ContributionsWidget::onResetDonationForm() {
    while (m_splitRows.size() > 1) {
        FundSplitRow r = m_splitRows.takeLast();
        if (r.container) {
            m_splitsLayout->removeWidget(r.container);
            r.container->deleteLater();
        }
    }
    if (!m_splitRows.isEmpty() && m_splitRows[0].amountSpin) {
        m_splitRows[0].amountSpin->setValue(0.00);
        if (m_splitRows[0].btnRemove) m_splitRows[0].btnRemove->setVisible(false);
    }

    if (m_personCombo) {
        m_personCombo->setCurrentIndex(-1);
        if (m_personCombo->lineEdit()) m_personCombo->lineEdit()->clear();
        m_personCombo->setFocus();
    }
    if (m_checkNumEdit) m_checkNumEdit->clear();
    if (m_noteEdit) m_noteEdit->clear();
    onUpdateSplitTotals();
    updateTabOrder();
}

void ContributionsWidget::refreshContributions() {
    if (!m_personCombo) return;

    m_personCombo->blockSignals(true);
    m_personCombo->clear();
    m_peopleList.clear();

    QSqlQuery q;
    if (q.exec("SELECT id, first_name || ' ' || last_name FROM people ORDER BY last_name, first_name")) {
        while (q.next()) {
            int id = q.value(0).toInt();
            QString name = q.value(1).toString();
            m_peopleList.append({id, name});
            m_personCombo->addItem(name, id);
        }
    }
    m_personCombo->blockSignals(false);
    m_personCombo->insertItem(kAnonymousIndex, "— Anonymous (plate offering) —");

    // Completer for person
    QCompleter* completer = new QCompleter(this);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setModel(m_personCombo->model());
    m_personCombo->setCompleter(completer);

    connect(completer, QOverload<const QModelIndex&>::of(&QCompleter::activated), this, [this](const QModelIndex&) {
        if (!m_splitRows.isEmpty() && m_splitRows[0].amountSpin) {
            m_splitRows[0].amountSpin->setFocus();
            m_splitRows[0].amountSpin->selectAll();
        }
    });

    connect(m_personCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        if (!m_splitRows.isEmpty() && m_splitRows[0].amountSpin) {
            m_splitRows[0].amountSpin->setFocus();
            m_splitRows[0].amountSpin->selectAll();
        }
    });

    // Refresh funds list
    m_fundsList.clear();
    if (q.exec("SELECT id, fund_name FROM funds WHERE is_active = 1 ORDER BY fund_name")) {
        while (q.next()) {
            int id = q.value(0).toInt();
            QString name = q.value(1).toString();
            m_fundsList.append({id, name});
        }
    }
    refreshFundCombos();
    updateTabOrder();
}

void ContributionsWidget::updateTabOrder() {
    if (!m_personCombo || m_splitRows.isEmpty()) return;

    QWidget* prev = m_personCombo;
    if (m_personCombo->isEditable() && m_personCombo->lineEdit()) {
        prev = m_personCombo->lineEdit();
    }

    for (int i = 0; i < m_splitRows.size(); ++i) {
        auto& row = m_splitRows[i];
        if (row.amountSpin) {
            QWidget::setTabOrder(prev, row.amountSpin);
            prev = row.amountSpin;
        }
        if (row.fundCombo) {
            QWidget::setTabOrder(prev, row.fundCombo);
            prev = row.fundCombo;
        }
        if (row.btnRemove && row.btnRemove->isVisible()) {
            QWidget::setTabOrder(prev, row.btnRemove);
            prev = row.btnRemove;
        }
    }

    if (m_btnAddSplit) {
        QWidget::setTabOrder(prev, m_btnAddSplit);
        prev = m_btnAddSplit;
    }
    if (m_dateEdit) {
        QWidget::setTabOrder(prev, m_dateEdit);
        prev = m_dateEdit;
    }
    if (m_methodCombo) {
        QWidget::setTabOrder(prev, m_methodCombo);
        prev = m_methodCombo;
    }
    if (m_checkNumEdit) {
        QWidget::setTabOrder(prev, m_checkNumEdit);
        prev = m_checkNumEdit;
    }
    if (m_noteEdit) {
        QWidget::setTabOrder(prev, m_noteEdit);
        prev = m_noteEdit;
    }
    if (m_btnSave) {
        QWidget::setTabOrder(prev, m_btnSave);
        prev = m_btnSave;
    }
    if (m_btnClearForm) {
        QWidget::setTabOrder(prev, m_btnClearForm);
        prev = m_btnClearForm;
    }
}

void ContributionsWidget::refreshFundCombos() {
    for (auto& r : m_splitRows) {
        if (!r.fundCombo) continue;
        int curId = r.fundCombo->currentData().toInt();
        r.fundCombo->blockSignals(true);
        r.fundCombo->clear();
        for (const auto& f : m_fundsList) {
            r.fundCombo->addItem(f.second, f.first);
        }
        int idx = r.fundCombo->findData(curId);
        if (idx >= 0) r.fundCombo->setCurrentIndex(idx);
        r.fundCombo->blockSignals(false);
    }
}

void ContributionsWidget::refreshBatchesList() {
    if (!m_batchesList || !m_tabInProgress) return;

    m_batchesList->blockSignals(true);
    m_batchesList->clear();

    const bool showInProgress = m_tabInProgress->isChecked();
    const QString statusFilter = showInProgress ? "In Progress" : "Committed";

    QSqlQuery q;
    q.prepare("SELECT b.id, b.batch_name, b.batch_date, b.status, "
              "       COUNT(c.id) AS donation_count, "
              "       COALESCE(SUM(c.amount), 0) AS total_cents "
              "FROM giving_batches b "
              "LEFT JOIN contributions c ON c.batch_id = b.id "
              "WHERE b.status = ? "
              "GROUP BY b.id "
              "ORDER BY b.batch_date DESC, b.id DESC");
    q.addBindValue(statusFilter);

    int targetRow = -1;
    int currentRow = 0;

    if (q.exec()) {
        while (q.next()) {
            int bId = q.value(0).toInt();
            QString bName = q.value(1).toString();
            QString bDate = q.value(2).toString();
            int count = q.value(4).toInt();
            double dollars = q.value(5).toDouble() / 100.0;

            QString label = QString("Batch #%1 — %2\n%3 • %4 gifts • %5%6")
                                .arg(QString::number(bId), bDate, bName,
                                     QString::number(count), appCurrencySymbol(),
                                     QString::number(dollars, 'f', 2));

            QListWidgetItem* item = new QListWidgetItem(label, m_batchesList);
            item->setData(Qt::UserRole, bId);
            item->setData(Qt::UserRole + 1, bName);
            item->setData(Qt::UserRole + 2, bDate);

            if (bId == m_currentBatchId) {
                targetRow = currentRow;
            }
            currentRow++;
        }
    }
    m_batchesList->blockSignals(false);

    if (targetRow >= 0) {
        m_batchesList->setCurrentRow(targetRow);
    } else if (m_batchesList->count() > 0) {
        m_batchesList->setCurrentRow(0);
        int firstId = m_batchesList->item(0)->data(Qt::UserRole).toInt();
        loadBatch(firstId);
    } else {
        if (showInProgress) {
            // Auto-create initial batch silently in DB without blocking popup
            QDate date = QDate::currentDate();
            int defaultFundId = m_fundsList.isEmpty() ? 1 : m_fundsList[0].first;
            QSqlQuery qCreate;
            qCreate.prepare("INSERT INTO giving_batches (batch_name, batch_date, status, default_fund_id, default_payment_method, created_by) "
                            "VALUES ('Sunday Service Offering', ?, 'In Progress', ?, 'Check', 'Admin')");
            qCreate.addBindValue(date.toString("yyyy-MM-dd"));
            qCreate.addBindValue(defaultFundId);
            if (qCreate.exec()) {
                int newId = qCreate.lastInsertId().toInt();
                loadBatch(newId);
                QListWidgetItem* item = new QListWidgetItem(
                    QString("Batch #%1 — %2\nSunday Service Offering • 0 gifts • %30.00")
                        .arg(QString::number(newId), date.toString("yyyy-MM-dd"), appCurrencySymbol()),
                    m_batchesList);
                item->setData(Qt::UserRole, newId);
                m_batchesList->setCurrentRow(0);
            }
        } else {
            m_currentBatchId = -1;
            if (m_txtBatchName) m_txtBatchName->clear();
            if (m_lblBatchMeta) m_lblBatchMeta->setText("No committed batches found.");
            if (m_historyModel) m_historyModel->clear();
            if (m_lblBatchTotalSummary) m_lblBatchTotalSummary->setText("0 donations");
        }
    }
}

void ContributionsWidget::onBatchSelected(QListWidgetItem* item) {
    if (!item) return;
    int bId = item->data(Qt::UserRole).toInt();
    if (bId > 0 && bId != m_currentBatchId) {
        loadBatch(bId);
    }
}

void ContributionsWidget::loadBatch(int batchId) {
    m_currentBatchId = batchId;
    if (batchId <= 0) return;

    QSqlQuery q;
    q.prepare("SELECT batch_name, batch_date, status, default_fund_id, default_payment_method, created_by, batch_note "
              "FROM giving_batches WHERE id = ?");
    q.addBindValue(batchId);

    if (q.exec() && q.next()) {
        if (m_txtBatchName) {
            m_txtBatchName->blockSignals(true);
            m_txtBatchName->setText(q.value(0).toString());
            m_txtBatchName->blockSignals(false);
        }
        if (m_batchDateEdit) {
            m_batchDateEdit->blockSignals(true);
            m_batchDateEdit->setDate(QDate::fromString(q.value(1).toString(), "yyyy-MM-dd"));
            m_batchDateEdit->blockSignals(false);
        }

        m_batchStatus = q.value(2).toString();
        int defFund = q.value(3).toInt();
        QString defMethod = q.value(4).toString();
        QString createdBy = q.value(5).toString();

        const bool isCommitted = (m_batchStatus == "Committed");
        if (m_lblBatchStatusBadge) {
            if (isCommitted) {
                m_lblBatchStatusBadge->setText("Committed (Locked)");
                const bool dark = ThemeManager::instance().currentTokens().isDark;
                m_lblBatchStatusBadge->setStyleSheet(QString("padding: 4px 10px; border-radius: 12px; font-size: 11px; font-weight: bold; "
                    "background-color: %1; color: %2;").arg(dark ? "rgba(166, 227, 161, 0.2)" : "rgba(21, 128, 61, 0.10)",
                    dark ? "#a6e3a1" : "#15803d"));
            } else {
                m_lblBatchStatusBadge->setText("In Progress");
                m_lblBatchStatusBadge->setStyleSheet("padding: 4px 10px; border-radius: 12px; font-size: 11px; font-weight: bold; background-color: rgba(249, 226, 175, 0.2); color: #f9e2af;");
            }
        }

        if (m_btnCommitBatch) {
            if (isCommitted) {
                m_btnCommitBatch->setText(" Reopen Batch");
                m_btnCommitBatch->setIcon(IconManager::instance().getIcon("unlock", QColor(), 14));
                m_btnCommitBatch->setObjectName("secondaryBtn");
            } else {
                m_btnCommitBatch->setText(" Commit Batch");
                m_btnCommitBatch->setIcon(IconManager::instance().getPrimaryIcon("lock", 14));
                m_btnCommitBatch->setObjectName("saveBtn");
            }
        }

        if (m_entryCard) {
            m_entryCard->setEnabled(!isCommitted && m_userRole != "readonly");
        }

        // Apply defaults to entry card
        if (m_dateEdit && m_batchDateEdit) {
            m_dateEdit->setDate(m_batchDateEdit->date());
        }
        if (m_methodCombo && !defMethod.isEmpty()) {
            int mIdx = m_methodCombo->findText(defMethod);
            if (mIdx >= 0) m_methodCombo->setCurrentIndex(mIdx);
        }
        if (defFund > 0 && !m_splitRows.isEmpty() && m_splitRows[0].fundCombo) {
            int fIdx = m_splitRows[0].fundCombo->findData(defFund);
            if (fIdx >= 0) m_splitRows[0].fundCombo->setCurrentIndex(fIdx);
        }

        if (m_lblBatchMeta && m_batchDateEdit) {
            m_lblBatchMeta->setText(QString("Batch #%1 • Created by %2 • Date: %3")
                                        .arg(QString::number(batchId), createdBy, m_batchDateEdit->date().toString("yyyy-MM-dd")));
        }
    }

    refreshHistoryModel();
    refreshFundPills();
}

void ContributionsWidget::refreshHistoryModel() {
    if (!m_historyModel || !m_historyTable) return;
    if (m_currentBatchId <= 0) {
        m_historyModel->clear();
        return;
    }

    QString queryStr = QString(
        "SELECT c.id AS \"ID\", "
        "       c.contribution_date AS \"Date\", "
        "       COALESCE(p.first_name || ' ' || p.last_name, '— Anonymous —') AS \"Contributor\", "
        "       f.fund_name AS \"Fund\", "
        "       COALESCE(NULLIF(c.payment_method, ''), '—') AS \"Method\", "
        "       COALESCE(NULLIF(c.check_number, ''), '—') AS \"Check #\", "
        "       printf('%1%.2f', c.amount / 100.0) AS \"Amount\", "
        "       COALESCE(c.note, '') AS \"Note\" "
        "FROM contributions c "
        "LEFT JOIN people p ON c.person_id = p.id "
        "JOIN funds f ON c.fund_id = f.id "
        "WHERE c.batch_id = %2 "
        "ORDER BY c.id DESC").arg(appCurrencySymbol(), QString::number(m_currentBatchId));

    m_historyModel->setQuery(queryStr);
    m_historyTable->hideColumn(0);

    QSqlQuery sumQ;
    sumQ.prepare("SELECT COUNT(*), COALESCE(SUM(amount), 0) FROM contributions WHERE batch_id = ?");
    sumQ.addBindValue(m_currentBatchId);
    if (sumQ.exec() && sumQ.next()) {
        int count = sumQ.value(0).toInt();
        double dollars = sumQ.value(1).toDouble() / 100.0;
        if (m_lblBatchTotalSummary) {
            m_lblBatchTotalSummary->setText(
                QString("%1 donations recorded  •  Batch Total: %2%3")
                    .arg(QString::number(count), appCurrencySymbol(), QString::number(dollars, 'f', 2)));
        }
    }
}

void ContributionsWidget::refreshFundPills() {
    if (!m_pillsLayout || !m_pillsContainer) return;

    QLayoutItem* item;
    while ((item = m_pillsLayout->takeAt(0)) != nullptr) {
        // deleteLater, not delete: this layout is installed on the live
        // container and refreshes run while events may still be dispatched
        // (same churn pattern that corrupted the graph's Smart Bridge panel).
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    if (m_currentBatchId <= 0) return;

    QSqlQuery q;
    q.prepare("SELECT f.fund_name, SUM(c.amount) FROM contributions c "
              "JOIN funds f ON c.fund_id = f.id "
              "WHERE c.batch_id = ? "
              "GROUP BY f.id ORDER BY f.fund_name");
    q.addBindValue(m_currentBatchId);

    const QString colors[] = {"#89b4fa", "#a6e3a1", "#f9e2af", "#cba6f7", "#fab387", "#94e2d5"};
    int cIdx = 0;

    QLabel* lblHeader = new QLabel("Batch Funds:", m_pillsContainer);
    lblHeader->setStyleSheet("font-size: 11px; font-weight: bold; color: palette(placeholderText);");
    m_pillsLayout->addWidget(lblHeader);

    bool hasPills = false;
    if (q.exec()) {
        while (q.next()) {
            hasPills = true;
            QString fName = q.value(0).toString();
            double dollars = q.value(1).toDouble() / 100.0;
            QString color = colors[cIdx % 6];
            cIdx++;

            QLabel* pill = new QLabel(QString("%1%2  %3")
                                          .arg(appCurrencySymbol(), QString::number(dollars, 'f', 2), fName),
                                      m_pillsContainer);
            pill->setStyleSheet(QString(
                "QLabel { background-color: rgba(255,255,255,0.06); border: 1px solid %1; "
                "color: %1; font-weight: bold; font-size: 11px; padding: 3px 8px; border-radius: 10px; }")
                .arg(color));
            m_pillsLayout->addWidget(pill);
        }
    }
    if (!hasPills) {
        QLabel* empty = new QLabel("(No donations recorded yet in this batch)", m_pillsContainer);
        empty->setStyleSheet("color: #6c7086; font-size: 11px; font-style: italic;");
        m_pillsLayout->addWidget(empty);
    }
    m_pillsLayout->addStretch();
}

void ContributionsWidget::onSaveContribution() {
    if (m_currentBatchId <= 0) {
        QMessageBox::warning(this, "No Batch Selected", "Please select or create a batch first.");
        return;
    }
    if (m_batchStatus == "Committed") {
        QMessageBox::warning(this, "Batch Locked", "This batch has been committed. Reopen it to add more donations.");
        return;
    }

    double totalAmount = 0.0;
    for (const auto& r : m_splitRows) {
        if (!r.amountSpin || r.amountSpin->value() <= 0.0) continue;
        totalAmount += r.amountSpin->value();
    }
    if (totalAmount <= 0.00) {
        QMessageBox::warning(this, "Validation Error", "Please enter a positive donation amount in at least one fund split.");
        return;
    }

    const bool anonymous = (m_personCombo->currentIndex() == kAnonymousIndex);
    int personId = 0;
    if (!anonymous) {
        personId = m_personCombo->currentData().toInt();
        if (personId == 0) {
            QString typedText = m_personCombo->currentText();
            for (const auto& person : m_peopleList) {
                if (person.second.compare(typedText, Qt::CaseInsensitive) == 0) {
                    personId = person.first;
                    break;
                }
            }
        }
        if (personId == 0) {
            QMessageBox::warning(this, "Validation Error",
                                 "Invalid person selected. Pick a name from the list, or choose '— Anonymous —'.");
            return;
        }
    }

    bool firstGift = false;
    if (!anonymous) {
        QSqlQuery cnt;
        cnt.prepare("SELECT COUNT(*) FROM contributions WHERE person_id = ?");
        cnt.addBindValue(personId);
        if (cnt.exec() && cnt.next()) firstGift = (cnt.value(0).toInt() == 0);
    }

    QSqlDatabase db = DatabaseManager::instance().database();
    db.transaction();

    QString dateStr = m_dateEdit->date().toString("yyyy-MM-dd");
    QString method = m_methodCombo->currentText();
    QString checkNum = m_checkNumEdit->text().trimmed();
    QString baseNote = m_noteEdit->text().trimmed();

    bool allOk = true;
    for (const auto& r : m_splitRows) {
        if (!r.amountSpin || r.amountSpin->value() <= 0.0) continue;
        int amountCents = static_cast<int>(r.amountSpin->value() * 100.0 + 0.5);
        int fundId = r.fundCombo ? r.fundCombo->currentData().toInt() : 1;

        QSqlQuery q(db);
        q.prepare("INSERT INTO contributions (batch_id, person_id, fund_id, amount, contribution_date, payment_method, check_number, note) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
        q.addBindValue(m_currentBatchId);
        q.addBindValue(anonymous ? QVariant(QMetaType(QMetaType::Int)) : QVariant(personId));
        q.addBindValue(fundId);
        q.addBindValue(amountCents);
        q.addBindValue(dateStr);
        q.addBindValue(method);
        q.addBindValue(checkNum.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(checkNum));
        q.addBindValue(baseNote.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(baseNote));

        if (!q.exec()) {
            allOk = false;
            break;
        }
    }

    if (allOk) {
        db.commit();
        if (!anonymous) {
            const QString amountStr = QString("%1%2").arg(appCurrencySymbol(), QString::number(totalAmount, 'f', 2));
            if (firstGift) {
                DatabaseManager::logMemberHistory(personId, "First Gift",
                    QString("First recorded contribution: %1 (%2).").arg(amountStr, method));
            }
            DatabaseManager::logMemberHistory(personId, "Contribution",
                QString("Contributed %1 (%2)").arg(amountStr, method));
        }

        refreshHistoryModel();
        refreshFundPills();
        refreshBatchesList();
        onResetDonationForm();
    } else {
        db.rollback();
        QMessageBox::critical(this, "Database Error", "Failed to save donation splits.");
    }
}

void ContributionsWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        onSaveContribution();
        event->accept();
    } else if (event->key() == Qt::Key_Escape) {
        onResetDonationForm();
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void ContributionsWidget::onNewBatchClicked() {
    bool ok;
    QString name = QInputDialog::getText(this, "New Giving Batch", "Batch name:", QLineEdit::Normal,
                                         "Sunday Morning Service", &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    QDate date = QDate::currentDate();
    int defaultFundId = m_fundsList.isEmpty() ? 1 : m_fundsList[0].first;

    QSqlQuery q;
    q.prepare("INSERT INTO giving_batches (batch_name, batch_date, status, default_fund_id, default_payment_method, created_by) "
              "VALUES (?, ?, 'In Progress', ?, 'Check', 'Admin')");
    q.addBindValue(name);
    q.addBindValue(date.toString("yyyy-MM-dd"));
    q.addBindValue(defaultFundId);

    if (q.exec()) {
        int newId = q.lastInsertId().toInt();
        if (m_tabInProgress) m_tabInProgress->setChecked(true);
        refreshBatchesList();
        loadBatch(newId);
    } else {
        QMessageBox::critical(this, "Error", "Could not create new batch: " + q.lastError().text());
    }
}

void ContributionsWidget::onDeleteBatchClicked() {
    if (m_currentBatchId <= 0) return;
    auto res = QMessageBox::question(this, "Delete Batch",
                                     QString("Are you sure you want to delete Batch #%1 ('%2')?\n\nAll donations in this batch will be permanently deleted (not orphaned).")
                                         .arg(QString::number(m_currentBatchId), m_txtBatchName ? m_txtBatchName->text() : ""),
                                     QMessageBox::Yes | QMessageBox::No);
    if (res != QMessageBox::Yes) return;

    QSqlDatabase db = DatabaseManager::instance().database();
    if (!db.transaction()) {
        QMessageBox::critical(this, "Error", "Could not start delete: " + db.lastError().text());
        return;
    }
    QSqlQuery delGifts(db);
    delGifts.prepare("DELETE FROM contributions WHERE batch_id = ?");
    delGifts.addBindValue(m_currentBatchId);
    if (!delGifts.exec()) {
        db.rollback();
        QMessageBox::critical(this, "Error", "Failed to delete batch gifts: " + delGifts.lastError().text());
        return;
    }
    QSqlQuery q(db);
    q.prepare("DELETE FROM giving_batches WHERE id = ?");
    q.addBindValue(m_currentBatchId);
    if (!q.exec()) {
        db.rollback();
        QMessageBox::critical(this, "Error", "Failed to delete batch: " + q.lastError().text());
        return;
    }
    if (!db.commit()) {
        db.rollback();
        QMessageBox::critical(this, "Error", "Failed to delete batch.");
        return;
    }
    DatabaseManager::logAudit("giving_batches", m_currentBatchId, "status", "deleted", "DELETED", "finance");
    m_currentBatchId = -1;
    refreshBatchesList();
}

void ContributionsWidget::onToggleBatchCommit() {
    if (m_currentBatchId <= 0) return;
    const bool willCommit = (m_batchStatus == "In Progress");

    if (willCommit) {
        auto res = QMessageBox::question(this, "Commit Batch",
                                         "Committing this batch will finalize and lock all donation entries.\nContinue?",
                                         QMessageBox::Yes | QMessageBox::No);
        if (res != QMessageBox::Yes) return;
    }

    QString nextStatus = willCommit ? "Committed" : "In Progress";
    QSqlQuery q;
    q.prepare("UPDATE giving_batches SET status = ? WHERE id = ?");
    q.addBindValue(nextStatus);
    q.addBindValue(m_currentBatchId);

    if (q.exec()) {
        loadBatch(m_currentBatchId);
        refreshBatchesList();
    } else {
        QMessageBox::critical(this, "Error", "Could not update batch status: " + q.lastError().text());
    }
}

void ContributionsWidget::onSaveBatchHeaderDetails() {
    if (m_currentBatchId <= 0 || m_batchStatus == "Committed" || !m_txtBatchName || !m_batchDateEdit) return;
    QSqlQuery q;
    q.prepare("UPDATE giving_batches SET batch_name = ?, batch_date = ? WHERE id = ?");
    q.addBindValue(m_txtBatchName->text().trimmed());
    q.addBindValue(m_batchDateEdit->date().toString("yyyy-MM-dd"));
    q.addBindValue(m_currentBatchId);
    q.exec();
}

void ContributionsWidget::onBatchDefaultsClicked() {
    if (m_currentBatchId <= 0 || !m_batchDateEdit || !m_methodCombo) return;
    QDialog dlg(this);
    dlg.setWindowTitle("Batch Defaults");
    dlg.resize(380, 220);

    QFormLayout* form = new QFormLayout(&dlg);
    form->setSpacing(12);

    QDateEdit* defDate = new QDateEdit(m_batchDateEdit->date(), &dlg);
    defDate->setCalendarPopup(true);
    defDate->setDisplayFormat("yyyy-MM-dd");

    QComboBox* defMethod = new QComboBox(&dlg);
    defMethod->addItems({"Check", "Cash", "Card / Online", "Bank Transfer / ACH"});
    defMethod->setCurrentText(m_methodCombo->currentText());

    QComboBox* defFund = new QComboBox(&dlg);
    for (const auto& f : m_fundsList) {
        defFund->addItem(f.second, f.first);
    }

    form->addRow("Default Donation Date:", defDate);
    form->addRow("Default Payment Method:", defMethod);
    form->addRow("Default Primary Fund:", defFund);

    QDialogButtonBox* btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btns);

    if (dlg.exec() == QDialog::Accepted) {
        QSqlQuery q;
        q.prepare("UPDATE giving_batches SET batch_date = ?, default_fund_id = ?, default_payment_method = ? WHERE id = ?");
        q.addBindValue(defDate->date().toString("yyyy-MM-dd"));
        q.addBindValue(defFund->currentData().toInt());
        q.addBindValue(defMethod->currentText());
        q.addBindValue(m_currentBatchId);
        if (q.exec()) {
            loadBatch(m_currentBatchId);
        }
    }
}

void ContributionsWidget::onBatchFilterChanged() {
    if (!m_txtBatchSearch || !m_batchesList) return;
    QString filter = m_txtBatchSearch->text().trimmed().toLower();
    for (int i = 0; i < m_batchesList->count(); ++i) {
        QListWidgetItem* item = m_batchesList->item(i);
        if (!item) continue;
        item->setHidden(!filter.isEmpty() && !item->text().toLower().contains(filter));
    }
}

void ContributionsWidget::onHistoryContextMenu(const QPoint& pos) {
    if (!m_historyTable || !m_historyModel) return;

    QModelIndex index = m_historyTable->indexAt(pos);
    if (!index.isValid()) return;

    int row = index.row();
    int contributionId = m_historyModel->index(row, 0).data().toInt();
    QString name = m_historyModel->index(row, 2).data().toString();
    QString fundName = m_historyModel->index(row, 3).data().toString();
    QString dateStr = m_historyModel->index(row, 1).data().toString();
    QString amountStr = m_historyModel->index(row, 6).data().toString();

    const bool isReadOnly = (m_userRole == "readonly" || m_userRole == "guest");
    const bool isCommitted = (m_batchStatus == "Committed");
    const QString roTooltip = isReadOnly
        ? "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify."
        : "Committed batches are locked for fiscal audit integrity. Reopen batch to modify.";

    QMenu menu(this);
    QAction* actCopy = menu.addAction(IconManager::instance().getIcon("copy", QColor(), 14), "Copy Contribution Details");
    menu.addSeparator();
    QAction* actEdit = menu.addAction(IconManager::instance().getIcon("edit-3", QColor(), 14), "Edit Amount...");
    QAction* actMoveFund = menu.addAction(IconManager::instance().getIcon("tag", QColor(), 14), "Move to Another Fund...");
    menu.addSeparator();
    QAction* actDelete = menu.addAction(IconManager::instance().getDangerIcon("trash-2", 14), "Delete Giving Record");

    if (isReadOnly || isCommitted) {
        actEdit->setEnabled(false);
        actEdit->setToolTip(roTooltip);
        actMoveFund->setEnabled(false);
        actMoveFund->setToolTip(roTooltip);
        actDelete->setEnabled(false);
        actDelete->setToolTip(roTooltip);
    }

    QAction* selected = menu.exec(m_historyTable->viewport()->mapToGlobal(pos));
    if (!selected) return;

    if (selected == actCopy) {
        QString summary = QString("Donor: %1 | Date: %2 | Fund: %3 | Amount: %4").arg(name, dateStr, fundName, amountStr);
        QGuiApplication::clipboard()->setText(summary);
        return;
    }

    QSqlDatabase db = DatabaseManager::instance().database();

    if (selected == actMoveFund) {
        QDialog dlg(this);
        dlg.setWindowTitle("Move to Another Fund");
        dlg.resize(300, 120);
        QFormLayout* form = new QFormLayout(&dlg);
        QComboBox* fCombo = new QComboBox(&dlg);
        for (const auto& f : m_fundsList) {
            fCombo->addItem(f.second, f.first);
        }
        form->addRow("Destination Fund:", fCombo);
        QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form->addRow(bb);
        if (dlg.exec() == QDialog::Accepted && fCombo->count() > 0) {
            int newFundId = fCombo->currentData().toInt();
            QSqlQuery fundQ;
            fundQ.prepare("SELECT fund_name FROM funds WHERE id = ?");
            fundQ.addBindValue(newFundId);
            QString newFundName;
            if (fundQ.exec() && fundQ.next()) newFundName = fundQ.value(0).toString();
            QSqlQuery uq;
            uq.prepare("UPDATE contributions SET fund_id = ? WHERE id = ?");
            uq.addBindValue(newFundId);
            uq.addBindValue(contributionId);
            if (uq.exec()) {
                DatabaseManager::logAudit("contributions", contributionId, "fund", fundName, newFundName, "finance");
                refreshHistoryModel();
                refreshFundPills();
                refreshBatchesList();
            }
        }
    } else if (selected == actEdit) {
        bool ok;
        double currentAmount = amountStr.replace(appCurrencySymbol(), "").toDouble();
        double newAmount = QInputDialog::getDouble(this, "Edit Contribution", 
                                                    QString("Enter new amount for %1's contribution to %2:").arg(name, fundName), 
                                                    currentAmount, 0.01, 999999.99, 2, &ok);
        if (ok) {
            int newAmountCents = qRound(newAmount * 100);
            int currentAmountCents = qRound(currentAmount * 100);
            QSqlQuery query(db);
            query.prepare("UPDATE contributions SET amount = ? WHERE id = ?");
            query.addBindValue(newAmountCents);
            query.addBindValue(contributionId);
            if (query.exec()) {
                DatabaseManager::logAudit("contributions", contributionId, "amount", QString::number(currentAmountCents), QString::number(newAmountCents), "finance");
                refreshHistoryModel();
                refreshFundPills();
                refreshBatchesList();
            }
        }
    } else if (selected == actDelete) {
        auto reply = QMessageBox::question(this, "Confirm Delete", 
                                           QString("Delete this gift of %1 from %2?").arg(amountStr, name),
                                           QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            QSqlQuery query(db);
            query.prepare("DELETE FROM contributions WHERE id = ?");
            query.addBindValue(contributionId);
            if (query.exec()) {
                DatabaseManager::logAudit("contributions", contributionId, "amount", amountStr, "DELETED", "finance");
                refreshHistoryModel();
                refreshFundPills();
                refreshBatchesList();
            }
        }
    }
}

void ContributionsWidget::onManageFunds() {
    QDialog dlg(this);
    dlg.setWindowTitle("Manage Giving Funds");
    dlg.resize(420, 360);

    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->setSpacing(10);

    QListWidget* list = new QListWidget(&dlg);
    lay->addWidget(list, 1);

    auto refreshList = [list]() {
        list->clear();
        QSqlQuery q;
        q.exec("SELECT id, fund_name, is_active FROM funds ORDER BY fund_name");
        while (q.next()) {
            int id = q.value(0).toInt();
            QString name = q.value(1).toString();
            bool active = q.value(2).toBool();
            QListWidgetItem* item = new QListWidgetItem(
                active ? name : QString("%1 (inactive)").arg(name), list);
            item->setData(Qt::UserRole, id);
            if (!active) item->setForeground(ThemeManager::instance().currentTokens().textSecondary);
        }
    };
    refreshList();

    QDialogButtonBox* btns = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QPushButton* btnAdd = btns->addButton(" Add Fund", QDialogButtonBox::ActionRole);
    btnAdd->setIcon(IconManager::instance().getPrimaryIcon("plus", 14));
    QPushButton* btnRename = btns->addButton(" Rename", QDialogButtonBox::ActionRole);
    btnRename->setIcon(IconManager::instance().getIcon("edit-3", QColor(), 14));
    QPushButton* btnToggle = btns->addButton(" Deactivate / Reactivate", QDialogButtonBox::ActionRole);
    btnToggle->setIcon(IconManager::instance().getIcon("refresh-cw", QColor(), 14));
    lay->addWidget(btns);

    connect(btnAdd, &QPushButton::clicked, this, [this, &refreshList]() {
        bool ok;
        QString name = QInputDialog::getText(this, "New Fund", "Fund name:", QLineEdit::Normal, "", &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        QSqlQuery q;
        q.prepare("INSERT INTO funds (fund_name, is_active) VALUES (?, 1)");
        q.addBindValue(name);
        if (q.exec()) {
            refreshList();
            refreshContributions();
        }
    });

    connect(btnRename, &QPushButton::clicked, this, [this, list, &refreshList]() {
        QListWidgetItem* sel = list->currentItem();
        if (!sel) return;
        int id = sel->data(Qt::UserRole).toInt();
        bool ok;
        QString name = QInputDialog::getText(this, "Rename Fund", "New name:", QLineEdit::Normal,
                                             sel->text().replace(" (inactive)", ""), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        QSqlQuery q;
        q.prepare("UPDATE funds SET fund_name = ? WHERE id = ?");
        q.addBindValue(name);
        q.addBindValue(id);
        if (q.exec()) {
            refreshList();
            refreshContributions();
            refreshHistoryModel();
            refreshFundPills();
        }
    });

    connect(btnToggle, &QPushButton::clicked, this, [this, list, &refreshList]() {
        QListWidgetItem* sel = list->currentItem();
        if (!sel) return;
        int id = sel->data(Qt::UserRole).toInt();
        QSqlQuery q;
        q.prepare("UPDATE funds SET is_active = 1 - is_active WHERE id = ?");
        q.addBindValue(id);
        if (q.exec()) {
            refreshList();
            refreshContributions();
        }
    });

    dlg.exec();
}

void ContributionsWidget::onExportGnuCashCsv() {
    QDialog dlg(this);
    dlg.setWindowTitle("Export Giving for GnuCash");
    dlg.resize(360, 190);

    QFormLayout* form = new QFormLayout(&dlg);
    QDateEdit* from = new QDateEdit(QDate::currentDate().addMonths(-1), &dlg);
    QDateEdit* to = new QDateEdit(QDate::currentDate(), &dlg);
    for (auto* d : {from, to}) {
        d->setCalendarPopup(true);
        d->setDisplayFormat("yyyy-MM-dd");
    }
    QCheckBox* chkSummary = new QCheckBox("One row per fund (summary) instead of per-gift", &dlg);
    chkSummary->setChecked(false);
    form->addRow("From:", from);
    form->addRow("To:", to);
    form->addRow(chkSummary);

    QDialogButtonBox* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btns);
    if (dlg.exec() != QDialog::Accepted) return;

    QString suggested = QString("OpenEkklesia_Giving_%1_%2.csv")
                            .arg(from->date().toString("yyyyMMdd"), to->date().toString("yyyyMMdd"));
    QString fileName = QFileDialog::getSaveFileName(this, "Export GnuCash CSV",
                       QDir::homePath() + "/" + suggested, "CSV Files (*.csv)");
    if (fileName.isEmpty()) return;

    if (!chkSummary->isChecked()) {
        auto warn = QMessageBox::warning(this, "Data Security Notice",
            "Per-gift export lists donor names with amounts.\nUse summary mode to de-identify, and store safely.",
            QMessageBox::Ok | QMessageBox::Cancel);
        if (warn != QMessageBox::Ok) return;
    }

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Error", "Could not open file for writing: " + file.errorString());
        return;
    }

    auto csv = [](const QString& s) {
        QString v = s;
        if (v.contains(',') || v.contains('"') || v.contains('\n'))
            v = '"' + v.replace('"', "\"\"") + '"';
        return v;
    };

    QTextStream out(&file);
    int rowCount = 0;
    qint64 totalCents = 0;

    if (chkSummary->isChecked()) {
        out << "Date,Description,Deposit\n";
        QSqlQuery q;
        q.prepare("SELECT f.fund_name, SUM(c.amount) FROM contributions c "
                  "JOIN funds f ON c.fund_id = f.id "
                  "WHERE c.contribution_date >= ? AND c.contribution_date <= ? "
                  "GROUP BY f.id ORDER BY f.fund_name");
        q.addBindValue(from->date().toString("yyyy-MM-dd"));
        q.addBindValue(to->date().toString("yyyy-MM-dd"));
        if (q.exec()) {
            while (q.next()) {
                QString fund = q.value(0).toString();
                const qint64 cents = q.value(1).toLongLong();
                out << to->date().toString("yyyy-MM-dd") << ','
                    << csv(QString("Giving — %1 (%2 to %3)")
                               .arg(fund, from->date().toString("yyyy-MM-dd"), to->date().toString("yyyy-MM-dd")))
                    << ',' << QString::number(cents / 100.0, 'f', 2) << '\n';
                totalCents += cents;
                rowCount++;
            }
        }
    } else {
        out << "Date,Description,Deposit\n";
        QSqlQuery q;
        q.prepare("SELECT c.contribution_date, f.fund_name, "
                  "(p.first_name || ' ' || p.last_name), c.payment_method, c.amount "
                  "FROM contributions c "
                  "JOIN funds f ON c.fund_id = f.id "
                  "LEFT JOIN people p ON c.person_id = p.id "
                  "WHERE c.contribution_date >= ? AND c.contribution_date <= ? "
                  "ORDER BY c.contribution_date, c.id");
        q.addBindValue(from->date().toString("yyyy-MM-dd"));
        q.addBindValue(to->date().toString("yyyy-MM-dd"));
        if (q.exec()) {
            while (q.next()) {
                QString date = q.value(0).toString();
                QString fund = q.value(1).toString();
                QString donor = q.value(2).toString();
                QString method = q.value(3).toString();
                const qint64 cents = q.value(4).toLongLong();
                QString donorLabel = donor.isEmpty() ? "Anonymous" : donor;
                if (method.isEmpty()) method = "—";
                out << date << ','
                    << csv(QString("Giving — %1 — %2 (%3)").arg(fund, donorLabel, method))
                    << ',' << QString::number(cents / 100.0, 'f', 2) << '\n';
                totalCents += cents;
                rowCount++;
            }
        }
    }
    file.close();

    DatabaseManager::logAudit("exports", 0, "gnucash_csv", QString::number(rowCount), fileName, "finance");
    QMessageBox::information(this, "Export Complete",
        QString("Exported %1 rows totaling %2%3.\n\nReady to import into GnuCash.")
            .arg(QString::number(rowCount), appCurrencySymbol(), QString::number(totalCents / 100.0, 'f', 2)));
}

void ContributionsWidget::applyRole(const QString& role) {
    m_userRole = role;
    const bool readOnly = (role == "readonly");
    if (m_btnSave) m_btnSave->setEnabled(!readOnly && m_batchStatus != "Committed");
    if (m_btnAddSplit) m_btnAddSplit->setEnabled(!readOnly && m_batchStatus != "Committed");
    if (m_btnManageFunds) m_btnManageFunds->setEnabled(!readOnly);
    if (m_btnGnuCashExport) m_btnGnuCashExport->setEnabled(!readOnly);
    if (m_btnNewBatch) m_btnNewBatch->setEnabled(!readOnly);
    if (m_btnDeleteBatch) m_btnDeleteBatch->setEnabled(!readOnly);
    if (m_btnCommitBatch) m_btnCommitBatch->setEnabled(!readOnly);
    if (m_btnBatchDefaults) m_btnBatchDefaults->setEnabled(!readOnly);
    if (m_txtBatchName) m_txtBatchName->setReadOnly(readOnly);
    if (m_batchDateEdit) m_batchDateEdit->setEnabled(!readOnly);
}
