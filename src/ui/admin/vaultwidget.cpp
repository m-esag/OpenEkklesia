#include "vaultwidget.h"
#include "../common/churchprofile.h"
#include "../common/appdateformatter.h"
#include <QDate>
#include "../database/databasemanager.h"
#include "../core/iconmanager.h"
#include "../core/thememanager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QClipboard>
#include <QGuiApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QDateEdit>
#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QTextStream>
#include <QTextDocument>
#include <QPrinter>
#include <QPageLayout>
#include <QPageSize>
#include <QSettings>
#include <QSet>
#include <QMenu>

VaultWidget::VaultWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    refreshVault();
    refreshBoardMinutes();
}

VaultWidget::~VaultWidget() {}

QString VaultWidget::vaultDirPath() {
    // Base path comes from DatabaseManager (the DB's own directory + /vault)
    // so documents always share the app's data root. A hardcoded path
    // previously created a case-sensitive sibling directory
    // (~/.local/share/openekklesia vs OpenEkklesia), and
    // QStandardPaths::AppDataLocation can nest differently (<org>/<app>) —
    // anchoring to the DB file sidesteps both failure modes.
    QString path = DatabaseManager::vaultDirPath();
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // One-time adoption: move files from the old lowercase location so no
    // document is left behind. rename() is safe because both directories
    // live under ~/.local/share on the same filesystem.
    const QString legacy = QDir::homePath() + "/.local/share/openekklesia/vault";
    if (legacy != path && QDir(legacy).exists()) {
        const auto files = QDir(legacy).entryInfoList(QDir::Files);
        for (const QFileInfo& fi : files) {
            const QString dest = path + "/" + fi.fileName();
            if (!QFile::exists(dest)) {
                QFile::rename(fi.absoluteFilePath(), dest);
            }
        }
        QDir(legacy).removeRecursively(); // no-op if collisions left files
    }

    // The vault holds private records (membership applications, minutes);
    // keep the directory readable only by this user.
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return path;
}

void VaultWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(30, 30, 30, 30);
    mainLayout->setSpacing(20);

    // Title / Header
    QLabel* pageTitle = new QLabel("Board Minutes & Document Vault", this);
    pageTitle->setObjectName("pageHeader");
    mainLayout->addWidget(pageTitle);

    QTabWidget* tabs = new QTabWidget(this);

    // --- Tab 1: Document Vault ---
    QWidget* tabVault = new QWidget(this);
    QVBoxLayout* vaultLay = new QVBoxLayout(tabVault);
    vaultLay->setContentsMargins(15, 15, 15, 15);
    vaultLay->setSpacing(12);

    QHBoxLayout* controls = new QHBoxLayout();
    
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search documents...");
    m_searchEdit->setFixedWidth(220);
    m_searchEdit->setObjectName("searchEdit");
    connect(m_searchEdit, &QLineEdit::textChanged, this, &VaultWidget::onSearchChanged);
    controls->addWidget(m_searchEdit);

    m_btnUpload = new QPushButton(" Upload Document (PDF)", this);
    m_btnUpload->setIcon(IconManager::instance().getPrimaryIcon("upload", 14));
    m_btnUpload->setObjectName("saveBtn");
    connect(m_btnUpload, &QPushButton::clicked, this, &VaultWidget::onUploadClicked);
    controls->addWidget(m_btnUpload);

    m_btnOpen = new QPushButton(" Open / View File", this);
    m_btnOpen->setIcon(IconManager::instance().getPrimaryIcon("eye", 14));
    m_btnOpen->setObjectName("primaryBtn");
    connect(m_btnOpen, &QPushButton::clicked, this, &VaultWidget::onOpenClicked);
    controls->addWidget(m_btnOpen);

    m_btnDelete = new QPushButton(" Remove Record", this);
    m_btnDelete->setIcon(IconManager::instance().getDangerIcon("trash-2", 14));
    m_btnDelete->setObjectName("dangerBtn");
    connect(m_btnDelete, &QPushButton::clicked, this, &VaultWidget::onDeleteClicked);
    controls->addWidget(m_btnDelete);

    m_btnSweep = new QPushButton(" Sweep Orphans", this);
    m_btnSweep->setIcon(IconManager::instance().getIcon("sparkles", QColor(), 14));
    m_btnSweep->setObjectName("secondaryBtn");
    m_btnSweep->setToolTip("Find files in the vault folder that no database record references (demo leftovers, record-only removals) and offer to delete them.");
    connect(m_btnSweep, &QPushButton::clicked, this, &VaultWidget::onSweepOrphans);
    controls->addWidget(m_btnSweep);

    controls->addStretch();
    vaultLay->addLayout(controls);

    m_tableView = new QTableView(this);
    m_queryModel = new QSqlQueryModel(this);
    m_tableView->setModel(m_queryModel);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tableView, &QTableView::customContextMenuRequested, this, [this](const QPoint& pos) {
        QModelIndex idx = m_tableView->indexAt(pos);
        if (!idx.isValid()) return;
        int row = idx.row();
        m_tableView->selectRow(row);
        int docId = m_queryModel->index(row, 0).data().toInt();
        QString docTitle = m_queryModel->index(row, 1).data().toString();
        QString docDate = m_queryModel->index(row, 2).data().toString();
        QString docCategory = m_queryModel->index(row, 3).data().toString();

        const ThemeTokens tok = ThemeManager::instance().currentTokens();
        const bool isReadOnly = (m_userRole == "readonly" || m_userRole == "guest");
        const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";

        QMenu menu(this);
        QAction* actOpen = menu.addAction(IconManager::instance().getIcon("eye", tok.accentPrimary, 14), "Open / View Document");
        QAction* actExport = menu.addAction(IconManager::instance().getIcon("download", QColor(), 14), "Save Copy to Local Disk...");
        QAction* actCopyInfo = menu.addAction(IconManager::instance().getIcon("copy", QColor(), 14), "Copy Document Info");
        menu.addSeparator();
        QAction* actDelete = menu.addAction(IconManager::instance().getDangerIcon("trash-2", 14), "Remove Document Record");
        if (isReadOnly) {
            actDelete->setEnabled(false);
            actDelete->setToolTip(roTooltip);
        }

        QAction* sel = menu.exec(m_tableView->viewport()->mapToGlobal(pos));
        if (!sel) return;

        if (sel == actOpen) {
            onOpenClicked();
        } else if (sel == actExport) {
            QSqlQuery q;
            q.prepare("SELECT file_path FROM vault_documents WHERE id = ?");
            q.addBindValue(docId);
            if (q.exec() && q.next()) {
                QString relPath = q.value(0).toString();
                QString fullPath = vaultDirPath() + "/" + relPath;
                if (QFile::exists(fullPath)) {
                    QFileInfo fi(fullPath);
                    QString savePath = QFileDialog::getSaveFileName(this, "Export Document", fi.fileName(), "All Files (*.*)");
                    if (!savePath.isEmpty()) {
                        // QFile::copy fails if the destination exists — remove first
                        // and report the real result instead of false success.
                        if (QFile::exists(savePath)) QFile::remove(savePath);
                        if (QFile::copy(fullPath, savePath)) {
                            QMessageBox::information(this, "Success", "Document copy saved!");
                        } else {
                            QMessageBox::warning(this, "Export Failed",
                                "Could not save a copy (check disk space and permissions).");
                        }
                    }
                }
            }
        } else if (sel == actCopyInfo) {
            QString info = QString("Document: %1 | Date: %2 | Category: %3").arg(docTitle, docDate, docCategory);
            QGuiApplication::clipboard()->setText(info);
        } else if (sel == actDelete && !isReadOnly) {
            onDeleteClicked();
        }
    });
    vaultLay->addWidget(m_tableView);

    tabs->addTab(tabVault, "Document Vault");

    // --- Tab 2: Board Minutes & Motion Tracker ---
    QWidget* tabMinutes = new QWidget(this);
    QVBoxLayout* minLay = new QVBoxLayout(tabMinutes);
    minLay->setContentsMargins(15, 15, 15, 15);
    minLay->setSpacing(12);

    QHBoxLayout* minControls = new QHBoxLayout();
    m_btnAddMinutes = new QPushButton(" Log Board Motion / Minutes", this);
    m_btnAddMinutes->setIcon(IconManager::instance().getPrimaryIcon("plus", 14));
    m_btnAddMinutes->setObjectName("primaryBtn");
    connect(m_btnAddMinutes, &QPushButton::clicked, this, &VaultWidget::onAddBoardMinutes);
    minControls->addWidget(m_btnAddMinutes);

    m_btnDeleteMinutes = new QPushButton(" Remove Motion Entry", this);
    m_btnDeleteMinutes->setIcon(IconManager::instance().getDangerIcon("trash-2", 14));
    m_btnDeleteMinutes->setObjectName("dangerBtn");
    connect(m_btnDeleteMinutes, &QPushButton::clicked, this, &VaultWidget::onDeleteMinutes);
    minControls->addWidget(m_btnDeleteMinutes);

    m_btnExportMinutesPDF = new QPushButton(" Export Board Minutes (PDF)", this);
    m_btnExportMinutesPDF->setIcon(IconManager::instance().getPrimaryIcon("download", 14));
    m_btnExportMinutesPDF->setObjectName("saveBtn");
    connect(m_btnExportMinutesPDF, &QPushButton::clicked, this, &VaultWidget::onExportMinutesPDF);
    minControls->addWidget(m_btnExportMinutesPDF);

    minControls->addStretch();
    minLay->addLayout(minControls);

    m_minutesView = new QTableView(this);
    m_minutesModel = new QSqlQueryModel(this);
    m_minutesView->setModel(m_minutesModel);
    m_minutesView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_minutesView->setAlternatingRowColors(true);
    m_minutesView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_minutesView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_minutesView, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QModelIndex idx = m_minutesView->indexAt(pos);
        if (!idx.isValid()) return;
        m_minutesView->selectRow(idx.row());

        int mRow = idx.row();
        QString mDate = m_minutesModel->index(mRow, 1).data().toString();
        QString mTitle = m_minutesModel->index(mRow, 2).data().toString();
        QString mMoved = m_minutesModel->index(mRow, 3).data().toString();
        QString mSeconded = m_minutesModel->index(mRow, 4).data().toString();
        QString mResult = m_minutesModel->index(mRow, 5).data().toString();

        const ThemeTokens tok = ThemeManager::instance().currentTokens();
        const bool isReadOnly = (m_userRole == "readonly" || m_userRole == "guest");
        const QString roTooltip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";

        QMenu menu(this);
        QAction* actExport = menu.addAction(IconManager::instance().getIcon("file-text", tok.accentPrimary, 14),
                                            "Export Board Minutes (PDF)");
        QAction* actCopy = menu.addAction(IconManager::instance().getIcon("copy", QColor(), 14),
                                          "Copy Motion Summary");
        menu.addSeparator();
        QAction* actDelete = menu.addAction(IconManager::instance().getDangerIcon("trash-2", 14),
                                            "Remove Motion Entry");
        if (isReadOnly) {
            actDelete->setEnabled(false);
            actDelete->setToolTip(roTooltip);
        }

        QAction* sel = menu.exec(m_minutesView->viewport()->mapToGlobal(pos));
        if (!sel) return;

        if (sel == actExport) {
            onExportMinutesPDF();
        } else if (sel == actCopy) {
            QString summary = QString("[%1] %2 | Moved by: %3 | Seconded by: %4 | Result: %5")
                                  .arg(mDate, mTitle, mMoved, mSeconded, mResult);
            QGuiApplication::clipboard()->setText(summary);
        } else if (sel == actDelete && !isReadOnly) {
            onDeleteMinutes();
        }
    });
    minLay->addWidget(m_minutesView);

    tabs->addTab(tabMinutes, "📜 Board Minutes & Motion Tracker");

    mainLayout->addWidget(tabs);
}

void VaultWidget::refreshVault() {
    // Person/group links are optional (NULL for general documents); LEFT
    // JOINs keep unlinked rows visible.
    m_queryModel->setQuery(
        "SELECT id AS \"ID\", "
        "       title AS \"Document Title\", "
        "       meeting_date AS \"Date\", "
        "       category AS \"Category\", "
        "       COALESCE(p.first_name || ' ' || p.last_name, '—') AS \"Person\", "
        "       COALESCE(g.name, '—') AS \"Group\", "
        "       file_path AS \"Filename\" "
        "FROM vault_documents v "
        "LEFT JOIN people p ON v.person_id = p.id "
        "LEFT JOIN groups g ON v.group_id = g.id "
        "ORDER BY v.meeting_date DESC"
    );
    m_tableView->hideColumn(0); // Hide ID column
}

void VaultWidget::onUploadClicked() {
    // Signup sheets are often photographed rather than scanned, so accept
    // images alongside PDFs.
    QString selectedFile = QFileDialog::getOpenFileName(
        this, "Upload Document", QString(), "Documents (*.pdf *.jpg *.jpeg *.png)");
    if (selectedFile.isEmpty()) return;

    QDialog dlg(this);
    dlg.setWindowTitle("Document Metadata");
    dlg.resize(400, 300);

    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    QFormLayout* form = new QFormLayout();

    QLineEdit* titleEdit = new QLineEdit(&dlg);
    titleEdit->setPlaceholderText("Auto-generated — edit if you like");
    form->addRow("Title:", titleEdit);

    QDateEdit* dateEdit = new QDateEdit(QDate::currentDate(), &dlg);
    dateEdit->setCalendarPopup(true);
    form->addRow("Document Date:", dateEdit);

    QComboBox* categoryBox = new QComboBox(&dlg);
    categoryBox->addItems({"Membership", "Meeting Minutes", "Legal", "Financial", "Sign-up Sheets", "Other"});
    form->addRow("Category:", categoryBox);

    // Optional links: membership documents belong to a person, sign-up
    // sheets to a ministry. Rows show/hide with the category.
    QComboBox* groupCombo = new QComboBox(&dlg);
    groupCombo->addItem("— none —", 0);
    {
        QSqlQuery gq;
        gq.exec("SELECT id, name FROM groups ORDER BY name");
        while (gq.next()) groupCombo->addItem(gq.value(1).toString(), gq.value(0).toInt());
    }

    QComboBox* personCombo = new QComboBox(&dlg);
    auto populateVaultPeople = [personCombo, groupCombo]() {
        int previousSelectedId = personCombo->currentData().toInt();
        personCombo->clear();
        personCombo->addItem("— none —", 0);

        int groupId = groupCombo->currentData().toInt();
        if (groupId > 0) {
            QSqlQuery pq;
            pq.prepare(
                "SELECT p.id, p.first_name || ' ' || p.last_name || ' (' || COALESCE(er.relationship_type, 'Member') || ')' "
                "FROM entity_relationships er "
                "JOIN people p ON er.source_id = p.id "
                "WHERE er.target_type = 'Group' AND er.target_id = ? "
                "ORDER BY (er.relationship_type LIKE '%Leader%') DESC, p.last_name, p.first_name");
            pq.addBindValue(groupId);
            if (pq.exec()) {
                bool hasMembers = false;
                while (pq.next()) {
                    personCombo->addItem(pq.value(1).toString(), pq.value(0).toInt());
                    hasMembers = true;
                }
                if (hasMembers) {
                    personCombo->insertSeparator(personCombo->count());
                }
            }
        }

        QSqlQuery pqAll("SELECT id, first_name || ' ' || last_name FROM people ORDER BY last_name, first_name");
        while (pqAll.next()) {
            int pid = pqAll.value(0).toInt();
            if (personCombo->findData(pid) < 0) {
                personCombo->addItem(pqAll.value(1).toString(), pid);
            }
        }

        if (previousSelectedId > 0) {
            int idx = personCombo->findData(previousSelectedId);
            if (idx >= 0) personCombo->setCurrentIndex(idx);
        }
    };
    connect(groupCombo, &QComboBox::currentIndexChanged, &dlg, populateVaultPeople);
    populateVaultPeople();

    const int personRow = form->rowCount();
    form->addRow("Person:", personCombo);
    const int groupRow = form->rowCount();
    form->addRow("Group:", groupCombo);
#if QT_VERSION < QT_VERSION_CHECK(6, 4, 0)
    // Row indices are only consumed by setRowVisible (Qt >= 6.4); silence
    // -Wunused-variable on the older-Qt Linux build.
    Q_UNUSED(personRow);
    Q_UNUSED(groupRow);
#endif

    // Auto-title ("Category — Person/Group — date") unless the user edits it.
    bool titleTouched = false;
    connect(titleEdit, &QLineEdit::textEdited, [&titleTouched]() { titleTouched = true; });
    auto refreshDefaultTitle = [=]() {
        if (titleTouched) return;
        QString who;
        if (categoryBox->currentText() == "Membership" && personCombo->currentData().toInt() > 0)
            who = personCombo->currentText();
        else if (categoryBox->currentText() == "Sign-up Sheets" && groupCombo->currentData().toInt() > 0)
            who = groupCombo->currentText();
        titleEdit->setText(who.isEmpty()
            ? QString("%1 — %2").arg(categoryBox->currentText(), dateEdit->date().toString("yyyy-MM-dd"))
            : QString("%1 — %2 — %3").arg(categoryBox->currentText(), who, dateEdit->date().toString("yyyy-MM-dd")));
    };
    auto updateLinkRows = [=]() {
        const QString cat = categoryBox->currentText();
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
        form->setRowVisible(personRow, cat == "Membership");
        form->setRowVisible(groupRow, cat == "Sign-up Sheets");
#endif
        refreshDefaultTitle();
    };
    connect(categoryBox, &QComboBox::currentTextChanged, &dlg, updateLinkRows);
    connect(personCombo, &QComboBox::currentIndexChanged, &dlg, refreshDefaultTitle);
    connect(groupCombo, &QComboBox::currentIndexChanged, &dlg, refreshDefaultTitle);
    updateLinkRows();

    lay->addLayout(form);
    lay->addSpacing(15);

    QHBoxLayout* actions = new QHBoxLayout();
    QPushButton* btnCancel = new QPushButton("Cancel", &dlg);
    btnCancel->setObjectName("secondaryBtn");
    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    actions->addWidget(btnCancel);

    QPushButton* btnConfirm = new QPushButton("Upload", &dlg);
    btnConfirm->setObjectName("saveBtn");
    connect(btnConfirm, &QPushButton::clicked, &dlg, [&]() {
        QString title = titleEdit->text().trimmed();
        if (title.isEmpty()) {
            QMessageBox::warning(&dlg, "Required", "Please provide a document title.");
            return;
        }
        
        QFileInfo info(selectedFile);
        QString baseName = info.fileName();
        QString destPath = vaultDirPath() + "/" + baseName;
        if (QFile::exists(destPath)) {
            // Avoid collision
            destPath = vaultDirPath() + "/" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "_" + baseName;
        }

        if (!QFile::copy(selectedFile, destPath)) {
            QMessageBox::critical(&dlg, "File Error", "Could not copy selected document to local Vault storage.");
            return;
        }

        QString finalFilename = QFileInfo(destPath).fileName();
        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery insert(db);
        insert.prepare("INSERT INTO vault_documents (title, meeting_date, category, file_path, person_id, group_id) "
                       "VALUES (?, ?, ?, ?, ?, ?)");
        insert.addBindValue(title);
        insert.addBindValue(dateEdit->date().toString("yyyy-MM-dd"));
        insert.addBindValue(categoryBox->currentText());
        insert.addBindValue(finalFilename);
        const int personId = personCombo->currentData().toInt();
        const int groupId = groupCombo->currentData().toInt();
        insert.addBindValue(personId > 0 ? QVariant(personId) : QVariant());
        insert.addBindValue(groupId > 0 ? QVariant(groupId) : QVariant());

        if (insert.exec()) {
            dlg.accept();
        } else {
            QFile::remove(destPath); // Clean up
            QMessageBox::critical(&dlg, "DB Error", "Failed to register document in ledger: " + insert.lastError().text());
        }
    });
    actions->addWidget(btnConfirm);
    lay->addLayout(actions);

    if (dlg.exec() == QDialog::Accepted) {
        refreshVault();
    }
}

void VaultWidget::onOpenClicked() {
    QModelIndex current = m_tableView->currentIndex();
    if (!current.isValid()) {
        QMessageBox::warning(this, "No Selection", "Please select a document to open.");
        return;
    }

    // Column order: ID, Title, Date, Category, Person, Group, Filename
    int row = current.row();
    QString fileName = m_queryModel->index(row, 6).data().toString();
    QString absolutePath = vaultDirPath() + "/" + fileName;

    if (!QFile::exists(absolutePath)) {
        QMessageBox::critical(this, "File Missing", QString("The requested document was not found at:\n%1\n\nIt may have been deleted or moved.").arg(absolutePath));
        return;
    }

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath))) {
        QMessageBox::critical(this, "Launch Error", "Could not open document. Make sure you have a registered PDF reader installed.");
    }
}

void VaultWidget::onDeleteClicked() {
    QModelIndex current = m_tableView->currentIndex();
    if (!current.isValid()) {
        QMessageBox::warning(this, "No Selection", "Please select a document entry to remove.");
        return;
    }

    int row = current.row();
    const int id = m_queryModel->index(row, 0).data().toInt();
    const QString title = m_queryModel->index(row, 1).data().toString();
    const QString fileName = m_queryModel->index(row, 6).data().toString();

    // Two exits: keep the file for record-only removals, or delete both.
    // (The old behavior was record-only with no way to ever remove a file,
    // which orphaned everything over time.)
    QMessageBox box(this);
    box.setWindowTitle("Remove Document");
    box.setText(QString("Remove '%1'?\n\nChoose whether the stored file should also be deleted.").arg(title));
    QPushButton* keepFile = box.addButton("Remove record only (keep file)", QMessageBox::ActionRole);
    QPushButton* deleteFile = box.addButton("Remove record + delete file", QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();

    const bool alsoDeleteFile = (box.clickedButton() == deleteFile);
    if (!alsoDeleteFile && box.clickedButton() != keepFile) return; // cancelled

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    query.prepare("DELETE FROM vault_documents WHERE id = ?");
    query.addBindValue(id);

    if (query.exec()) {
        if (alsoDeleteFile) {
            const QString abs = vaultDirPath() + "/" + fileName;
            if (QFile::exists(abs) && !QFile::remove(abs)) {
                QMessageBox::warning(this, "File Not Removed",
                    QString("The record was removed but the file could not be deleted:\n%1\nUse Sweep Orphans later.").arg(abs));
            }
        }
        DatabaseManager::logAudit("vault_documents", id, "record", title,
                                  alsoDeleteFile ? "DELETED (record + file)" : "DELETED (record only)", "admin");
        refreshVault();
    } else {
        QMessageBox::critical(this, "DB Error", "Failed to delete document: " + query.lastError().text());
    }
}

void VaultWidget::onSweepOrphans() {
    // Files on disk with no DB row: leftovers from demo purges and from
    // record-only removals. Offer to delete them in one pass.
    QSet<QString> referenced;
    QSqlQuery q;
    q.exec("SELECT file_path FROM vault_documents");
    while (q.next()) referenced.insert(q.value(0).toString());

    const QString dir = vaultDirPath();
    QStringList orphans;
    const auto files = QDir(dir).entryInfoList(QDir::Files);
    for (const QFileInfo& fi : files) {
        if (!referenced.contains(fi.fileName())) orphans.append(fi.fileName());
    }

    if (orphans.isEmpty()) {
        QMessageBox::information(this, "Sweep Orphans", "No orphaned files — every file in the vault is linked to a record. 🎉");
        return;
    }

    const auto res = QMessageBox::question(this, "Sweep Orphans",
        QString("Found %1 file(s) on disk with no database record:\n\n%2\n\nDelete these files permanently?")
            .arg(QString::number(orphans.size()), orphans.join("\n")),
        QMessageBox::Yes | QMessageBox::No);
    if (res != QMessageBox::Yes) return;

    int removed = 0;
    for (const QString& name : orphans) {
        if (QFile::remove(dir + "/" + name)) removed++;
    }
    QMessageBox::information(this, "Sweep Orphans",
        QString("Deleted %1 of %2 orphaned file(s).").arg(QString::number(removed), QString::number(orphans.size())));
}

void VaultWidget::applyRole(const QString& role) {
    m_userRole = role;
    // Roles only gate editing vs read-only; a Guest -> Admin sign-in must
    // re-enable the controls (otherwise Vault stays latched disabled).
    const bool isReadOnly = (role == "readonly" || role == "guest");
    const QString roTip = "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify.";
    const QString tip = isReadOnly ? roTip : QString();

    m_btnUpload->setEnabled(!isReadOnly);
    m_btnDelete->setEnabled(!isReadOnly);
    m_btnSweep->setEnabled(!isReadOnly);
    m_btnAddMinutes->setEnabled(!isReadOnly);
    m_btnDeleteMinutes->setEnabled(!isReadOnly);

    m_btnUpload->setToolTip(tip);
    m_btnDelete->setToolTip(tip);
    m_btnSweep->setToolTip(tip);
    m_btnAddMinutes->setToolTip(tip);
    m_btnDeleteMinutes->setToolTip(tip);
}

void VaultWidget::onSearchChanged(const QString& text) {
    QString filter = "%" + text.trimmed() + "%";
    QSqlQuery query(DatabaseManager::instance().database());
    query.prepare("SELECT id AS \"ID\", "
                  "       title AS \"Document Title\", "
                  "       meeting_date AS \"Date\", "
                  "       category AS \"Category\", "
                  "       file_path AS \"Filename\" "
                  "FROM vault_documents "
                  "WHERE title LIKE ? OR category LIKE ? "
                  "ORDER BY meeting_date DESC");
    query.addBindValue(filter);
    query.addBindValue(filter);
    if (query.exec()) {
        m_queryModel->setQuery(std::move(query));
    }
}

void VaultWidget::refreshBoardMinutes() {
    QSqlDatabase db = DatabaseManager::instance().database();
    m_minutesModel->setQuery(
        "SELECT id AS \"ID\", "
        "       meeting_date AS \"Meeting Date\", "
        "       motion_title AS \"Motion / Resolution\", "
        "       moved_by AS \"Moved By\", "
        "       seconded_by AS \"Seconded By\", "
        "       vote_result AS \"Vote Result\" "
        "FROM board_motions "
        "ORDER BY meeting_date DESC", db);
    m_minutesView->hideColumn(0);
}

void VaultWidget::onAddBoardMinutes() {
    QDialog dlg(this);
    dlg.setWindowTitle("Log Board Motion / Minutes");
    dlg.resize(380, 280);

    QFormLayout* form = new QFormLayout(&dlg);
    form->setSpacing(12);

    QDateEdit* dateEdit = new QDateEdit(QDate::currentDate(), &dlg);
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat("yyyy-MM-dd");

    QLineEdit* motionEdit = new QLineEdit(&dlg);
    motionEdit->setPlaceholderText("e.g. Approved $5,000 for roof repairs");

    QLineEdit* moverEdit = new QLineEdit(&dlg);
    moverEdit->setPlaceholderText("Trustee / Elder name");

    QLineEdit* seconderEdit = new QLineEdit(&dlg);
    seconderEdit->setPlaceholderText("Trustee / Elder name");

    QComboBox* resultCombo = new QComboBox(&dlg);
    resultCombo->addItems({"Passed (Unanimous)", "Passed (Majority)", "Tabled / Postponed", "Failed / Rejected"});

    QLineEdit* detailsEdit = new QLineEdit(&dlg);
    detailsEdit->setPlaceholderText("Optional notes or conditions");

    form->addRow("Meeting Date:", dateEdit);
    form->addRow("Motion / Resolution:", motionEdit);
    form->addRow("Moved By:", moverEdit);
    form->addRow("Seconded By:", seconderEdit);
    form->addRow("Vote Result:", resultCombo);
    form->addRow("Notes / Details:", detailsEdit);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() == QDialog::Accepted) {
        QString mDate = dateEdit->date().toString("yyyy-MM-dd");
        QString title = motionEdit->text().trimmed();
        QString mover = moverEdit->text().trimmed();
        QString seconder = seconderEdit->text().trimmed();
        QString result = resultCombo->currentText();
        QString details = detailsEdit->text().trimmed();

        if (title.isEmpty() || mover.isEmpty()) {
            QMessageBox::warning(this, "Validation Error", "Motion title and mover name are required.");
            return;
        }

        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery q(db);
        q.prepare("INSERT INTO board_motions (meeting_date, motion_title, moved_by, seconded_by, vote_result, details) "
                  "VALUES (?, ?, ?, ?, ?, ?)");
        q.addBindValue(mDate);
        q.addBindValue(title);
        q.addBindValue(mover);
        q.addBindValue(seconder);
        q.addBindValue(result);
        q.addBindValue(details);

        if (q.exec()) {
            refreshBoardMinutes();
            QMessageBox::information(this, "Motion Saved", "Board motion logged to official minutes history.");
        } else {
            QMessageBox::critical(this, "Error", "Failed to log motion: " + q.lastError().text());
        }
    }
}

void VaultWidget::onDeleteMinutes() {
    int row = m_minutesView->currentIndex().row();
    if (row < 0) {
        QMessageBox::warning(this, "Selection Required", "Please select a motion entry to remove.");
        return;
    }
    int mId = m_minutesModel->index(row, 0).data().toInt();
    QString title = m_minutesModel->index(row, 2).data().toString();

    if (QMessageBox::question(this, "Confirm Removal", QString("Delete board motion log for '%1'?").arg(title), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery q(db);
        q.prepare("DELETE FROM board_motions WHERE id = ?");
        q.addBindValue(mId);
        if (q.exec()) {
            refreshBoardMinutes();
        }
    }
}

void VaultWidget::onExportMinutesPDF() {
    QString savePath = QFileDialog::getSaveFileName(this, "Export Board Minutes & Motion History (PDF)", 
                                                     QString("board_minutes_%1.pdf").arg(QDate::currentDate().toString("yyyy_MM_dd")), 
                                                     "PDF Document (*.pdf)");
    if (savePath.isEmpty()) return;

    QSettings settings("OpenEkklesia", "CHMS");
    QString churchName = settings.value("churchName", "OpenEkklesia Church").toString();

    QString html =
        "<html><head><style>"
        "body { font-family: sans-serif; margin: 20px; color: #11111b; }"
        "h1 { color: #1e66f5; border-bottom: 2px solid #1e66f5; padding-bottom: 5px; }"
        "table { width: 100%; border-collapse: collapse; margin-top: 15px; }"
        "th { background-color: #f4f4f4; text-align: left; padding: 8px; border: 1px solid #ddd; font-size: 11px; }"
        "td { padding: 8px; border: 1px solid #ddd; font-size: 10px; }"
        ".footer { margin-top: 30px; font-size: 9px; color: #777; font-style: italic; }"
        "</style></head><body>"
        + ChurchProfile::renderHtmlHeader("Official Board Minutes & Motion History",
                                          QString("Generated %1").arg(AppDateFormatter::formatDate(QDate::currentDate())),
                                          /*includeTaxId=*/true)
        + "<table>"
          "<tr><th>Meeting Date</th><th>Motion / Resolution</th><th>Moved By</th><th>Seconded By</th><th>Vote Result</th></tr>";

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q("SELECT meeting_date, motion_title, moved_by, seconded_by, vote_result FROM board_motions ORDER BY meeting_date DESC", db);
    while (q.next()) {
        html += QString("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td><strong>%5</strong></td></tr>")
                .arg(q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(), q.value(4).toString());
    }
    html += "</table><div class=\"footer\">Recorded and Certified by Church Secretary via OpenEkklesia ChMS</div></body></html>";

    QTextDocument doc;
    doc.setHtml(html);

    QPrinter printer(QPrinter::PrinterResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(savePath);
    printer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);

    doc.print(&printer);
    QFileInfo doneInfo(savePath);
    if (!doneInfo.exists() || doneInfo.size() == 0) {
        QMessageBox::warning(this, "Export Failed",
            "Could not write minutes PDF (check disk space and permissions).");
        return;
    }
    QMessageBox::information(this, "PDF Export Complete", QString("Board Minutes PDF successfully exported to:\n%1").arg(savePath));
}
