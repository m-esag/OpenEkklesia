#include "vaultwidget.h"
#include "../database/databasemanager.h"
#include <QSqlQuery>
#include <QSqlError>
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
#include <QLineEdit>
#include <QDateEdit>
#include <QComboBox>
#include <QHeaderView>
#include <QTextStream>
#include <QPrinter>
#include <QTextDocument>
#include <QSettings>

VaultWidget::VaultWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    refreshVault();
    refreshBoardMinutes();
}

VaultWidget::~VaultWidget() {}

QString VaultWidget::vaultDirPath() const {
    QString path = QDir::homePath() + "/.local/share/openekklesia/vault";
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
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
    m_searchEdit->setPlaceholderText("🔍 Search documents...");
    m_searchEdit->setFixedWidth(220);
    m_searchEdit->setObjectName("searchEdit");
    connect(m_searchEdit, &QLineEdit::textChanged, this, &VaultWidget::onSearchChanged);
    controls->addWidget(m_searchEdit);

    m_btnUpload = new QPushButton("+ Upload Document (PDF)", this);
    m_btnUpload->setObjectName("saveBtn");
    connect(m_btnUpload, &QPushButton::clicked, this, &VaultWidget::onUploadClicked);
    controls->addWidget(m_btnUpload);

    m_btnOpen = new QPushButton("👁 Open / View File", this);
    m_btnOpen->setObjectName("primaryBtn");
    connect(m_btnOpen, &QPushButton::clicked, this, &VaultWidget::onOpenClicked);
    controls->addWidget(m_btnOpen);

    m_btnDelete = new QPushButton("Remove Record", this);
    m_btnDelete->setObjectName("dangerBtn");
    connect(m_btnDelete, &QPushButton::clicked, this, &VaultWidget::onDeleteClicked);
    controls->addWidget(m_btnDelete);

    controls->addStretch();
    vaultLay->addLayout(controls);

    m_tableView = new QTableView(this);
    m_queryModel = new QSqlQueryModel(this);
    m_tableView->setModel(m_queryModel);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    vaultLay->addWidget(m_tableView);

    tabs->addTab(tabVault, "📁 Document Vault");

    // --- Tab 2: Board Minutes & Motion Tracker ---
    QWidget* tabMinutes = new QWidget(this);
    QVBoxLayout* minLay = new QVBoxLayout(tabMinutes);
    minLay->setContentsMargins(15, 15, 15, 15);
    minLay->setSpacing(12);

    QHBoxLayout* minControls = new QHBoxLayout();
    m_btnAddMinutes = new QPushButton("📜 Log Board Motion / Minutes", this);
    m_btnAddMinutes->setObjectName("primaryBtn");
    connect(m_btnAddMinutes, &QPushButton::clicked, this, &VaultWidget::onAddBoardMinutes);
    minControls->addWidget(m_btnAddMinutes);

    m_btnDeleteMinutes = new QPushButton("🗑️ Remove Motion Entry", this);
    m_btnDeleteMinutes->setObjectName("dangerBtn");
    connect(m_btnDeleteMinutes, &QPushButton::clicked, this, &VaultWidget::onDeleteMinutes);
    minControls->addWidget(m_btnDeleteMinutes);

    m_btnExportMinutesPDF = new QPushButton("📄 Export Board Minutes (PDF)", this);
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
    minLay->addWidget(m_minutesView);

    tabs->addTab(tabMinutes, "📜 Board Minutes & Motion Tracker");

    mainLayout->addWidget(tabs);
}

void VaultWidget::refreshVault() {
    m_queryModel->setQuery(
        "SELECT id AS \"ID\", "
        "       title AS \"Document Title\", "
        "       meeting_date AS \"Date\", "
        "       category AS \"Category\", "
        "       file_path AS \"Filename\" "
        "FROM vault_documents "
        "ORDER BY meeting_date DESC"
    );
    m_tableView->hideColumn(0); // Hide ID column
}

void VaultWidget::onUploadClicked() {
    QString selectedFile = QFileDialog::getOpenFileName(this, "Upload PDF", QString(), "PDF Files (*.pdf)");
    if (selectedFile.isEmpty()) return;

    QDialog dlg(this);
    dlg.setWindowTitle("Document Metadata");
    dlg.resize(350, 220);
    dlg.setStyleSheet("background-color: #1e1e2e; color: #cdd6f4;");

    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    QFormLayout* form = new QFormLayout();

    QLineEdit* titleEdit = new QLineEdit(&dlg);
    titleEdit->setPlaceholderText("E.g. Q2 Elder Minutes...");
    titleEdit->setStyleSheet("background-color: #313244; color: #cdd6f4; border: 1px solid #45475a; padding: 6px; border-radius: 4px;");
    form->addRow("Title:", titleEdit);

    QDateEdit* dateEdit = new QDateEdit(QDate::currentDate(), &dlg);
    dateEdit->setCalendarPopup(true);
    dateEdit->setStyleSheet("background-color: #313244; color: #cdd6f4; border: 1px solid #45475a; padding: 6px; border-radius: 4px;");
    form->addRow("Document Date:", dateEdit);

    QComboBox* categoryBox = new QComboBox(&dlg);
    categoryBox->addItems({"Board Minutes", "Financial Bylaws", "Legal Documents", "Other"});
    categoryBox->setStyleSheet("background-color: #313244; color: #cdd6f4; border: 1px solid #45475a; padding: 6px; border-radius: 4px;");
    form->addRow("Category:", categoryBox);

    lay->addLayout(form);
    lay->addSpacing(15);

    QHBoxLayout* actions = new QHBoxLayout();
    QPushButton* btnCancel = new QPushButton("Cancel", &dlg);
    btnCancel->setStyleSheet("background-color: #313244; color: #cdd6f4; border: 1px solid #45475a; padding: 8px; border-radius: 5px;");
    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    actions->addWidget(btnCancel);

    QPushButton* btnConfirm = new QPushButton("Upload", &dlg);
    btnConfirm->setStyleSheet("background-color: #a6e3a1; color: #11111b; font-weight: bold; padding: 8px; border-radius: 5px;");
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
        insert.prepare("INSERT INTO vault_documents (title, meeting_date, category, file_path) VALUES (?, ?, ?, ?)");
        insert.addBindValue(title);
        insert.addBindValue(dateEdit->date().toString("yyyy-MM-dd"));
        insert.addBindValue(categoryBox->currentText());
        insert.addBindValue(finalFilename);

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

    // Retrieve file_path (column 4)
    int row = current.row();
    QString fileName = m_queryModel->index(row, 4).data().toString();
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

    auto res = QMessageBox::question(this, "Remove Document", "Are you sure you want to remove this document registration? (Physical file remains safe in Vault).", QMessageBox::Yes | QMessageBox::No);
    if (res == QMessageBox::No) return;

    int row = current.row();
    int id = m_queryModel->index(row, 0).data().toInt();

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery query(db);
    query.prepare("DELETE FROM vault_documents WHERE id = ?");
    query.addBindValue(id);
    
    if (query.exec()) {
        refreshVault();
    } else {
        QMessageBox::critical(this, "DB Error", "Failed to delete document: " + query.lastError().text());
    }
}

void VaultWidget::applyRole(const QString& role) {
    if (role == "readonly" || role == "finance") {
        m_btnUpload->setEnabled(false);
        m_btnDelete->setEnabled(false);
    }
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

    QString html = QString(
        "<html><head><style>"
        "body { font-family: sans-serif; margin: 20px; color: #1e1e2e; }"
        "h1 { color: #1e1e2e; border-bottom: 2px solid #89b4fa; padding-bottom: 5px; }"
        "table { width: 100%; border-collapse: collapse; margin-top: 15px; }"
        "th { background-color: #f4f4f4; text-align: left; padding: 8px; border: 1px solid #ddd; font-size: 11px; }"
        "td { padding: 8px; border: 1px solid #ddd; font-size: 10px; }"
        ".footer { margin-top: 30px; font-size: 9px; color: #777; font-style: italic; }"
        "</style></head><body>"
        "<h1>%1 — Official Board Minutes & Motion History</h1>"
        "<p><strong>Generated Date:</strong> %2</p>"
        "<table>"
        "<tr><th>Meeting Date</th><th>Motion / Resolution</th><th>Moved By</th><th>Seconded By</th><th>Vote Result</th></tr>"
    ).arg(churchName, QDate::currentDate().toString("MMMM d, yyyy"));

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
    QMessageBox::information(this, "PDF Export Complete", QString("Board Minutes PDF successfully exported to:\n%1").arg(savePath));
}
