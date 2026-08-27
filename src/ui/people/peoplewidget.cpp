#include "peoplewidget.h"
#include "ministryarchetypes.h"
#include "../database/databasemanager.h"
#include <QDesktopServices>
#include <QUrl>
#include "../core/thememanager.h"
#include "../core/iconmanager.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QSplitter>
#include <QFileDialog>
#include <QMessageBox>
#include <QPdfWriter>
#include <QTextDocument>
#include <QSqlRecord>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRelationalDelegate>
#include <QItemDelegate>
#include <QDir>
#include <QPixmap>
#include <QInputDialog>
#include <QDate>
#include <QDebug>
#include <QClipboard>
#include <QGuiApplication>
#include <QScrollArea>
#include <QRadioButton>
#include <QCheckBox>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFrame>
#include <QSpacerItem>
#include <QStackedWidget>
#include <QShortcut>
#include <QKeySequence>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QMenu>
#include <QDialogButtonBox>
#include <QSettings>
#include "../common/appcurrency.h"
#include "../common/churchprofile.h"
#include "../common/appdateformatter.h"

// ============================================================================
// ClickableAvatarLabel Implementation
// ============================================================================
void ClickableAvatarLabel::paintEvent(QPaintEvent* event) {
    QLabel::paintEvent(event);

    if (m_isHovered) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // Semi-transparent dark circular overlay
        QColor overlayColor(17, 17, 27, 170);
        painter.setBrush(QBrush(overlayColor));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(rect(), 55, 55);

        // White text badge
        painter.setPen(QPen(Qt::white));
        QFont f = font();
        f.setPointSize(10);
        f.setBold(true);
        painter.setFont(f);
        painter.drawText(rect(), Qt::AlignCenter, "📷 Photo");
    }
}

// ============================================================================
// StatusItemDelegate Implementation (Colored Pill Badges)
// ============================================================================
void StatusItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    QString status = index.data(Qt::DisplayRole).toString();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
    }

    QColor badgeBg;
    QColor badgeText;

    if (status == "Member") {
        badgeBg = QColor(46, 240, 152, 45); // Emerald Green
        badgeText = QColor("#2ef098");
    } else if (status == "Regular Attender") {
        badgeBg = QColor(180, 190, 254, 45); // Violet/Lavender
        badgeText = QColor("#b4befe");
    } else if (status == "Visitor") {
        badgeBg = QColor(249, 226, 175, 45); // Warm Gold
        badgeText = QColor("#f9e2af");
    } else { // Inactive or Unspecified
        badgeBg = QColor(108, 112, 134, 45); // Muted Slate
        badgeText = QColor("#a6adc8");
    }

    QRect badgeRect = option.rect.adjusted(6, 4, -6, -4);
    painter->setBrush(QBrush(badgeBg));
    painter->setPen(QPen(badgeText, 1));
    painter->drawRoundedRect(badgeRect, 8, 8);

    painter->setPen(QPen(badgeText));
    QFont font = option.font;
    font.setPointSize(9);
    font.setBold(true);
    painter->setFont(font);
    painter->drawText(badgeRect, Qt::AlignCenter, status.isEmpty() ? "Unassigned" : status);

    painter->restore();
}

// ============================================================================
// PeopleProxyModel Implementation (Merged Name Column & Sorting)
// ============================================================================
QVariant PeopleProxyModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return QVariant();

    int col = index.column();

    if (role == Qt::DisplayRole) {
        QModelIndex srcRowIndex = mapToSource(index.sibling(index.row(), 0));
        int srcRow = srcRowIndex.row();

        if (col == 0) { // Merged Name: "Last Name, First Name" with Care Flag dot
            QString firstName = sourceModel()->data(sourceModel()->index(srcRow, 2)).toString();
            QString lastName = sourceModel()->data(sourceModel()->index(srcRow, 3)).toString();
            QString careFlag = sourceModel()->data(sourceModel()->index(srcRow, 9)).toString();
            QString prefix;
            if (careFlag == "Needs Contact" || careFlag == "At-Risk" || careFlag == "At Risk") {
                prefix = "🔴 ";
            } else if (careFlag == "Follow-up" || careFlag == "Grief Support") {
                prefix = "🟡 ";
            } else if (careFlag == "Leader") {
                prefix = "⭐ ";
            }
            QString name;
            if (lastName.isEmpty() && firstName.isEmpty()) name = "—";
            else if (lastName.isEmpty()) name = firstName;
            else if (firstName.isEmpty()) name = lastName;
            else name = QString("%1, %2").arg(lastName, firstName);
            return prefix + name;
        } else if (col == 1) { // Household / Family
            return sourceModel()->data(sourceModel()->index(srcRow, 1));
        } else if (col == 2) { // Phone
            return sourceModel()->data(sourceModel()->index(srcRow, 4));
        } else if (col == 3) { // Email
            return sourceModel()->data(sourceModel()->index(srcRow, 5));
        } else if (col == 4) { // Status
            return sourceModel()->data(sourceModel()->index(srcRow, 11));
        }
    } else if (role == Qt::ToolTipRole) {
        QModelIndex srcRowIndex = mapToSource(index.sibling(index.row(), 0));
        int srcRow = srcRowIndex.row();
        if (col == 0) {
            QString careFlag = sourceModel()->data(sourceModel()->index(srcRow, 9)).toString();
            QString status = sourceModel()->data(sourceModel()->index(srcRow, 11)).toString();
            QString tip = QString("Status: %1").arg(status.isEmpty() ? "Unassigned" : status);
            if (!careFlag.isEmpty() && careFlag != "Normal") {
                tip += QString("\nCare Flag: %1").arg(careFlag);
            }
            return tip;
        } else if (col == 2) {
            return "Click to select; Right-click to copy phone.";
        } else if (col == 3) {
            return "Click to select; Right-click to copy email.";
        }
    }

    return QSortFilterProxyModel::data(index, role);
}

QVariant PeopleProxyModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
            case 0: return "Name";
            case 1: return "Household / Family";
            case 2: return "Phone";
            case 3: return "Email";
            case 4: return "Status";
            default: return QVariant();
        }
    }
    return QSortFilterProxyModel::headerData(section, orientation, role);
}

bool PeopleProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    if (!sourceModel()) return false;

    // In QSortFilterProxyModel::lessThan, left and right are ALREADY source model indices
    int leftRow = left.row();
    int rightRow = right.row();

    auto cleanText = [](const QString& str) -> QString {
        QString s = str.normalized(QString::NormalizationForm_C).trimmed();
        while (!s.isEmpty() && (s.at(0).isSpace() || s.at(0) == QChar(0xFEFF) || s.at(0) == QChar(0x00A0))) {
            s.remove(0, 1);
        }
        return s;
    };

    // 1. Custom multi-field sort modes from m_cmbSortBy
    if (m_sortMode == SortByFirstName) {
        QString lFirst = cleanText(sourceModel()->data(sourceModel()->index(leftRow, 2)).toString());
        QString rFirst = cleanText(sourceModel()->data(sourceModel()->index(rightRow, 2)).toString());
        int cmp = QString::localeAwareCompare(lFirst, rFirst);
        if (cmp != 0) return cmp < 0;
        QString lLast = cleanText(sourceModel()->data(sourceModel()->index(leftRow, 3)).toString());
        QString rLast = cleanText(sourceModel()->data(sourceModel()->index(rightRow, 3)).toString());
        return QString::localeAwareCompare(lLast, rLast) < 0;
    }

    if (m_sortMode == SortByJoinDate) {
        QString lJd = sourceModel()->data(sourceModel()->index(leftRow, 13)).toString().trimmed();
        QString rJd = sourceModel()->data(sourceModel()->index(rightRow, 13)).toString().trimmed();
        if (lJd.isEmpty()) lJd = "0000-00-00";
        if (rJd.isEmpty()) rJd = "0000-00-00";
        return lJd < rJd;
    }

    if (m_sortMode == SortByBirthDate) {
        QString lDob = sourceModel()->data(sourceModel()->index(leftRow, 6)).toString().trimmed();
        QString rDob = sourceModel()->data(sourceModel()->index(rightRow, 6)).toString().trimmed();
        if (lDob.isEmpty()) lDob = "0000-00-00";
        if (rDob.isEmpty()) rDob = "0000-00-00";
        return lDob < rDob;
    }

    if (m_sortMode == SortByGender) {
        QString lG = cleanText(sourceModel()->data(sourceModel()->index(leftRow, 14)).toString());
        QString rG = cleanText(sourceModel()->data(sourceModel()->index(rightRow, 14)).toString());
        int cmp = QString::localeAwareCompare(lG, rG);
        if (cmp != 0) return cmp < 0;
        QString lLast = cleanText(sourceModel()->data(sourceModel()->index(leftRow, 3)).toString());
        QString rLast = cleanText(sourceModel()->data(sourceModel()->index(rightRow, 3)).toString());
        return QString::localeAwareCompare(lLast, rLast) < 0;
    }

    // 2. Column-based sorting (clicking headers or selecting standard sorts)
    int col = left.column();
    switch (col) {
        case 0: { // Name: Last Name, then First Name
            QString lLast = cleanText(sourceModel()->data(sourceModel()->index(leftRow, 3)).toString());
            QString rLast = cleanText(sourceModel()->data(sourceModel()->index(rightRow, 3)).toString());
            QString lFirst = cleanText(sourceModel()->data(sourceModel()->index(leftRow, 2)).toString());
            QString rFirst = cleanText(sourceModel()->data(sourceModel()->index(rightRow, 2)).toString());

            // If a contact has no last name, fall back to sorting by first name
            QString lKey = lLast.isEmpty() ? lFirst : lLast;
            QString rKey = rLast.isEmpty() ? rFirst : rLast;

            int cmp = QString::localeAwareCompare(lKey, rKey);
            if (cmp != 0) return cmp < 0;

            return QString::localeAwareCompare(lFirst, rFirst) < 0;
        }
        case 1: { // Household / Family (source col 1)
            QString lHouse = cleanText(sourceModel()->data(sourceModel()->index(leftRow, 1)).toString());
            QString rHouse = cleanText(sourceModel()->data(sourceModel()->index(rightRow, 1)).toString());
            if (lHouse.isEmpty() != rHouse.isEmpty()) {
                return rHouse.isEmpty(); // Empty households at the bottom
            }
            int cmp = QString::localeAwareCompare(lHouse, rHouse);
            if (cmp != 0) return cmp < 0;
            QString lLast = cleanText(sourceModel()->data(sourceModel()->index(leftRow, 3)).toString());
            QString rLast = cleanText(sourceModel()->data(sourceModel()->index(rightRow, 3)).toString());
            return QString::localeAwareCompare(lLast, rLast) < 0;
        }
        case 2: { // Phone: source col 4 (NOT source col 2 which is first_name!)
            QString lPhone = cleanText(sourceModel()->data(sourceModel()->index(leftRow, 4)).toString());
            QString rPhone = cleanText(sourceModel()->data(sourceModel()->index(rightRow, 4)).toString());
            return QString::localeAwareCompare(lPhone, rPhone) < 0;
        }
        case 3: { // Email: source col 5 (NOT source col 3 which is last_name!)
            QString lEmail = cleanText(sourceModel()->data(sourceModel()->index(leftRow, 5)).toString());
            QString rEmail = cleanText(sourceModel()->data(sourceModel()->index(rightRow, 5)).toString());
            return QString::localeAwareCompare(lEmail, rEmail) < 0;
        }
        case 4: { // Membership Status: source col 11 (NOT source col 4 which is phone!)
            QString lStat = cleanText(sourceModel()->data(sourceModel()->index(leftRow, 11)).toString());
            QString rStat = cleanText(sourceModel()->data(sourceModel()->index(rightRow, 11)).toString());
            int cmp = QString::localeAwareCompare(lStat, rStat);
            if (cmp != 0) return cmp < 0;
            QString lLast = cleanText(sourceModel()->data(sourceModel()->index(leftRow, 3)).toString());
            QString rLast = cleanText(sourceModel()->data(sourceModel()->index(rightRow, 3)).toString());
            return QString::localeAwareCompare(lLast, rLast) < 0;
        }
        default:
            break;
    }

    return QSortFilterProxyModel::lessThan(left, right);
}

// ============================================================================
// PeopleWidget Constructor & UI Setup
// ============================================================================
PeopleWidget::PeopleWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    setupModels();
}

PeopleWidget::~PeopleWidget() {}

void PeopleWidget::setupUI() {
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(15);

    // Keyboard shortcuts
    QShortcut* scSearch = new QShortcut(QKeySequence("Ctrl+F"), this);
    connect(scSearch, &QShortcut::activated, this, [this]() {
        if (m_searchEdit) { m_searchEdit->setFocus(); m_searchEdit->selectAll(); }
    });
    QShortcut* scNew = new QShortcut(QKeySequence("Ctrl+N"), this);
    connect(scNew, &QShortcut::activated, this, &PeopleWidget::onAddPerson);
    QShortcut* scSave = new QShortcut(QKeySequence("Ctrl+S"), this);
    connect(scSave, &QShortcut::activated, this, &PeopleWidget::onSaveProfile);
    QShortcut* scEsc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(scEsc, &QShortcut::activated, this, [this]() {
        if (m_searchEdit) m_searchEdit->clear();
        if (m_statusFilterGroup && m_statusFilterGroup->button(0)) m_statusFilterGroup->button(0)->setChecked(true);
        reapplyFilter();
    });

    // ----------------------------------------------------
    // LEFT PANEL: Master Directory Table
    // ----------------------------------------------------
    QWidget* leftPanel = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);

    QHBoxLayout* searchSortRow = new QHBoxLayout();
    searchSortRow->setSpacing(8);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("🔍 Search members (Ctrl+F)...");
    m_searchEdit->setObjectName("searchEdit");
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &PeopleWidget::onSearchTextChanged);

    m_cmbSortBy = new QComboBox(this);
    m_cmbSortBy->addItem("🔤 Last Name (A - Z)");
    m_cmbSortBy->addItem("🔤 Last Name (Z - A)");
    m_cmbSortBy->addItem("👤 First Name (A - Z)");
    m_cmbSortBy->addItem("👤 First Name (Z - A)");
    m_cmbSortBy->addItem("🏠 Family Name (A - Z)");
    m_cmbSortBy->addItem("🏷️ Membership Status");
    m_cmbSortBy->addItem("📅 Join Date (Newest)");
    m_cmbSortBy->addItem("📅 Join Date (Oldest)");
    m_cmbSortBy->addItem("🎂 Birth Date");
    m_cmbSortBy->addItem("♂️♀️ Gender");
    connect(m_cmbSortBy, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PeopleWidget::onSortByChanged);
    connect(m_cmbSortBy, QOverload<int>::of(&QComboBox::activated), this, &PeopleWidget::onSortByChanged);

    searchSortRow->addWidget(m_searchEdit, 3);
    QLabel* lblSort = new QLabel("Sort:", this);
    lblSort->setStyleSheet("font-weight: bold; font-size: 11px;");
    searchSortRow->addWidget(lblSort);
    searchSortRow->addWidget(m_cmbSortBy, 2);

    leftLayout->addLayout(searchSortRow);

    // Membership-status filter row
    QHBoxLayout* statusFilterRow = new QHBoxLayout();
    statusFilterRow->setSpacing(4);
    m_statusFilterGroup = new QButtonGroup(this);
    m_statusFilterGroup->setExclusive(true);

    static const QHash<int, QString> filters = {
        {0, "All"}, {1, "Active"}, {2, "Visitors"}, {3, "Attenders"},
        {4, "Members"}, {5, "Inactive"}
    };
    for (auto it = filters.constBegin(); it != filters.constEnd(); ++it) {
        QRadioButton* btn = new QRadioButton(it.value(), this);
        btn->setStyleSheet("QRadioButton { color: palette(text); padding: 2px 4px; font-size: 11px; }"
                           "QRadioButton::indicator { width: 11px; height: 11px; }");
        btn->setChecked(it.key() == 0);
        m_statusFilterGroup->addButton(btn, it.key());
        statusFilterRow->addWidget(btn);
    }
    statusFilterRow->addStretch();
    leftLayout->addLayout(statusFilterRow);
    connect(m_statusFilterGroup, &QButtonGroup::idClicked, this, &PeopleWidget::onStatusFilterChanged);

    m_peopleTable = new QTableView(this);
    m_peopleTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_peopleTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_peopleTable->verticalHeader()->setVisible(false);
    m_peopleTable->verticalHeader()->setDefaultSectionSize(34);
    m_peopleTable->setAlternatingRowColors(true);
    m_peopleTable->setSortingEnabled(true);
    m_peopleTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_peopleTable, &QTableView::customContextMenuRequested, this, &PeopleWidget::onPeopleTableContextMenu);
    leftLayout->addWidget(m_peopleTable);

    // Action Toolbar Row 1
    QHBoxLayout* row1 = new QHBoxLayout();
    row1->setSpacing(6);
    m_btnAddPerson = new QPushButton(" Add Person (Ctrl+N)", this);
    m_btnAddPerson->setIcon(IconManager::instance().getPrimaryIcon("user-plus", 14));
    m_btnAddPerson->setObjectName("primaryBtn");
    connect(m_btnAddPerson, &QPushButton::clicked, this, &PeopleWidget::onAddPerson);

    m_btnAddHousehold = new QPushButton(" Add Family", this);
    m_btnAddHousehold->setIcon(IconManager::instance().getIcon("users", ThemeManager::textOn(QColor("#f5c2e7")), 14));
    m_btnAddHousehold->setObjectName("accentBtnPink");
    connect(m_btnAddHousehold, &QPushButton::clicked, this, &PeopleWidget::onAddHousehold);

    m_btnManageHouseholds = new QPushButton(" Families", this);
    m_btnManageHouseholds->setIcon(IconManager::instance().getIcon("folder", QColor(), 14));
    m_btnManageHouseholds->setObjectName("secondaryBtn");
    m_btnManageHouseholds->setToolTip("Manage or purge registered family households");
    connect(m_btnManageHouseholds, &QPushButton::clicked, this, &PeopleWidget::onDeleteHousehold);

    row1->addWidget(m_btnAddPerson);
    row1->addWidget(m_btnAddHousehold);
    row1->addWidget(m_btnManageHouseholds);

    // Action Toolbar Row 2
    QHBoxLayout* row2 = new QHBoxLayout();
    row2->setSpacing(6);

    ThemeTokens curTok = ThemeManager::instance().currentTokens();
    m_btnDeleteSelected = new QPushButton(" Delete Selected", this);
    m_btnDeleteSelected->setIcon(IconManager::instance().getIcon("trash-2", curTok.accentDanger, 14));
    m_btnDeleteSelected->setObjectName("ghostDangerBtn");
    connect(m_btnDeleteSelected, &QPushButton::clicked, this, &PeopleWidget::onDeleteSelectedMembers);

    m_btnBatchStatus = new QPushButton(" Batch Status", this);
    m_btnBatchStatus->setIcon(IconManager::instance().getIcon("tag", QColor(), 14));
    m_btnBatchStatus->setObjectName("secondaryBtn");
    connect(m_btnBatchStatus, &QPushButton::clicked, this, &PeopleWidget::onBatchSetStatus);

    QPushButton* btnCopyRoster = new QPushButton(" Roster SMS", this);
    btnCopyRoster->setIcon(IconManager::instance().getIcon("copy", QColor(), 14));
    btnCopyRoster->setObjectName("secondaryBtn");
    btnCopyRoster->setToolTip("Copy all member phone numbers in current filter to clipboard");
    connect(btnCopyRoster, &QPushButton::clicked, this, [this]() {
        QStringList phoneNumbers;
        for (int r = 0; r < m_proxyModel->rowCount(); ++r) {
            QModelIndex idx = m_proxyModel->index(r, 2); // Phone
            QString phone = idx.data().toString().trimmed();
            if (!phone.isEmpty()) phoneNumbers << phone;
        }
        if (!phoneNumbers.isEmpty()) {
            QGuiApplication::clipboard()->setText(phoneNumbers.join(", "));
            QMessageBox::information(this, "SMS Roster Exported", 
                QString("Copied %1 phone numbers to clipboard, ready to paste in your SMS client.").arg(phoneNumbers.size()));
        } else {
            QMessageBox::warning(this, "Roster Empty", "No valid phone numbers found in current directory view.");
        }
    });

    QPushButton* btnPrintDirectory = new QPushButton(" Print", this);
    btnPrintDirectory->setIcon(IconManager::instance().getIcon("printer", QColor(), 14));
    btnPrintDirectory->setObjectName("secondaryBtn");
    btnPrintDirectory->setToolTip("Print official church directory with church letterhead");
    connect(btnPrintDirectory, &QPushButton::clicked, this, &PeopleWidget::onPrintDirectory);

    m_btnImportCSV = new QPushButton(" Import CSV", this);
    m_btnImportCSV->setIcon(IconManager::instance().getIcon("upload", QColor(), 14));
    m_btnImportCSV->setObjectName("secondaryBtn");
    m_btnImportCSV->setToolTip("Import member roster from CSV file");
    connect(m_btnImportCSV, &QPushButton::clicked, this, &PeopleWidget::onImportCSV);

    row2->addWidget(m_btnDeleteSelected);
    row2->addWidget(m_btnBatchStatus);
    row2->addWidget(btnCopyRoster);
    row2->addWidget(btnPrintDirectory);
    row2->addWidget(m_btnImportCSV);

    leftLayout->addLayout(row1);
    leftLayout->addLayout(row2);

    // ----------------------------------------------------
    // RIGHT PANEL: 360° Member Dossier Detail View
    // ----------------------------------------------------
    QWidget* rightContainer = new QWidget(this);
    QVBoxLayout* rightMainLayout = new QVBoxLayout(rightContainer);
    rightMainLayout->setContentsMargins(0, 0, 0, 0);
    rightMainLayout->setSpacing(0);

    // Empty state container
    m_emptyStateWidget = new QWidget(rightContainer);
    QVBoxLayout* layEmpty = new QVBoxLayout(m_emptyStateWidget);
    layEmpty->setAlignment(Qt::AlignCenter);
    layEmpty->setSpacing(14);
    QLabel* icoEmpty = new QLabel("👤", m_emptyStateWidget);
    icoEmpty->setStyleSheet("font-size: 64px; opacity: 0.35;");
    icoEmpty->setAlignment(Qt::AlignCenter);
    QLabel* lblEmptyTitle = new QLabel("No Member Profile Selected", m_emptyStateWidget);
    lblEmptyTitle->setStyleSheet("font-size: 17px; font-weight: bold; color: palette(text);");
    lblEmptyTitle->setAlignment(Qt::AlignCenter);
    QLabel* lblEmptySub = new QLabel("Select a member from the directory on the left or click 'Add Person' to manage profiles.", m_emptyStateWidget);
    lblEmptySub->setStyleSheet("font-size: 12px; color: palette(placeholderText); max-width: 320px; text-align: center;");
    lblEmptySub->setWordWrap(true);
    lblEmptySub->setAlignment(Qt::AlignCenter);
    layEmpty->addWidget(icoEmpty);
    layEmpty->addWidget(lblEmptyTitle);
    layEmpty->addWidget(lblEmptySub);
    rightMainLayout->addWidget(m_emptyStateWidget);

    // Dossier container (Header + Navigation Rail + Stack + Sticky Footer)
    m_dossierContainer = new QWidget(rightContainer);
    QVBoxLayout* dossierLayout = new QVBoxLayout(m_dossierContainer);
    dossierLayout->setContentsMargins(0, 0, 0, 0);
    dossierLayout->setSpacing(8);

    // 1. PINNED DYNAMIC HEADER
    m_headerWidget = new QFrame(m_dossierContainer);
    m_headerWidget->setObjectName("cardWidget");
    m_headerWidget->setStyleSheet("QFrame#cardWidget { background-color: palette(base); border: 1px solid palette(mid); border-radius: 10px; }");
    QHBoxLayout* headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(14, 10, 14, 10);
    headerLayout->setSpacing(14);

    m_avatarLabel = new ClickableAvatarLabel(m_headerWidget);
    m_avatarLabel->setFixedSize(64, 64);
    m_avatarLabel->setObjectName("avatarLabel");
    m_avatarLabel->setAlignment(Qt::AlignCenter);
    connect(m_avatarLabel, &ClickableAvatarLabel::clicked, this, &PeopleWidget::onChangeAvatar);
    headerLayout->addWidget(m_avatarLabel);

    QVBoxLayout* headerInfo = new QVBoxLayout();
    headerInfo->setSpacing(3);

    m_lblHeaderName = new QLabel("Member Profile", m_headerWidget);
    m_lblHeaderName->setStyleSheet("font-size: 17px; font-weight: bold; color: palette(text);");
    headerInfo->addWidget(m_lblHeaderName);

    m_lblHeaderSub = new QLabel("Select a member", m_headerWidget);
    m_lblHeaderSub->setStyleSheet("font-size: 11px; color: palette(placeholderText);");
    headerInfo->addWidget(m_lblHeaderSub);

    QHBoxLayout* badgesRow = new QHBoxLayout();
    badgesRow->setSpacing(8);

    m_lblHeaderStatusBadge = new QLabel("Member", m_headerWidget);
    m_lblHeaderStatusBadge->setStyleSheet("color: #2ef098; background-color: rgba(46, 240, 152, 0.2); border-radius: 6px; padding: 2px 8px; font-weight: bold; font-size: 10px;");
    badgesRow->addWidget(m_lblHeaderStatusBadge);

    m_lblHeaderCareBadge = new QLabel("Care Alert", m_headerWidget);
    m_lblHeaderCareBadge->setStyleSheet(QString("color: %1; background-color: rgba(%2, %3, %4, 0.2); border-radius: 6px; padding: 2px 8px; font-weight: bold; font-size: 10px;")
        .arg(ThemeManager::instance().currentTokens().accentDanger.name())
        .arg(ThemeManager::instance().currentTokens().accentDanger.red())
        .arg(ThemeManager::instance().currentTokens().accentDanger.green())
        .arg(ThemeManager::instance().currentTokens().accentDanger.blue()));
    m_lblHeaderCareBadge->setVisible(false);
    badgesRow->addWidget(m_lblHeaderCareBadge);

    badgesRow->addStretch();
    headerInfo->addLayout(badgesRow);
    headerLayout->addLayout(headerInfo, 1);
    dossierLayout->addWidget(m_headerWidget);

    // 2. CONTEXTUAL NAVIGATION RAIL
    m_navRail = new QWidget(m_dossierContainer);
    QHBoxLayout* navLayout = new QHBoxLayout(m_navRail);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(4);

    auto makeNavBtn = [this](const QString& text, int pageIdx) {
        QPushButton* btn = new QPushButton(text, m_navRail);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton { background-color: palette(base); border: 1px solid palette(mid); border-radius: 6px; color: palette(text); padding: 5px 10px; font-size: 11px; font-weight: bold; }"
            "QPushButton:hover:!checked { background-color: palette(mid); color: palette(text); border-color: palette(highlight); }"
            "QPushButton:checked { background-color: palette(highlight); color: palette(highlightedText); border: 1px solid palette(highlight); }"
        );
        connect(btn, &QPushButton::clicked, this, [this, pageIdx]() {
            m_detailStack->setCurrentIndex(pageIdx);
            m_btnNavIdentity->setChecked(pageIdx == 0);
            m_btnNavPastoral->setChecked(pageIdx == 1);
            m_btnNavGroups->setChecked(pageIdx == 2);
            m_btnNavGiving->setChecked(pageIdx == 3);
            m_btnNavMinistry->setChecked(pageIdx == 4);
        });
        return btn;
    };

    m_btnNavIdentity = makeNavBtn("👤 Identity", 0);
    m_btnNavPastoral = makeNavBtn("🕊️ Pastoral", 1);
    m_btnNavGroups = makeNavBtn("👥 Groups", 2);
    m_btnNavGiving = makeNavBtn("💰 Giving", 3);
    m_btnNavMinistry = makeNavBtn("✨ Gifts & Tags", 4);
    m_btnNavIdentity->setChecked(true);

    navLayout->addWidget(m_btnNavIdentity);
    navLayout->addWidget(m_btnNavPastoral);
    navLayout->addWidget(m_btnNavGroups);
    navLayout->addWidget(m_btnNavGiving);
    navLayout->addWidget(m_btnNavMinistry);
    navLayout->addStretch();
    dossierLayout->addWidget(m_navRail);

    // 3. STACKED WIDGET (5 Focused Pages)
    m_detailStack = new QStackedWidget(m_dossierContainer);

    // --- TAB 0: IDENTITY & FAMILY ---
    QWidget* pageIdentity = new QWidget();
    QVBoxLayout* layPageIdent = new QVBoxLayout(pageIdentity);
    layPageIdent->setContentsMargins(0, 0, 0, 0);
    layPageIdent->setSpacing(10);

    QFrame* cardIdent = new QFrame(pageIdentity);
    cardIdent->setObjectName("cardWidget");
    QVBoxLayout* layCardIdent = new QVBoxLayout(cardIdent);
    layCardIdent->setContentsMargins(12, 10, 12, 10);
    layCardIdent->setSpacing(8);

    QLabel* lblIdentTitle = new QLabel("👤 PERSONAL & CONTACT DETAILS", cardIdent);
    lblIdentTitle->setStyleSheet("font-size: 11px; font-weight: bold; color: palette(highlight);");
    layCardIdent->addWidget(lblIdentTitle);

    QFormLayout* formIdent = new QFormLayout();
    formIdent->setSpacing(6);

    m_firstNameEdit = new QLineEdit(cardIdent);
    m_lastNameEdit = new QLineEdit(cardIdent);
    m_genderCombo = new QComboBox(cardIdent);
    m_genderCombo->addItems({"Male", "Female", "Unspecified"});
    m_birthDateEdit = new QDateEdit(QDate(1900, 1, 1), cardIdent);
    m_birthDateEdit->setCalendarPopup(true);
    m_birthDateEdit->setDisplayFormat("yyyy-MM-dd");
    m_birthDateEdit->setMinimumDate(QDate(1900, 1, 1));
    m_birthDateEdit->setSpecialValueText("—");
    m_phoneEdit = new QLineEdit(cardIdent);
    m_emailEdit = new QLineEdit(cardIdent);

    // Real-time input validation
    QRegularExpression rxEmail("^[\\w\\.+-]+@[\\w\\.-]+\\.\\w+$");
    m_emailEdit->setValidator(new QRegularExpressionValidator(rxEmail, this));

    connect(m_firstNameEdit, &QLineEdit::textChanged, this, [this]() { setDirty(true); });
    connect(m_lastNameEdit, &QLineEdit::textChanged, this, [this]() { setDirty(true); });
    connect(m_genderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { setDirty(true); });
    connect(m_birthDateEdit, &QDateEdit::dateChanged, this, [this]() { setDirty(true); });
    connect(m_phoneEdit, &QLineEdit::textChanged, this, [this]() { setDirty(true); });
    connect(m_emailEdit, &QLineEdit::textChanged, this, [this]() { setDirty(true); });

    formIdent->addRow("First Name:", m_firstNameEdit);
    formIdent->addRow("Last Name:", m_lastNameEdit);
    formIdent->addRow("Gender:", m_genderCombo);
    formIdent->addRow("Birth Date:", m_birthDateEdit);
    formIdent->addRow("Phone Number:", m_phoneEdit);
    formIdent->addRow("Email Address:", m_emailEdit);
    layCardIdent->addLayout(formIdent);
    layPageIdent->addWidget(cardIdent);

    // Household Card
    QFrame* cardHouse = new QFrame(pageIdentity);
    cardHouse->setObjectName("cardWidget");
    QVBoxLayout* layCardHouse = new QVBoxLayout(cardHouse);
    layCardHouse->setContentsMargins(12, 10, 12, 10);
    layCardHouse->setSpacing(8);

    QLabel* lblHouseTitle = new QLabel("🏠 HOUSEHOLD & FAMILY ASSIGNMENT", cardHouse);
    lblHouseTitle->setStyleSheet("font-size: 11px; font-weight: bold; color: palette(highlight);");
    layCardHouse->addWidget(lblHouseTitle);

    QFormLayout* formHouse = new QFormLayout();
    formHouse->setSpacing(6);

    m_householdCombo = new QComboBox(cardHouse);
    connect(m_householdCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        setDirty(true);
        int hId = m_householdCombo->currentData().toInt();
        refreshHouseholdMembersList(hId, m_currentPersonId);
    });
    formHouse->addRow("Family Assignment:", m_householdCombo);
    layCardHouse->addLayout(formHouse);

    m_lstHouseholdMembers = new QListWidget(cardHouse);
    m_lstHouseholdMembers->setMaximumHeight(85);
    connect(m_lstHouseholdMembers, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        int pId = item->data(Qt::UserRole).toInt();
        if (pId > 0) selectPerson(pId);
    });
    layCardHouse->addWidget(m_lstHouseholdMembers);
    layPageIdent->addWidget(cardHouse);

    // Membership Card
    QFrame* cardMember = new QFrame(pageIdentity);
    cardMember->setObjectName("cardWidget");
    QVBoxLayout* layCardMember = new QVBoxLayout(cardMember);
    layCardMember->setContentsMargins(12, 10, 12, 10);
    layCardMember->setSpacing(8);

    QLabel* lblMemberTitle = new QLabel("🏷️ MEMBERSHIP & MILESTONES", cardMember);
    lblMemberTitle->setStyleSheet("font-size: 11px; font-weight: bold; color: palette(highlight);");
    layCardMember->addWidget(lblMemberTitle);

    QFormLayout* formMember = new QFormLayout();
    formMember->setSpacing(6);

    m_statusCombo = new QComboBox(cardMember);
    m_statusCombo->addItems({"Visitor", "Regular Attender", "Member", "Inactive"});
    connect(m_statusCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { setDirty(true); });

    QDate nullDate(1900, 1, 1);
    m_firstVisitEdit = new QDateEdit(nullDate, cardMember);
    m_firstVisitEdit->setCalendarPopup(true);
    m_firstVisitEdit->setDisplayFormat("yyyy-MM-dd");
    m_firstVisitEdit->setMinimumDate(nullDate);
    m_firstVisitEdit->setSpecialValueText("—");
    connect(m_firstVisitEdit, &QDateEdit::dateChanged, this, [this]() { setDirty(true); });

    m_joinDateEdit = new QDateEdit(nullDate, cardMember);
    m_joinDateEdit->setCalendarPopup(true);
    m_joinDateEdit->setDisplayFormat("yyyy-MM-dd");
    m_joinDateEdit->setMinimumDate(nullDate);
    m_joinDateEdit->setSpecialValueText("—");
    connect(m_joinDateEdit, &QDateEdit::dateChanged, this, [this]() { setDirty(true); });

    formMember->addRow("Membership Status:", m_statusCombo);
    formMember->addRow("First Visit:", m_firstVisitEdit);
    formMember->addRow("Join Date:", m_joinDateEdit);
    layCardMember->addLayout(formMember);
    layPageIdent->addWidget(cardMember);

    // Documents Card: vault documents linked to this person (membership
    // applications and similar records filed from the Document Vault).
    QFrame* cardDocs = new QFrame(pageIdentity);
    cardDocs->setObjectName("cardWidget");
    QVBoxLayout* layCardDocs = new QVBoxLayout(cardDocs);
    layCardDocs->setContentsMargins(12, 10, 12, 10);
    layCardDocs->setSpacing(8);

    QLabel* lblDocsTitle = new QLabel("📄 LINKED DOCUMENTS", cardDocs);
    lblDocsTitle->setStyleSheet("font-size: 11px; font-weight: bold; color: palette(highlight);");
    layCardDocs->addWidget(lblDocsTitle);

    m_lstPersonDocs = new QListWidget(cardDocs);
    m_lstPersonDocs->setMaximumHeight(95);
    connect(m_lstPersonDocs, &QListWidget::itemDoubleClicked, this, &PeopleWidget::onOpenPersonDocument);
    layCardDocs->addWidget(m_lstPersonDocs);

    layPageIdent->addWidget(cardDocs);
    layPageIdent->addStretch();

    QScrollArea* scrollIdent = new QScrollArea();
    scrollIdent->setWidgetResizable(true);
    scrollIdent->setFrameShape(QFrame::NoFrame);
    scrollIdent->setWidget(pageIdentity);
    m_detailStack->addWidget(scrollIdent);

    // --- TAB 1: PASTORAL CARE & HISTORY ---
    QWidget* pagePastoral = new QWidget();
    QVBoxLayout* layPagePastoral = new QVBoxLayout(pagePastoral);
    layPagePastoral->setContentsMargins(0, 0, 0, 0);
    layPagePastoral->setSpacing(10);

    QFrame* cardPastoral = new QFrame(pagePastoral);
    cardPastoral->setObjectName("cardWidget");
    QVBoxLayout* layCardPastoral = new QVBoxLayout(cardPastoral);
    layCardPastoral->setContentsMargins(12, 10, 12, 10);
    layCardPastoral->setSpacing(8);

    QHBoxLayout* pastHeaderRow = new QHBoxLayout();
    QLabel* lblPastTitle = new QLabel("🕊️ PASTORAL CARE & TRIAGE", cardPastoral);
    lblPastTitle->setStyleSheet("font-size: 11px; font-weight: bold; color: palette(highlight);");
    pastHeaderRow->addWidget(lblPastTitle);
    pastHeaderRow->addStretch();

    m_btnLogCare = new QPushButton(" Log Care Entry", cardPastoral);
    m_btnLogCare->setIcon(IconManager::instance().getPrimaryIcon("plus", 14));
    m_btnLogCare->setObjectName("primaryBtn");
    m_btnLogCare->setFixedHeight(26);
    connect(m_btnLogCare, &QPushButton::clicked, this, &PeopleWidget::onLogCareEntry);
    pastHeaderRow->addWidget(m_btnLogCare);
    layCardPastoral->addLayout(pastHeaderRow);

    QFormLayout* formPastoral = new QFormLayout();
    formPastoral->setSpacing(6);

    m_careFlagCombo = new QComboBox(cardPastoral);
    m_careFlagCombo->addItems({"Normal", "Needs Contact", "At-Risk", "Follow-up", "Grief Support", "Leader"});
    connect(m_careFlagCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { setDirty(true); });

    m_adminNotesText = new QTextEdit(cardPastoral);
    m_adminNotesText->setMaximumHeight(80);
    m_adminNotesText->setPlaceholderText("Confidential pastoral notes, prayer needs, or counseling context...");
    connect(m_adminNotesText, &QTextEdit::textChanged, this, [this]() { setDirty(true); });

    formPastoral->addRow("Care Flag:", m_careFlagCombo);
    formPastoral->addRow("Admin Notes:", m_adminNotesText);
    layCardPastoral->addLayout(formPastoral);
    layPagePastoral->addWidget(cardPastoral);

    // History Timeline Card
    QFrame* cardTimeline = new QFrame(pagePastoral);
    cardTimeline->setObjectName("cardWidget");
    QVBoxLayout* layCardTimeline = new QVBoxLayout(cardTimeline);
    layCardTimeline->setContentsMargins(12, 10, 12, 10);
    layCardTimeline->setSpacing(8);

    QLabel* lblTimelineTitle = new QLabel("📅 PASTORAL CARE & ACTIVITY TIMELINE", cardTimeline);
    lblTimelineTitle->setStyleSheet("font-size: 11px; font-weight: bold; color: palette(highlight);");
    layCardTimeline->addWidget(lblTimelineTitle);

    m_historyList = new QListWidget(cardTimeline);
    m_historyList->setObjectName("historyList");
    m_historyList->setMinimumHeight(140);
    m_historyList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_historyList, &QListWidget::customContextMenuRequested, this, &PeopleWidget::onTimelineContextMenu);
    layCardTimeline->addWidget(m_historyList);
    layPagePastoral->addWidget(cardTimeline);
    layPagePastoral->addStretch();

    QScrollArea* scrollPastoral = new QScrollArea();
    scrollPastoral->setWidgetResizable(true);
    scrollPastoral->setFrameShape(QFrame::NoFrame);
    scrollPastoral->setWidget(pagePastoral);
    m_detailStack->addWidget(scrollPastoral);

    // --- TAB 2: GROUPS & CONNECTIONS ---
    QWidget* pageGroups = new QWidget();
    QVBoxLayout* layPageGroups = new QVBoxLayout(pageGroups);
    layPageGroups->setContentsMargins(0, 0, 0, 0);
    layPageGroups->setSpacing(10);

    QFrame* cardGroups = new QFrame(pageGroups);
    cardGroups->setObjectName("cardWidget");
    QVBoxLayout* layCardGroups = new QVBoxLayout(cardGroups);
    layCardGroups->setContentsMargins(12, 10, 12, 10);
    layCardGroups->setSpacing(8);

    QLabel* lblGroupsTitle = new QLabel("👥 ACTIVE MINISTRY GROUPS & SERVING TEAMS", cardGroups);
    lblGroupsTitle->setStyleSheet("font-size: 11px; font-weight: bold; color: palette(highlight);");
    layCardGroups->addWidget(lblGroupsTitle);

    m_connectionsList = new QListWidget(cardGroups);
    m_connectionsList->setObjectName("historyList");
    m_connectionsList->setMinimumHeight(160);
    layCardGroups->addWidget(m_connectionsList);
    layPageGroups->addWidget(cardGroups);
    layPageGroups->addStretch();

    QScrollArea* scrollGroups = new QScrollArea();
    scrollGroups->setWidgetResizable(true);
    scrollGroups->setFrameShape(QFrame::NoFrame);
    scrollGroups->setWidget(pageGroups);
    m_detailStack->addWidget(scrollGroups);

    // --- TAB 3: GIVING & STEWARDSHIP ---
    QWidget* pageGiving = new QWidget();
    QVBoxLayout* layPageGiving = new QVBoxLayout(pageGiving);
    layPageGiving->setContentsMargins(0, 0, 0, 0);
    layPageGiving->setSpacing(10);

    QFrame* cardGiving = new QFrame(pageGiving);
    cardGiving->setObjectName("cardWidget");
    QVBoxLayout* layCardGiving = new QVBoxLayout(cardGiving);
    layCardGiving->setContentsMargins(12, 10, 12, 10);
    layCardGiving->setSpacing(10);

    QLabel* lblGivingTitle = new QLabel("💰 STEWARDSHIP & GIVING OVERVIEW", cardGiving);
    lblGivingTitle->setStyleSheet("font-size: 11px; font-weight: bold; color: palette(highlight);");
    layCardGiving->addWidget(lblGivingTitle);

    QHBoxLayout* givingMetricsRow = new QHBoxLayout();
    givingMetricsRow->setSpacing(10);

    auto makeMetric = [](const QString& title, QLabel** outLabel, const QString& defVal) {
        QFrame* mBox = new QFrame();
        mBox->setStyleSheet("QFrame { background-color: palette(base); border: 1px solid palette(mid); border-radius: 8px; padding: 6px; }");
        QVBoxLayout* v = new QVBoxLayout(mBox);
        v->setContentsMargins(6, 6, 6, 6);
        v->setSpacing(2);
        QLabel* t = new QLabel(title);
        t->setStyleSheet("font-size: 10px; color: palette(placeholderText); font-weight: bold;");
        QLabel* val = new QLabel(defVal);
        val->setStyleSheet("font-size: 15px; font-weight: bold; color: #2ef098;");
        *outLabel = val;
        v->addWidget(t);
        v->addWidget(val);
        return mBox;
    };

    // Note: this SUM is all-time (not year-to-date); the label used to say
    // "YTD" which was misleading. Household combines this person + spouse.
    givingMetricsRow->addWidget(makeMetric("Total Giving", &m_lblGivingYTD, "$0.00"));
    givingMetricsRow->addWidget(makeMetric("Household (with spouse)", &m_lblGivingHousehold, "—"));
    givingMetricsRow->addWidget(makeMetric("Total Gifts", &m_lblGivingCount, "0"));
    givingMetricsRow->addWidget(makeMetric("Last Contribution", &m_lblGivingLastDate, "—"));
    layCardGiving->addLayout(givingMetricsRow);

    QLabel* lblRecentGifts = new QLabel("Recent Contribution Records:", cardGiving);
    lblRecentGifts->setStyleSheet("font-size: 11px; font-weight: bold; color: palette(text); margin-top: 4px;");
    layCardGiving->addWidget(lblRecentGifts);

    m_lstRecentGiving = new QListWidget(cardGiving);
    m_lstRecentGiving->setObjectName("historyList");
    m_lstRecentGiving->setMinimumHeight(120);
    layCardGiving->addWidget(m_lstRecentGiving);

    m_btnGenerateStatement = new QPushButton(" Generate End-of-Year Giving Tax Statement (PDF)", cardGiving);
    m_btnGenerateStatement->setIcon(IconManager::instance().getIcon("file-text", QColor(), 14));
    m_btnGenerateStatement->setObjectName("secondaryBtn");
    connect(m_btnGenerateStatement, &QPushButton::clicked, this, [this]() {
        if (m_currentPersonId <= 0) return;
        QString fileName = QFileDialog::getSaveFileName(this, "Save Giving Tax Statement", 
            QString("Giving_Statement_%1_%2.pdf").arg(m_firstNameEdit->text(), m_lastNameEdit->text()), "PDF Files (*.pdf)");
        if (!fileName.isEmpty()) {
            QPdfWriter writer(fileName);
            writer.setPageSize(QPageSize(QPageSize::Letter));
            QPainter painter(&writer);
            painter.setPen(Qt::black);

            // Official church letterhead (logo, name, address, Tax ID).
            ChurchProfile::renderPainterHeader(painter, writer.width(), "Official Giving Tax Statement", /*includeTaxId=*/true);

            QFont fHeader("Helvetica", 16, QFont::Bold);
            painter.setFont(fHeader);
            painter.drawText(500, 500, "Official Giving Tax Statement");
            QFont fSub("Helvetica", 10);
            painter.setFont(fSub);
            painter.drawText(500, 750, QString("Donor: %1 %2").arg(m_firstNameEdit->text(), m_lastNameEdit->text()));
            painter.drawText(500, 950, QString("Total Giving Recorded: %1").arg(m_lblGivingYTD->text()));
            painter.drawText(500, 1150, "OpenEkklesia Church Management System — 501(c)(3) Compliant");
            painter.end();
            QMessageBox::information(this, "Statement Generated", QString("Giving tax statement exported successfully to:\n%1").arg(fileName));
        }
    });
    layCardGiving->addWidget(m_btnGenerateStatement);
    layPageGiving->addWidget(cardGiving);
    layPageGiving->addStretch();

    QScrollArea* scrollGiving = new QScrollArea();
    scrollGiving->setWidgetResizable(true);
    scrollGiving->setFrameShape(QFrame::NoFrame);
    scrollGiving->setWidget(pageGiving);
    m_detailStack->addWidget(scrollGiving);

    // --- TAB 4: MINISTRY FIT (ARCHETYPES, GIFTS & TAGS) ---
    QWidget* pageMinistry = new QWidget();
    QVBoxLayout* layPageMinistry = new QVBoxLayout(pageMinistry);
    layPageMinistry->setContentsMargins(0, 0, 0, 0);
    layPageMinistry->setSpacing(10);

    QFrame* cardMin = new QFrame(pageMinistry);
    cardMin->setObjectName("cardWidget");
    QVBoxLayout* layCardMin = new QVBoxLayout(cardMin);
    layCardMin->setContentsMargins(12, 10, 12, 10);
    layCardMin->setSpacing(10);

    // 1. Primary & Secondary Archetypes
    QLabel* lblArchetypeTitle = new QLabel("🔥 MINISTRY ARCHETYPE & BEHAVIORAL DNA", cardMin);
    lblArchetypeTitle->setStyleSheet("font-size: 11px; font-weight: bold; color: palette(highlight);");
    layCardMin->addWidget(lblArchetypeTitle);

    m_primaryArchetypeEdit = new QLineEdit(cardMin);
    m_primaryArchetypeEdit->setVisible(false);

    QLabel* lblPrimaryNote = new QLabel("Primary Ministry Calling (Single-Select):", cardMin);
    lblPrimaryNote->setStyleSheet("font-size: 10px; font-weight: bold; color: palette(text);");
    layCardMin->addWidget(lblPrimaryNote);

    QHBoxLayout* archetypeRow = new QHBoxLayout();
    archetypeRow->setSpacing(6);

    const struct { ChurchArchetype arch; const char* name; const char* icon; const char* color; } kArchetypes[] = {
        {ChurchArchetype::Catalyst, "Catalyst", "🔥", "#fab387"},
        {ChurchArchetype::Shepherd, "Shepherd", "🌿", "#a6e3a1"},
        {ChurchArchetype::Architect, "Architect", "📐", "#89b4fa"},
        {ChurchArchetype::Spark, "Spark", "⚡", "#f9e2af"},
        {ChurchArchetype::Anchor, "Anchor", "⚓", "#cba6f7"}
    };

    for (const auto& a : kArchetypes) {
        QPushButton* chip = new QPushButton(QString("%1 %2").arg(a.icon, a.name), cardMin);
        chip->setCheckable(true);
        chip->setProperty("rawName", a.name);
        const QString onColor = ThemeManager::textOn(QColor(a.color)).name();
        chip->setStyleSheet(QString(
            "QPushButton { background-color: palette(base); border: 1px solid palette(mid); border-radius: 12px; color: palette(text); padding: 4px 10px; font-size: 11px; font-weight: bold; }"
            "QPushButton:hover:!checked { border: 1px solid %1; color: %1; background-color: palette(mid); }"
            "QPushButton:checked { background-color: %1; border: 2px solid %1; color: %2; }"
        ).arg(a.color, onColor));

        connect(chip, &QPushButton::clicked, this, [this, chip]() {
            for (QPushButton* c : m_archetypeChips) {
                if (c != chip) {
                    c->blockSignals(true);
                    c->setChecked(false);
                    c->blockSignals(false);
                }
            }
            chip->setChecked(true);
            updateArchetypeFromChips();
            setDirty(true);
        });

        m_archetypeChips.append(chip);
        archetypeRow->addWidget(chip);
    }
    archetypeRow->addStretch();
    layCardMin->addLayout(archetypeRow);

    // Secondary Archetype Dropdown
    QFormLayout* formSec = new QFormLayout();
    formSec->setSpacing(6);
    m_cmbSecondaryArchetype = new QComboBox(cardMin);
    m_cmbSecondaryArchetype->addItem("— None —", "None");
    for (const auto& a : kArchetypes) {
        m_cmbSecondaryArchetype->addItem(QString("%1 %2").arg(a.icon, a.name), a.name);
    }
    connect(m_cmbSecondaryArchetype, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        setDirty(true);
    });
    formSec->addRow("Secondary Archetype:", m_cmbSecondaryArchetype);
    layCardMin->addLayout(formSec);

    // 2. Spiritual Gifts (Max 4)
    QLabel* lblMinTitle = new QLabel("✨ SPIRITUAL GIFTS (Max 4 Primary):", cardMin);
    lblMinTitle->setStyleSheet("font-size: 11px; font-weight: bold; color: palette(text); margin-top: 4px;");
    layCardMin->addWidget(lblMinTitle);

    m_giftsEdit = new QLineEdit(cardMin);
    m_giftsEdit->setVisible(false);

    static const QStringList kSpiritualGifts = {
        "Administration", "Evangelism", "Helps", "Hospitality", "Leadership",
        "Mercy", "Pastoral Care", "Prophecy", "Teaching", "Worship / Music"
    };

    QVBoxLayout* giftsChipBox = new QVBoxLayout();
    giftsChipBox->setSpacing(4);
    QHBoxLayout* giftsRow1 = new QHBoxLayout(); giftsRow1->setSpacing(4);
    QHBoxLayout* giftsRow2 = new QHBoxLayout(); giftsRow2->setSpacing(4);

    for (int i = 0; i < kSpiritualGifts.size(); ++i) {
        QString g = kSpiritualGifts.at(i);
        QPushButton* chip = new QPushButton(g, cardMin);
        chip->setCheckable(true);
        chip->setProperty("rawName", g);
        chip->setStyleSheet(
            "QPushButton { background-color: palette(base); border: 1px solid palette(mid); border-radius: 10px; color: palette(text); padding: 3px 8px; font-size: 10px; font-weight: bold; }"
            "QPushButton:hover:!checked { background-color: palette(mid); border: 1px solid palette(highlight); color: palette(highlight); }"
            "QPushButton:checked { background-color: palette(highlight); border: 1px solid palette(highlight); color: palette(highlightedText); }"
        );
        connect(chip, &QPushButton::toggled, this, [this, chip](bool checked) {
            // Constrain to max 4 selected gifts
            if (checked) {
                int count = 0;
                for (QPushButton* c : m_giftChips) {
                    if (c->isChecked()) count++;
                }
                if (count > 4) {
                    chip->blockSignals(true);
                    chip->setChecked(false);
                    chip->blockSignals(false);
                    QMessageBox::information(this, "Gifts Limit", "To ensure high data quality, please select up to 4 primary spiritual gifts.");
                    return;
                }
            }
            QString raw = chip->property("rawName").toString();
            chip->setText(checked ? QString("✓ %1").arg(raw) : raw);
            updateGiftsFromChips();
            setDirty(true);
        });
        m_giftChips.append(chip);

        if (i < 5) giftsRow1->addWidget(chip);
        else giftsRow2->addWidget(chip);
    }
    giftsRow1->addStretch();
    giftsRow2->addStretch();
    giftsChipBox->addLayout(giftsRow1);
    giftsChipBox->addLayout(giftsRow2);
    layCardMin->addLayout(giftsChipBox);

    // 3. Relational Styles (Single-Select)
    QLabel* lblStyleHeader = new QLabel("🧠 RELATIONAL STYLE (Single-Select):", cardMin);
    lblStyleHeader->setStyleSheet("font-size: 11px; font-weight: bold; color: palette(text); margin-top: 4px;");
    layCardMin->addWidget(lblStyleHeader);

    m_styleEdit = new QLineEdit(cardMin);
    m_styleEdit->setVisible(false);

    static const QStringList kRelationalStyles = {
        "Organizer", "Listener", "Doer", "Energizer", "Strategist", "Connector"
    };

    QHBoxLayout* styleRow = new QHBoxLayout();
    styleRow->setSpacing(4);
    for (const QString& s : kRelationalStyles) {
        QPushButton* chip = new QPushButton(s, cardMin);
        chip->setCheckable(true);
        chip->setProperty("rawName", s);
        chip->setStyleSheet(
            "QPushButton { background-color: palette(base); border: 1px solid palette(mid); border-radius: 10px; color: palette(text); padding: 3px 8px; font-size: 10px; font-weight: bold; }"
            "QPushButton:hover:!checked { background-color: palette(mid); border: 1px solid palette(highlight); color: palette(highlight); }"
            "QPushButton:checked { background-color: palette(highlight); border: 1px solid palette(highlight); color: palette(highlightedText); }"
        );
        connect(chip, &QPushButton::clicked, this, [this, chip]() {
            for (QPushButton* c : m_styleChips) {
                if (c != chip) {
                    c->blockSignals(true);
                    c->setChecked(false);
                    c->setText(c->property("rawName").toString());
                    c->blockSignals(false);
                }
            }
            chip->setChecked(true);
            chip->setText(QString("✓ %1").arg(chip->property("rawName").toString()));
            updateStylesFromChips();
            setDirty(true);
        });
        m_styleChips.append(chip);
        styleRow->addWidget(chip);
    }
    styleRow->addStretch();
    layCardMin->addLayout(styleRow);

    // 4. Interest Tags
    QLabel* lblTagsHeader = new QLabel("🏷️ INTEREST TAGS:", cardMin);
    lblTagsHeader->setStyleSheet("font-size: 11px; font-weight: bold; color: palette(text); margin-top: 4px;");
    layCardMin->addWidget(lblTagsHeader);

    m_tagsEdit = new QLineEdit(cardMin);
    m_tagsEdit->setVisible(false);

    m_tagsChipsContainer = new QWidget(cardMin);
    layCardMin->addWidget(m_tagsChipsContainer);

    QHBoxLayout* tagInputRow = new QHBoxLayout();
    tagInputRow->setSpacing(6);
    m_tagInputField = new QLineEdit(cardMin);
    m_tagInputField->setPlaceholderText("Type a tag (e.g. Young Families, AV/Tech) & press Enter...");
    QPushButton* btnAddTag = new QPushButton("+ Add Tag", cardMin);
    btnAddTag->setObjectName("secondaryBtn");

    auto triggerAddTag = [this]() {
        addInterestTag(m_tagInputField->text());
        setDirty(true);
    };
    connect(m_tagInputField, &QLineEdit::returnPressed, this, triggerAddTag);
    connect(btnAddTag, &QPushButton::clicked, this, triggerAddTag);

    tagInputRow->addWidget(m_tagInputField, 1);
    tagInputRow->addWidget(btnAddTag);
    layCardMin->addLayout(tagInputRow);
    layPageMinistry->addWidget(cardMin);
    layPageMinistry->addStretch();

    QScrollArea* scrollMinistry = new QScrollArea();
    scrollMinistry->setWidgetResizable(true);
    scrollMinistry->setFrameShape(QFrame::NoFrame);
    scrollMinistry->setWidget(pageMinistry);
    m_detailStack->addWidget(scrollMinistry);

    dossierLayout->addWidget(m_detailStack, 1);

    // 4. STICKY ACTION FOOTER BAR
    m_footerFrame = new QFrame(m_dossierContainer);
    m_footerFrame->setObjectName("footerFrame");
    m_footerFrame->setStyleSheet("QFrame#footerFrame { background-color: palette(base); border-top: 1px solid palette(mid); border-radius: 8px; }");
    QHBoxLayout* footerLayout = new QHBoxLayout(m_footerFrame);
    footerLayout->setContentsMargins(12, 8, 12, 8);
    footerLayout->setSpacing(10);

    m_lblDirtyIndicator = new QLabel("● Unsaved Changes", m_footerFrame);
    m_lblDirtyIndicator->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 11px;").arg(curTok.accentWarning.name()));
    m_lblDirtyIndicator->setVisible(false);
    footerLayout->addWidget(m_lblDirtyIndicator);
    footerLayout->addStretch();

    m_btnSave = new QPushButton(" Save Profile (Ctrl+S)", m_footerFrame);
    m_btnSave->setIcon(IconManager::instance().getPrimaryIcon("check", 14));
    m_btnSave->setObjectName("primaryBtn");
    m_btnSave->setFixedHeight(34);
    connect(m_btnSave, &QPushButton::clicked, this, &PeopleWidget::onSaveProfile);
    footerLayout->addWidget(m_btnSave);

    QPushButton* btnExportVCard = new QPushButton(" Export vCard", m_footerFrame);
    btnExportVCard->setIcon(IconManager::instance().getIcon("download", QColor(), 14));
    btnExportVCard->setObjectName("secondaryBtn");
    btnExportVCard->setFixedHeight(34);
    btnExportVCard->setToolTip("Export contact as standard .vcf format for smartphones, Apple Contacts, or Outlook.");
    connect(btnExportVCard, &QPushButton::clicked, this, &PeopleWidget::onExportVCard);
    footerLayout->addWidget(btnExportVCard);

    m_btnDeletePerson = new QPushButton(" Delete Profile", m_footerFrame);
    m_btnDeletePerson->setIcon(IconManager::instance().getIcon("trash-2", curTok.accentDanger, 14));
    m_btnDeletePerson->setObjectName("ghostDangerBtn");
    connect(m_btnDeletePerson, &QPushButton::clicked, this, &PeopleWidget::onDeletePerson);
    footerLayout->addWidget(m_btnDeletePerson);

    dossierLayout->addWidget(m_footerFrame);

    m_avatarPathEdit = new QLineEdit(this);
    m_avatarPathEdit->setVisible(false);

    rightMainLayout->addWidget(m_dossierContainer);

    // Initial Splitter Setup
    QSplitter* mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->addWidget(leftPanel);
    mainSplitter->addWidget(rightContainer);
    mainSplitter->setStretchFactor(0, 3);
    mainSplitter->setStretchFactor(1, 2);
    mainLayout->addWidget(mainSplitter);
}


void PeopleWidget::setupModels() {
    QSqlDatabase db = QSqlDatabase::database();

    // Household list model
    m_householdModel = new QSqlTableModel(this, db);
    m_householdModel->setTable("households");
    m_householdModel->setSort(1, Qt::AscendingOrder);
    m_householdModel->select();

    // People model with relational lookup for household_id -> family_name
    m_peopleModel = new QSqlRelationalTableModel(this, db);
    m_peopleModel->setTable("people");
    m_peopleModel->setRelation(1, QSqlRelation("households", "id", "family_name"));
    m_peopleModel->setEditStrategy(QSqlTableModel::OnManualSubmit);
    m_peopleModel->setSort(3, Qt::AscendingOrder);
    m_peopleModel->select();
    while (m_peopleModel->canFetchMore()) {
        m_peopleModel->fetchMore();
    }

    // Setup Custom Proxy Model for merged Name column & searching
    m_proxyModel = new PeopleProxyModel(this);
    m_proxyModel->setSourceModel(m_peopleModel);
    m_proxyModel->setDynamicSortFilter(true);
    m_proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setFilterKeyColumn(-1);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

    m_peopleTable->setModel(m_proxyModel);
    m_peopleTable->setItemDelegateForColumn(4, new StatusItemDelegate(this));

    m_peopleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_peopleTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch); // Name
    m_peopleTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive); // Household
    m_peopleTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive); // Phone
    m_peopleTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive); // Email
    m_peopleTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents); // Status

    // Initialize sort by Name (Last Name A-Z)
    m_proxyModel->setSortMode(PeopleProxyModel::NormalColumnSort);
    m_proxyModel->sort(0, Qt::AscendingOrder);
    m_peopleTable->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);

    connect(m_peopleTable->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int logicalIndex) {
        m_proxyModel->setSortMode(PeopleProxyModel::NormalColumnSort);
        if (logicalIndex == 0) {
            m_cmbSortBy->blockSignals(true);
            m_cmbSortBy->setCurrentIndex(m_peopleTable->horizontalHeader()->sortIndicatorOrder() == Qt::AscendingOrder ? 0 : 1);
            m_cmbSortBy->blockSignals(false);
        } else if (logicalIndex == 1) {
            m_cmbSortBy->blockSignals(true);
            m_cmbSortBy->setCurrentIndex(4); // Family Name
            m_cmbSortBy->blockSignals(false);
        } else if (logicalIndex == 4) {
            m_cmbSortBy->blockSignals(true);
            m_cmbSortBy->setCurrentIndex(5); // Status
            m_cmbSortBy->blockSignals(false);
        }
    });

    // Populate household dropdown
    populateHouseholdCombo();

    // Widget Data Mapper configuration
    m_mapper = new QDataWidgetMapper(this);
    m_mapper->setModel(m_peopleModel);
    m_mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);
    m_mapper->setItemDelegate(new QItemDelegate(this));
    m_mapper->addMapping(m_firstNameEdit, 2);
    m_mapper->addMapping(m_lastNameEdit, 3);
    m_mapper->addMapping(m_phoneEdit, 4);
    m_mapper->addMapping(m_emailEdit, 5);
    m_mapper->addMapping(m_avatarPathEdit, 7);
    m_mapper->addMapping(m_giftsEdit, 15); // spiritual_gifts
    m_mapper->addMapping(m_styleEdit, 16); // relational_style
    m_mapper->addMapping(m_tagsEdit, 17);  // interest_tags

    connect(m_peopleTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &PeopleWidget::onSelectionChanged);

    refreshTagCompleter();

    // Initial Selection
    if (m_proxyModel->rowCount() > 0) {
        m_peopleTable->selectRow(0);
    } else {
        loadAvatar("");
    }
}

void PeopleWidget::populateHouseholdCombo() {
    m_householdCombo->blockSignals(true);
    m_householdCombo->clear();
    m_householdCombo->addItem("— None (No Household) —", 0);

    QSqlQuery q("SELECT id, family_name FROM households ORDER BY family_name ASC");
    while (q.next()) {
        int id = q.value(0).toInt();
        QString name = q.value(1).toString();
        m_householdCombo->addItem(name, id);
    }
    m_householdCombo->blockSignals(false);
}

void PeopleWidget::updateArchetypesFromText(const QString& primary, const QString& secondary) {
    QString p = primary.trimmed();
    if (p.isEmpty()) p = "Anchor";
    m_primaryArchetypeEdit->setText(p);

    for (QPushButton* btn : m_archetypeChips) {
        QString raw = btn->property("rawName").toString();
        bool isSel = (p.compare(raw, Qt::CaseInsensitive) == 0);
        btn->blockSignals(true);
        btn->setChecked(isSel);
        btn->blockSignals(false);
    }

    m_cmbSecondaryArchetype->blockSignals(true);
    int secIdx = m_cmbSecondaryArchetype->findData(secondary.trimmed());
    if (secIdx < 0) secIdx = m_cmbSecondaryArchetype->findText(secondary.trimmed(), Qt::MatchContains);
    m_cmbSecondaryArchetype->setCurrentIndex(secIdx >= 0 ? secIdx : 0);
    m_cmbSecondaryArchetype->blockSignals(false);
}

void PeopleWidget::updateArchetypeFromChips() {
    for (QPushButton* btn : m_archetypeChips) {
        if (btn->isChecked()) {
            m_primaryArchetypeEdit->setText(btn->property("rawName").toString());
            break;
        }
    }
}

void PeopleWidget::updateGiftsFromChips() {
    QStringList selected;
    for (QPushButton* btn : m_giftChips) {
        if (btn->isChecked()) {
            selected << btn->property("rawName").toString();
        }
    }
    m_giftsEdit->setText(selected.join(", "));
}

void PeopleWidget::updateChipsFromGiftsText(const QString& giftsText) {
    QStringList gifts = giftsText.split(",", Qt::SkipEmptyParts);
    for (int i = 0; i < gifts.size(); ++i) gifts[i] = gifts[i].trimmed();

    for (QPushButton* btn : m_giftChips) {
        QString rawText = btn->property("rawName").toString();
        bool isSel = gifts.contains(rawText, Qt::CaseInsensitive);
        btn->blockSignals(true);
        btn->setChecked(isSel);
        btn->setText(isSel ? QString("✓ %1").arg(rawText) : rawText);
        btn->blockSignals(false);
    }
}

void PeopleWidget::updateStylesFromChips() {
    QStringList selected;
    for (QPushButton* btn : m_styleChips) {
        if (btn->isChecked()) {
            selected << btn->property("rawName").toString();
        }
    }
    m_styleEdit->setText(selected.join(", "));
}

void PeopleWidget::updateChipsFromStyleText(const QString& styleText) {
    QStringList styles = styleText.split(",", Qt::SkipEmptyParts);
    for (int i = 0; i < styles.size(); ++i) styles[i] = styles[i].trimmed();

    for (QPushButton* btn : m_styleChips) {
        QString rawText = btn->property("rawName").toString();
        bool isSel = styles.contains(rawText, Qt::CaseInsensitive);
        btn->blockSignals(true);
        btn->setChecked(isSel);
        btn->setText(isSel ? QString("✓ %1").arg(rawText) : rawText);
        btn->blockSignals(false);
    }
}

void PeopleWidget::renderTagChips() {
    QLayout* lay = m_tagsChipsContainer->layout();
    if (lay) {
        QLayoutItem* item;
        while ((item = lay->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
    } else {
        lay = new QHBoxLayout(m_tagsChipsContainer);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(6);
    }

    QStringList tags = m_tagsEdit->text().split(",", Qt::SkipEmptyParts);
    for (const QString& tagRaw : tags) {
        QString tag = tagRaw.trimmed();
        if (tag.isEmpty()) continue;

        QPushButton* chip = new QPushButton(QString("%1  ✕").arg(tag), m_tagsChipsContainer);
        chip->setStyleSheet(
            "QPushButton { background-color: palette(base); color: palette(highlight); border: 1px solid palette(mid); border-radius: 12px; padding: 3px 8px; font-size: 11px; font-weight: bold; }"
            "QPushButton:hover { background-color: palette(highlight); color: palette(highlightedText); }"
        );
        connect(chip, &QPushButton::clicked, this, [this, tag]() {
            removeInterestTag(tag);
        });
        lay->addWidget(chip);
    }
    lay->addItem(new QSpacerItem(1, 1, QSizePolicy::Expanding, QSizePolicy::Minimum));
}

void PeopleWidget::addInterestTag(const QString& tag) {
    QString clean = tag.trimmed();
    if (clean.isEmpty()) return;

    QStringList current = m_tagsEdit->text().split(",", Qt::SkipEmptyParts);
    for (int i = 0; i < current.size(); ++i) current[i] = current[i].trimmed();

    if (!current.contains(clean, Qt::CaseInsensitive)) {
        current << clean;
        m_tagsEdit->setText(current.join(", "));
        renderTagChips();
    }
    m_tagInputField->clear();
}

void PeopleWidget::removeInterestTag(const QString& tag) {
    QStringList current = m_tagsEdit->text().split(",", Qt::SkipEmptyParts);
    QStringList newList;
    for (const QString& t : current) {
        if (t.trimmed().compare(tag.trimmed(), Qt::CaseInsensitive) != 0) {
            newList << t.trimmed();
        }
    }
    m_tagsEdit->setText(newList.join(", "));
    renderTagChips();
}

void PeopleWidget::refreshHouseholdMembersList(int householdId, int currentPersonId) {
    m_lstHouseholdMembers->clear();

    if (householdId <= 1) {
        m_lstHouseholdMembers->addItem("No other family members assigned.");
        return;
    }

    QSqlQuery q;
    q.prepare("SELECT id, first_name || ' ' || last_name, membership_status FROM people WHERE household_id = ? AND id != ? ORDER BY first_name");
    q.addBindValue(householdId);
    q.addBindValue(currentPersonId);
    if (q.exec()) {
        while (q.next()) {
            int pId = q.value(0).toInt();
            QString pName = q.value(1).toString();
            QString pStatus = q.value(2).toString();

            QListWidgetItem* item = new QListWidgetItem(QString("👤 %1 — %2").arg(pName, pStatus), m_lstHouseholdMembers);
            item->setData(Qt::UserRole, pId);
        }
    }

    if (m_lstHouseholdMembers->count() == 0) {
        m_lstHouseholdMembers->addItem("No other family members in this household.");
    }
}

void PeopleWidget::refreshTagCompleter() {
    QStringList existingTags;
    QSqlQuery q("SELECT DISTINCT interest_tags FROM people");
    while (q.next()) {
        QString str = q.value(0).toString();
        for (const QString& tag : str.split(",", Qt::SkipEmptyParts)) {
            QString clean = tag.trimmed();
            if (!clean.isEmpty() && !existingTags.contains(clean, Qt::CaseInsensitive)) {
                existingTags << clean;
            }
        }
    }
    existingTags.sort(Qt::CaseInsensitive);

    m_tagCompleter = new QCompleter(existingTags, this);
    m_tagCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_tagInputField->setCompleter(m_tagCompleter);
}

void PeopleWidget::setDirty(bool dirty) {
    m_isDirty = dirty;
    if (m_lblDirtyIndicator) {
        m_lblDirtyIndicator->setVisible(dirty);
    }
    if (m_btnSave) {
        if (dirty) {
            const ThemeTokens tok = ThemeManager::instance().currentTokens();
            m_btnSave->setStyleSheet(QString("QPushButton#primaryBtn { background-color: %1; color: %2; font-weight: bold; border: 2px solid %3; }")
                .arg(tok.accentSuccess.name(), ThemeManager::textOn(tok.accentSuccess).name(), tok.accentSuccess.lighter(115).name()));
        } else {
            m_btnSave->setStyleSheet("");
        }
    }
}

void PeopleWidget::updateHeader(const QString& name, const QString& status, const QString& careFlag, const QString& household, const QString& joinDate) {
    if (m_lblHeaderName) m_lblHeaderName->setText(name.isEmpty() ? "—" : name);

    QString sub;
    if (!household.isEmpty() && household != "— None (No Household) —") {
        sub += household;
    }
    if (!joinDate.isEmpty() && joinDate != "1900-01-01") {
        if (!sub.isEmpty()) sub += " • ";
        sub += QString("Joined %1").arg(joinDate);
    }
    if (sub.isEmpty()) sub = "No household assigned";
    if (m_lblHeaderSub) m_lblHeaderSub->setText(sub);

    if (m_lblHeaderStatusBadge) {
        m_lblHeaderStatusBadge->setText(status.isEmpty() ? "Member" : status);
        QString statusColor = "#a6adc8";
        QString statusBg = "rgba(166, 173, 200, 0.2)";
        if (status == "Member") {
            statusColor = "#2ef098";
            statusBg = "rgba(46, 240, 152, 0.2)";
        } else if (status == "Regular Attender") {
            statusColor = "#b4befe";
            statusBg = "rgba(180, 190, 254, 0.2)";
        } else if (status == "Visitor") {
            statusColor = "#f9e2af";
            statusBg = "rgba(249, 226, 175, 0.2)";
        }
        m_lblHeaderStatusBadge->setStyleSheet(QString("color: %1; background-color: %2; border-radius: 6px; padding: 2px 8px; font-weight: bold; font-size: 10px;").arg(statusColor, statusBg));
    }

    if (m_lblHeaderCareBadge) {
        const ThemeTokens tokBadge = ThemeManager::instance().currentTokens();
        if (careFlag == "Needs Contact" || careFlag == "At-Risk" || careFlag == "At Risk") {
            m_lblHeaderCareBadge->setText(careFlag);
            m_lblHeaderCareBadge->setStyleSheet(QString("color: %1; background-color: rgba(%2, %3, %4, 0.2); border-radius: 6px; padding: 2px 8px; font-weight: bold; font-size: 10px;")
                .arg(tokBadge.accentDanger.name()).arg(tokBadge.accentDanger.red()).arg(tokBadge.accentDanger.green()).arg(tokBadge.accentDanger.blue()));
            m_lblHeaderCareBadge->setVisible(true);
        } else if (careFlag == "Follow-up" || careFlag == "Grief Support") {
            m_lblHeaderCareBadge->setText(careFlag);
            m_lblHeaderCareBadge->setStyleSheet(QString("color: %1; background-color: rgba(%2, %3, %4, 0.2); border-radius: 6px; padding: 2px 8px; font-weight: bold; font-size: 10px;")
                .arg(tokBadge.accentWarning.name()).arg(tokBadge.accentWarning.red()).arg(tokBadge.accentWarning.green()).arg(tokBadge.accentWarning.blue()));
            m_lblHeaderCareBadge->setVisible(true);
        } else if (careFlag == "Leader") {
            m_lblHeaderCareBadge->setText("Leader");
            m_lblHeaderCareBadge->setStyleSheet(QString("color: %1; background-color: rgba(%2, %3, %4, 0.2); border-radius: 6px; padding: 2px 8px; font-weight: bold; font-size: 10px;")
                .arg(tokBadge.accentPrimary.name()).arg(tokBadge.accentPrimary.red()).arg(tokBadge.accentPrimary.green()).arg(tokBadge.accentPrimary.blue()));
            m_lblHeaderCareBadge->setVisible(true);
        } else {
            m_lblHeaderCareBadge->setVisible(false);
        }
    }
}

void PeopleWidget::updateGivingSummary(int personId) {
    if (!m_lblGivingYTD) return;

    double totalGiving = 0.0;
    int count = 0;
    QString lastDate = "—";

    QSqlQuery q;
    q.prepare("SELECT COALESCE(SUM(amount), 0), COUNT(id), MAX(contribution_date) FROM contributions WHERE person_id = ?");
    q.addBindValue(personId);
    if (q.exec() && q.next()) {
        totalGiving = q.value(0).toDouble() / 100.0;
        count = q.value(1).toInt();
        lastDate = q.value(2).toString();
        if (lastDate.isEmpty()) lastDate = "None recorded";
    }

    m_lblGivingYTD->setText(QString("%1 %2").arg(appCurrencySymbol(), QString::number(totalGiving, 'f', 2)));
    m_lblGivingCount->setText(QString::number(count));
    m_lblGivingLastDate->setText(lastDate);

    // Household view: couples usually give jointly under one spouse's record.
    // The spouse link lives in entity_relationships (stored in both
    // directions), so one lookup suffices. Anonymous gifts never land here.
    if (m_lblGivingHousehold) {
        QString householdText = "—";
        int spouseId = 0;
        QSqlQuery spouse;
        spouse.prepare("SELECT CASE WHEN source_type = 'Person' AND source_id = :me THEN target_id "
                       "        ELSE source_id END AS other "
                       "FROM entity_relationships "
                       "WHERE relationship_type = 'Spouse' "
                       "  AND ((source_type = 'Person' AND source_id = :me) OR (target_type = 'Person' AND target_id = :me)) "
                       "LIMIT 1");
        spouse.bindValue(":me", personId);
        if (spouse.exec() && spouse.next()) {
            spouseId = spouse.value(0).toInt();
        }
        if (spouseId > 0) {
            QSqlQuery hq;
            hq.prepare("SELECT COALESCE(SUM(amount), 0) FROM contributions WHERE person_id IN (?, ?)");
            hq.addBindValue(personId);
            hq.addBindValue(spouseId);
            if (hq.exec() && hq.next()) {
                double household = hq.value(0).toDouble() / 100.0;
                householdText = QString("%1 %2").arg(appCurrencySymbol(), QString::number(household, 'f', 2));
            }
        }
        m_lblGivingHousehold->setText(householdText);
    }

    if (m_lstRecentGiving) {
        m_lstRecentGiving->clear();
        QSqlQuery qGifts;
        qGifts.prepare("SELECT c.contribution_date, f.fund_name, c.amount, COALESCE(NULLIF(c.payment_method, ''), '—'), COALESCE(c.note, '') "
                       "FROM contributions c JOIN funds f ON c.fund_id = f.id "
                       "WHERE c.person_id = ? ORDER BY c.contribution_date DESC LIMIT 10");
        qGifts.addBindValue(personId);
        if (qGifts.exec()) {
            while (qGifts.next()) {
                QString dt = qGifts.value(0).toString();
                QString fund = qGifts.value(1).toString();
                double amt = qGifts.value(2).toDouble() / 100.0;
                QString pay = qGifts.value(3).toString();
                QString note = qGifts.value(4).toString();

                QString line = QString("💰 %1 — %2 %3 (%4, %5)")
                    .arg(dt, appCurrencySymbol(), QString::number(amt, 'f', 2), fund, pay);
                if (!note.isEmpty()) line += QString(" · %1").arg(note);
                m_lstRecentGiving->addItem(line);
            }
        }
        if (m_lstRecentGiving->count() == 0) {
            m_lstRecentGiving->addItem("No contribution records found for this member.");
        }
    }
}

void PeopleWidget::updateDocumentsSection(int personId) {
    if (!m_lstPersonDocs) return;
    m_lstPersonDocs->clear();

    QSqlQuery q;
    q.prepare("SELECT id, category, title, meeting_date, file_path FROM vault_documents "
              "WHERE person_id = ? ORDER BY meeting_date DESC");
    q.addBindValue(personId);
    if (q.exec()) {
        while (q.next()) {
            QListWidgetItem* item = new QListWidgetItem(
                QString("📄 %1 — %2 (%3)").arg(q.value(1).toString(), q.value(2).toString(), q.value(3).toString()),
                m_lstPersonDocs);
            // Store the filename for the double-click open handler.
            item->setData(Qt::UserRole, q.value(4).toString());
            item->setToolTip("Double-click to open the stored document.");
        }
    }
    if (m_lstPersonDocs->count() == 0) {
        m_lstPersonDocs->addItem("No linked documents (file membership forms in the Document Vault).");
    }
}

void PeopleWidget::onOpenPersonDocument(QListWidgetItem* item) {
    if (!item) return;
    const QString fileName = item->data(Qt::UserRole).toString();
    if (fileName.isEmpty()) return; // placeholder row

    const QString abs = DatabaseManager::vaultDirPath() + "/" + fileName;
    if (!QFile::exists(abs)) {
        QMessageBox::warning(this, "File Missing",
            QString("The document was not found at:\n%1\n\nIt may have been removed or the vault moved.").arg(abs));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(abs));
}

void PeopleWidget::onSelectionChanged(const QModelIndex& current, const QModelIndex& previous) {
    Q_UNUSED(previous);
    if (!current.isValid()) {
        m_currentPersonId = 0;
        if (m_emptyStateWidget) m_emptyStateWidget->setVisible(true);
        if (m_dossierContainer) m_dossierContainer->setVisible(false);
        return;
    }

    if (m_emptyStateWidget) m_emptyStateWidget->setVisible(false);
    if (m_dossierContainer) m_dossierContainer->setVisible(true);

    QModelIndex sourceIndex = m_proxyModel->mapToSource(current);
    int row = sourceIndex.row();

    m_mapper->setCurrentIndex(row);

    QSqlRecord rec = m_peopleModel->record(row);
    int personId = rec.value("id").toInt();
    if (personId <= 0) {
        personId = m_peopleModel->data(m_peopleModel->index(row, 0)).toInt();
    }
    m_currentPersonId = personId;

    int householdId = 0;
    QString gender = "Unspecified";
    QString avatarPath = rec.value("avatar_path").toString();
    QString giftsText = rec.value("spiritual_gifts").toString();
    QString styleText = rec.value("relational_style").toString();
    QString notes = rec.value("admin_notes").toString();
    QString careFlag = rec.value("care_flag").toString();
    QString status = rec.value("membership_status").toString();
    QString dobStr = rec.value("date_of_birth").toString();
    QString fvStr = rec.value("first_visit_date").toString();
    QString jdStr = rec.value("join_date").toString();

    // Query directly from database to guarantee 100% fidelity without relational model column-shift bugs
    QString priArchetype = "Anchor";
    QString secArchetype = "None";
    {
        QSqlQuery hq;
        hq.prepare("SELECT household_id, gender, first_name, last_name, phone_number, email, "
                   "date_of_birth, avatar_path, care_flag, admin_notes, membership_status, "
                   "first_visit_date, join_date, spiritual_gifts, relational_style, interest_tags, "
                   "ministry_personality, personality_secondary "
                   "FROM people WHERE id = ?");
        hq.addBindValue(personId);
        if (hq.exec() && hq.next()) {
            householdId = hq.value(0).toInt();
            gender = hq.value(1).toString();
            m_firstNameEdit->setText(hq.value(2).toString());
            m_lastNameEdit->setText(hq.value(3).toString());
            m_phoneEdit->setText(hq.value(4).toString());
            m_emailEdit->setText(hq.value(5).toString());
            dobStr = hq.value(6).toString();
            avatarPath = hq.value(7).toString();
            careFlag = hq.value(8).toString();
            notes = hq.value(9).toString();
            status = hq.value(10).toString();
            fvStr = hq.value(11).toString();
            jdStr = hq.value(12).toString();
            giftsText = hq.value(13).toString();
            styleText = hq.value(14).toString();
            m_tagsEdit->setText(hq.value(15).toString());
            priArchetype = hq.value(16).toString();
            secArchetype = hq.value(17).toString();
        }
    }

    const QDate nullDate(1900, 1, 1);

    if (!dobStr.isEmpty()) m_birthDateEdit->setDate(QDate::fromString(dobStr, "yyyy-MM-dd"));
    else m_birthDateEdit->setDate(nullDate);

    if (!fvStr.isEmpty()) m_firstVisitEdit->setDate(QDate::fromString(fvStr, "yyyy-MM-dd"));
    else m_firstVisitEdit->setDate(nullDate);

    if (!jdStr.isEmpty()) m_joinDateEdit->setDate(QDate::fromString(jdStr, "yyyy-MM-dd"));
    else m_joinDateEdit->setDate(nullDate);

    m_careFlagCombo->blockSignals(true);
    int cfIdx = m_careFlagCombo->findText(careFlag, Qt::MatchFixedString);
    m_careFlagCombo->setCurrentIndex(cfIdx >= 0 ? cfIdx : 0);
    m_careFlagCombo->blockSignals(false);

    m_statusCombo->blockSignals(true);
    int stIdx = m_statusCombo->findText(status, Qt::MatchFixedString);
    m_statusCombo->setCurrentIndex(stIdx >= 0 ? stIdx : 0);
    m_statusCombo->blockSignals(false);

    m_genderCombo->blockSignals(true);
    int gIdx = m_genderCombo->findText(gender, Qt::MatchFixedString);
    m_genderCombo->setCurrentIndex(gIdx >= 0 ? gIdx : 2);
    m_genderCombo->blockSignals(false);

    m_householdCombo->blockSignals(true);
    int hIdx = m_householdCombo->findData(householdId);
    m_householdCombo->setCurrentIndex(hIdx >= 0 ? hIdx : 0);
    m_householdCombo->blockSignals(false);

    if (m_adminNotesText) {
        m_adminNotesText->setText(notes);
    }

    updateArchetypesFromText(priArchetype, secArchetype);
    updateChipsFromGiftsText(giftsText);
    updateChipsFromStyleText(styleText);
    renderTagChips();
    refreshHouseholdMembersList(householdId, personId);
    loadAvatar(avatarPath);

    // Update Pinned Header
    QString householdName = m_householdCombo->currentText();
    QString fullName = QString("%1 %2").arg(m_firstNameEdit->text(), m_lastNameEdit->text()).trimmed();
    updateHeader(fullName, status, careFlag, householdName, jdStr);

    // Update Giving Summary
    updateGivingSummary(personId);
    updateDocumentsSection(personId);

    // Fetch Connections
    m_connectionsList->clear();
    QSqlQuery qConn;
    qConn.prepare("SELECT g.name, er.relationship_type FROM entity_relationships er JOIN groups g ON er.target_id = g.id WHERE er.source_type = 'Person' AND er.target_type = 'Group' AND er.source_id = ?");
    qConn.addBindValue(personId);
    if (qConn.exec()) {
        while (qConn.next()) {
            m_connectionsList->addItem(QString("👥 %1 (%2)").arg(qConn.value(0).toString(), qConn.value(1).toString()));
        }
    }
    if (m_connectionsList->count() == 0) {
        m_connectionsList->addItem("No active group assignments.");
    }

    updateHistoryTimeline(personId);
    setDirty(false);
}

void PeopleWidget::updateHistoryTimeline(int personId) {
    if (!m_historyList) return;
    m_historyList->clear();

    QSqlQuery qHist;
    qHist.prepare(
        "SELECT id, timestamp, type, text, follow_up, src FROM ("
        "  SELECT id, timestamp, activity_type AS type, details AS text, 0 AS follow_up, 'member_history' AS src "
        "  FROM member_history WHERE person_id = ? "
        "  UNION ALL "
        "  SELECT id, timestamp, care_type AS type, notes AS text, follow_up, 'care_logs' AS src "
        "  FROM care_logs WHERE person_id = ?"
        ") ORDER BY timestamp DESC LIMIT 20");
    qHist.addBindValue(personId);
    qHist.addBindValue(personId);
    if (qHist.exec()) {
        while (qHist.next()) {
            int logId = qHist.value(0).toInt();
            QString ts = qHist.value(1).toString();
            QString type = qHist.value(2).toString();
            QString text = qHist.value(3).toString();
            bool followUp = qHist.value(4).toInt() != 0;
            QString srcTable = qHist.value(5).toString();

            QString badge = "🕊️ " + type;
            if (type == "Counseling") badge = "💬 " + type;
            else if (type == "Visitation" || type == "Hospital Visit") badge = "🏥 " + type;
            else if (type == "Prayer") badge = "🙏 " + type;
            else if (type == "Follow-up Call") badge = "📞 " + type;
            else if (type == "Benevolence") badge = "🤝 " + type;

            QString itemStr = QString("%1  [%2]%3\n%4")
                .arg(badge, ts, (followUp ? "  [🔔 Follow-up Needed]" : ""), text);

            QListWidgetItem* item = new QListWidgetItem(itemStr, m_historyList);
            item->setData(Qt::UserRole, logId);
            item->setData(Qt::UserRole + 1, srcTable);
            item->setData(Qt::UserRole + 2, text);
            if (followUp) {
                item->setForeground(QColor("#fab387"));
            }
        }
    }
    if (m_historyList->count() == 0) {
        m_historyList->addItem("No pastoral care entries logged yet.");
    }
}

void PeopleWidget::onLogCareEntry() {
    if (m_currentPersonId <= 0) {
        QMessageBox::information(this, "Select a Person", "Select a person profile before logging a care entry.");
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle("Log Pastoral Care Entry");
    dlg.resize(380, 260);
    QFormLayout* form = new QFormLayout(&dlg);

    QComboBox* typeCombo = new QComboBox(&dlg);
    typeCombo->addItems({"Counseling", "Visitation", "Prayer", "Hospital Visit", "Follow-up Call", "Benevolence", "Discipleship", "Other"});
    form->addRow("Care Type:", typeCombo);

    QTextEdit* notesEdit = new QTextEdit(&dlg);
    notesEdit->setPlaceholderText("Describe the care provided, outcome, or next steps...");
    form->addRow("Notes:", notesEdit);

    QCheckBox* followUpCheck = new QCheckBox("Flag for follow-up", &dlg);
    form->addRow("", followUpCheck);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    QString type = typeCombo->currentText();
    QString notes = notesEdit->toPlainText().trimmed();
    if (notes.isEmpty()) {
        QMessageBox::warning(this, "Missing Notes", "Add a short note describing the care entry.");
        return;
    }

    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);
    q.prepare("INSERT INTO care_logs (person_id, care_type, notes, follow_up) VALUES (?, ?, ?, ?)");
    q.addBindValue(m_currentPersonId);
    q.addBindValue(type);
    q.addBindValue(notes);
    if (q.exec()) {
        if (followUpCheck->isChecked()) {
            QSqlQuery qPastoral(db);
            qPastoral.prepare("INSERT INTO pastoral_activities (person_id, activity_type, activity_date, notes, outcome, follow_up_date, follow_up_done) "
                              "VALUES (?, ?, date('now', 'localtime'), ?, 'Logged from People Module', date('now', '+7 days', 'localtime'), 0)");
            qPastoral.addBindValue(m_currentPersonId);
            qPastoral.addBindValue(type);
            qPastoral.addBindValue(notes);
            qPastoral.exec();
        }
        updateHistoryTimeline(m_currentPersonId);
    } else {
        QMessageBox::critical(this, "Database Error", "Failed to save care entry: " + q.lastError().text());
        return;
    }
}

void PeopleWidget::selectPerson(int personId) {
    for (int r = 0; r < m_proxyModel->rowCount(); ++r) {
        QModelIndex proxyIdx = m_proxyModel->index(r, 0);
        QModelIndex srcIdx = m_proxyModel->mapToSource(proxyIdx);
        int id = m_peopleModel->record(srcIdx.row()).value("id").toInt();
        if (id == personId) {
            m_peopleTable->selectRow(r);
            break;
        }
    }
}

void PeopleWidget::applyRole(const QString& role) {
    // Roles only gate full-access vs read-only (Guest); modules gate features.
    bool isReadOnly = (role.compare("readonly", Qt::CaseInsensitive) == 0 || role.compare("guest", Qt::CaseInsensitive) == 0);
    bool canEdit = !isReadOnly;
    m_btnSave->setEnabled(canEdit);
    m_btnDeletePerson->setEnabled(canEdit);
    m_btnAddPerson->setEnabled(canEdit);
    m_btnAddHousehold->setEnabled(canEdit);
    m_btnManageHouseholds->setEnabled(canEdit);
    m_btnDeleteSelected->setEnabled(canEdit);
    m_btnBatchStatus->setEnabled(canEdit);

    QString tip = isReadOnly ? "Disabled in Guest (Read-Only) mode. Sign in with Admin PIN (1234) to modify records." : "";
    m_btnSave->setToolTip(tip);
    m_btnDeletePerson->setToolTip(tip);
    m_btnAddPerson->setToolTip(tip);
    m_btnAddHousehold->setToolTip(tip);
    m_btnManageHouseholds->setToolTip(tip);
    m_btnDeleteSelected->setToolTip(tip);
    m_btnBatchStatus->setToolTip(tip);
}

void PeopleWidget::onSearchTextChanged(const QString& text) {
    Q_UNUSED(text);
    reapplyFilter();
}

void PeopleWidget::onStatusFilterChanged(int id) {
    Q_UNUSED(id);
    reapplyFilter();
}

void PeopleWidget::reapplyFilter() {
    QString text = m_searchEdit->text().trimmed();
    int statusId = m_statusFilterGroup->checkedId();

    QStringList conditions;
    if (!text.isEmpty()) {
        QString escaped = text;
        escaped.replace("'", "''");
        conditions << QString("(first_name LIKE '%1%' OR last_name LIKE '%1%' OR phone_number LIKE '%1%' OR email LIKE '%1%' OR membership_status LIKE '%1%')").arg(escaped);
    }

    switch (statusId) {
        case 1: conditions << "(membership_status = 'Member' OR membership_status = 'Regular Attender')"; break;
        case 2: conditions << "membership_status = 'Visitor'"; break;
        case 3: conditions << "membership_status = 'Regular Attender'"; break;
        case 4: conditions << "membership_status = 'Member'"; break;
        case 5: conditions << "membership_status = 'Inactive'"; break;
        default: break;
    }

    m_peopleModel->setFilter(conditions.join(" AND "));
    m_peopleModel->select();
    while (m_peopleModel->canFetchMore()) {
        m_peopleModel->fetchMore();
    }

    // Re-apply active sort mode so filtered results are immediately and properly sorted
    onSortByChanged(m_cmbSortBy->currentIndex());
}

void PeopleWidget::refreshModel() {
    populateHouseholdCombo();
    m_householdModel->select();
    m_peopleModel->select();
    while (m_peopleModel->canFetchMore()) {
        m_peopleModel->fetchMore();
    }
    refreshTagCompleter();
    onSortByChanged(m_cmbSortBy->currentIndex());
}

void PeopleWidget::onSortByChanged(int index) {
    switch (index) {
        case 0: // Last Name (A - Z)
            m_peopleModel->setSort(3, Qt::AscendingOrder);
            m_proxyModel->setSortMode(PeopleProxyModel::NormalColumnSort);
            m_proxyModel->sort(0, Qt::AscendingOrder);
            m_peopleTable->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
            break;
        case 1: // Last Name (Z - A)
            m_peopleModel->setSort(3, Qt::DescendingOrder);
            m_proxyModel->setSortMode(PeopleProxyModel::NormalColumnSort);
            m_proxyModel->sort(0, Qt::DescendingOrder);
            m_peopleTable->horizontalHeader()->setSortIndicator(0, Qt::DescendingOrder);
            break;
        case 2: // First Name (A - Z)
            m_peopleModel->setSort(2, Qt::AscendingOrder);
            m_proxyModel->setSortMode(PeopleProxyModel::SortByFirstName);
            m_proxyModel->sort(0, Qt::AscendingOrder);
            m_peopleTable->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
            break;
        case 3: // First Name (Z - A)
            m_peopleModel->setSort(2, Qt::DescendingOrder);
            m_proxyModel->setSortMode(PeopleProxyModel::SortByFirstName);
            m_proxyModel->sort(0, Qt::DescendingOrder);
            m_peopleTable->horizontalHeader()->setSortIndicator(0, Qt::DescendingOrder);
            break;
        case 4: // Family Name (A - Z)
            m_proxyModel->setSortMode(PeopleProxyModel::NormalColumnSort);
            m_proxyModel->sort(1, Qt::AscendingOrder);
            m_peopleTable->horizontalHeader()->setSortIndicator(1, Qt::AscendingOrder);
            break;
        case 5: // Membership Status
            m_proxyModel->setSortMode(PeopleProxyModel::NormalColumnSort);
            m_proxyModel->sort(4, Qt::AscendingOrder);
            m_peopleTable->horizontalHeader()->setSortIndicator(4, Qt::AscendingOrder);
            break;
        case 6: // Join Date (Newest)
            m_proxyModel->setSortMode(PeopleProxyModel::SortByJoinDate);
            m_proxyModel->sort(0, Qt::DescendingOrder);
            break;
        case 7: // Join Date (Oldest)
            m_proxyModel->setSortMode(PeopleProxyModel::SortByJoinDate);
            m_proxyModel->sort(0, Qt::AscendingOrder);
            break;
        case 8: // Birth Date
            m_proxyModel->setSortMode(PeopleProxyModel::SortByBirthDate);
            m_proxyModel->sort(0, Qt::AscendingOrder);
            break;
        case 9: // Gender
            m_proxyModel->setSortMode(PeopleProxyModel::SortByGender);
            m_proxyModel->sort(0, Qt::AscendingOrder);
            break;
        default:
            m_proxyModel->setSortMode(PeopleProxyModel::NormalColumnSort);
            m_proxyModel->sort(0, Qt::AscendingOrder);
            m_peopleTable->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
            break;
    }
    if (m_peopleTable->currentIndex().isValid()) {
        m_peopleTable->scrollTo(m_peopleTable->currentIndex());
    }
}

void PeopleWidget::onAddPerson() {
    // Reset any active search or status filter so the newly created person is immediately visible
    if (m_searchEdit) m_searchEdit->clear();
    if (m_statusFilterGroup && m_statusFilterGroup->button(0)) {
        m_statusFilterGroup->button(0)->setChecked(true);
    }
    reapplyFilter();

    QSqlDatabase db = QSqlDatabase::database();
    db.transaction();

    QSqlQuery q(db);
    q.prepare("INSERT INTO people (first_name, last_name, membership_status, join_date) VALUES (?, ?, ?, ?)");
    q.addBindValue("New");
    q.addBindValue("Member");
    q.addBindValue("Visitor");
    q.addBindValue(QDate::currentDate().toString("yyyy-MM-dd"));

    if (q.exec()) {
        int newId = q.lastInsertId().toInt();
        db.commit();
        DatabaseManager::logAudit("people", newId, "record", "New Person", "CREATED", "admin");

        m_peopleModel->revertAll();
        m_peopleModel->select();
        reapplyFilter();
        selectPerson(newId);
        m_firstNameEdit->setFocus();
        m_firstNameEdit->selectAll();
    } else {
        db.rollback();
        QMessageBox::critical(this, "Database Error", "Failed to create new member profile: " + q.lastError().text());
    }
}

void PeopleWidget::onAddHousehold() {
    bool ok;
    QString familyName = QInputDialog::getText(this, "Add New Household", "Enter Family / Household Name:", QLineEdit::Normal, "", &ok);
    if (ok && !familyName.trimmed().isEmpty()) {
        QSqlDatabase db = QSqlDatabase::database();
        QSqlQuery q(db);
        q.prepare("INSERT INTO households (family_name) VALUES (?)");
        q.addBindValue(familyName.trimmed());
        if (q.exec()) {
            DatabaseManager::logAudit("households", q.lastInsertId().toInt(), "family_name", familyName.trimmed(), "CREATED", "admin");
            populateHouseholdCombo();
            m_peopleModel->select();
            QMessageBox::information(this, "Success", QString("Household '%1' added successfully.").arg(familyName.trimmed()));
        } else {
            QMessageBox::critical(this, "Error", "Failed to create household: " + q.lastError().text());
        }
    }
}

void PeopleWidget::onChangeAvatar() {
    QString fileName = QFileDialog::getOpenFileName(this, "Select Profile Avatar Image", "", "Images (*.png *.jpg *.jpeg *.bmp)");
    if (!fileName.isEmpty()) {
        m_selectedAvatarPath = fileName;
        m_avatarPathEdit->setText(fileName);
        loadAvatar(fileName);
        setDirty(true);
    }
}

void PeopleWidget::loadAvatar(const QString& path) {
    if (!path.isEmpty() && QFile::exists(path)) {
        QPixmap pix(path);
        if (!pix.isNull()) {
            QPixmap scaled = pix.scaled(64, 64, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            QPixmap rounded(64, 64);
            rounded.fill(Qt::transparent);
            QPainter p(&rounded);
            p.setRenderHint(QPainter::Antialiasing);
            QPainterPath clipPath;
            clipPath.addRoundedRect(0, 0, 64, 64, 32, 32);
            p.setClipPath(clipPath);
            p.drawPixmap(0, 0, scaled);
            p.end();

            m_avatarLabel->setPixmap(rounded);
            m_avatarLabel->setStyleSheet("border: 2px solid palette(highlight); border-radius: 32px;");
            return;
        }
    }

    m_avatarLabel->clear();
    m_avatarLabel->setText("👤");
    const ThemeTokens tok = ThemeManager::instance().currentTokens();
    m_avatarLabel->setStyleSheet(
        QString("border: 2px dashed %1; border-radius: 32px; background-color: %2; font-size: 24px; color: %3;")
            .arg(tok.borderColor.name(), tok.bgInput.name(), tok.accentPrimary.name()));
}

void PeopleWidget::onSaveProfile() {
    int personId = m_currentPersonId;
    if (personId <= 0) {
        QModelIndex current = m_peopleTable->currentIndex();
        if (current.isValid()) {
            QModelIndex sourceIndex = m_proxyModel->mapToSource(current);
            personId = m_peopleModel->record(sourceIndex.row()).value("id").toInt();
        }
    }

    if (personId <= 0) {
        QMessageBox::warning(this, "No Selection", "Please select a person profile from the roster list first.");
        return;
    }

    QString fn = m_firstNameEdit->text().trimmed();
    QString ln = m_lastNameEdit->text().trimmed();
    QString phone = m_phoneEdit->text().trimmed();
    QString email = m_emailEdit->text().trimmed();

    // Store "unknown" dates as empty rather than a placeholder timestamp so
    // the 1900-01-01 sentinel never leaks into the database.
    auto dateOrEmpty = [](const QDateEdit* edit) {
        QDate d = edit->date();
        return (d == QDate(1900, 1, 1)) ? QString() : d.toString("yyyy-MM-dd");
    };
    QString birthDate = dateOrEmpty(m_birthDateEdit);
    QString avatarPath = m_avatarPathEdit->text().trimmed();
    QString status = m_statusCombo->currentText();
    QString firstVisit = dateOrEmpty(m_firstVisitEdit);
    QString joinDate = dateOrEmpty(m_joinDateEdit);
    int selectedHouseholdId = m_householdCombo->currentData().toInt();
    QString gender = m_genderCombo->currentText();
    QString giftsText = m_giftsEdit->text().trimmed();
    QString styleText = m_styleEdit->text().trimmed();
    QString tagsText = m_tagsEdit->text().trimmed();
    QString priArchetype = m_primaryArchetypeEdit->text().trimmed();
    if (priArchetype.isEmpty()) priArchetype = "Anchor";
    QString secArchetype = m_cmbSecondaryArchetype->currentData().toString();
    if (secArchetype.isEmpty() || secArchetype == "None" || secArchetype.contains("None")) secArchetype = "";
    QString notes = m_adminNotesText ? m_adminNotesText->toPlainText() : "";
    QString careFlag = m_careFlagCombo ? m_careFlagCombo->currentText() : "Normal";

    QSqlDatabase db = QSqlDatabase::database();
    QSqlQuery q(db);
    q.prepare("UPDATE people SET "
              "first_name = :fn, "
              "last_name = :ln, "
              "phone_number = :phone, "
              "email = :email, "
              "date_of_birth = :dob, "
              "avatar_path = :avatar, "
              "care_flag = :care, "
              "admin_notes = :notes, "
              "membership_status = :status, "
              "first_visit_date = :fv, "
              "join_date = :jd, "
              "household_id = :hid, "
              "gender = :gender, "
              "spiritual_gifts = :gifts, "
              "relational_style = :style, "
              "interest_tags = :tags, "
              "ministry_personality = :pri, "
              "personality_secondary = :sec "
              "WHERE id = :id");

    q.bindValue(":fn", fn);
    q.bindValue(":ln", ln);
    q.bindValue(":phone", phone);
    q.bindValue(":email", email);
    q.bindValue(":dob", birthDate);
    q.bindValue(":avatar", avatarPath);
    q.bindValue(":care", careFlag);
    q.bindValue(":notes", notes);
    q.bindValue(":status", status);
    q.bindValue(":fv", firstVisit);
    q.bindValue(":jd", joinDate);
    q.bindValue(":hid", selectedHouseholdId > 0 ? QVariant(selectedHouseholdId) : QVariant(QMetaType(QMetaType::Int)));
    q.bindValue(":gender", gender);
    q.bindValue(":gifts", giftsText);
    q.bindValue(":style", styleText);
    q.bindValue(":tags", tagsText);
    q.bindValue(":pri", priArchetype);
    q.bindValue(":sec", secArchetype);
    q.bindValue(":id", personId);

    if (q.exec()) {
        m_peopleModel->revertAll();
        m_peopleModel->select();
        reapplyFilter();
        selectPerson(personId);
        refreshTagCompleter();
        setDirty(false);
        updateHeader(QString("%1 %2").arg(fn, ln), status, careFlag, m_householdCombo->currentText(), joinDate);
        DatabaseManager::logAudit("people", personId, "profile", "Profile Modifications", "UPDATED", "admin");
        QMessageBox::information(this, "Success", QString("Profile for '%1 %2' saved successfully.").arg(fn, ln));
    } else {
        QMessageBox::critical(this, "Database Error", "Failed to update profile: " + q.lastError().text());
    }
}

void PeopleWidget::onDeletePerson() {
    int personId = m_currentPersonId;
    QString fullName;
    QModelIndex current = m_peopleTable->currentIndex();
    if (current.isValid()) {
        QModelIndex sourceIndex = m_proxyModel->mapToSource(current);
        int row = sourceIndex.row();
        if (personId <= 0) personId = m_peopleModel->record(row).value("id").toInt();
        fullName = QString("%1 %2").arg(
            m_peopleModel->record(row).value("first_name").toString(),
            m_peopleModel->record(row).value("last_name").toString()
        );
    }

    if (personId <= 0) {
        QMessageBox::warning(this, "No Selection", "Please select a person to delete.");
        return;
    }

    if (fullName.trimmed().isEmpty()) {
        QSqlQuery qName;
        qName.prepare("SELECT first_name, last_name FROM people WHERE id = ?");
        qName.addBindValue(personId);
        if (qName.exec() && qName.next()) {
            fullName = QString("%1 %2").arg(qName.value(0).toString(), qName.value(1).toString());
        }
    }

    auto res = QMessageBox::question(this, "Confirm Member Deletion", 
                                     QString("Are you sure you want to permanently delete '%1' and all care/attendance history?").arg(fullName),
                                     QMessageBox::Yes | QMessageBox::No);
    if (res == QMessageBox::Yes) {
        QSqlDatabase db = QSqlDatabase::database();
        db.transaction();

        QSqlQuery q(db);
        if (deletePersonCascade(q, personId)) {
            db.commit();
            DatabaseManager::logAudit("people", personId, "record", fullName, "DELETED", "admin");
            m_peopleModel->select();
            reapplyFilter();
            QMessageBox::information(this, "Success", "Person profile deleted successfully.");
        } else {
            db.rollback();
            QMessageBox::critical(this, "Database Error", "Failed to delete person: " + q.lastError().text());
        }
    }
}

bool PeopleWidget::deletePersonCascade(QSqlQuery& q, int personId) {
    // Remove child activity & tracking records first so foreign-key constraints don't block deletion
    const QStringList childTables = {
        "pastoral_activities", "member_history", "care_logs", "attendance",
        "member_skills", "service_volunteers", "checkout_logs"
    };
    for (const QString& t : childTables) {
        q.prepare(QString("DELETE FROM %1 WHERE person_id = ?").arg(t));
        q.addBindValue(personId);
        if (!q.exec()) {
            qWarning() << "deletePersonCascade failed on" << t << ":" << q.lastError().text();
            return false;
        }
    }

    // Set splits person_id to NULL to preserve double-entry accounting ledger balance and debit/credit equality
    q.prepare("UPDATE splits SET person_id = NULL WHERE person_id = ?");
    q.addBindValue(personId);
    if (!q.exec()) {
        qWarning() << "deletePersonCascade failed on splits:" << q.lastError().text();
        return false;
    }

    // Delete direct contributions records
    q.prepare("DELETE FROM contributions WHERE person_id = ?");
    q.addBindValue(personId);
    if (!q.exec()) {
        qWarning() << "deletePersonCascade failed on contributions:" << q.lastError().text();
        return false;
    }

    // Unlink document vault attachments
    q.prepare("UPDATE vault_documents SET person_id = NULL WHERE person_id = ?");
    q.addBindValue(personId);
    q.exec();

    // Unlink task ownership
    q.prepare("UPDATE ministry_tasks SET owner_id = NULL WHERE owner_id = ?");
    q.addBindValue(personId);
    q.exec();

    // Polymorphic relationships can reference the person as source or target.
    q.prepare("DELETE FROM entity_relationships WHERE (source_type = 'Person' AND source_id = ?) "
              "OR (target_type = 'Person' AND target_id = ?)");
    q.addBindValue(personId);
    q.addBindValue(personId);
    if (!q.exec()) {
        qWarning() << "deletePersonCascade failed on entity_relationships:" << q.lastError().text();
        return false;
    }

    // Clear group contact person reference
    q.prepare("UPDATE groups SET contact_person_id = NULL WHERE contact_person_id = ?");
    q.addBindValue(personId);
    if (!q.exec()) {
        qWarning() << "deletePersonCascade failed on groups contact_person_id:" << q.lastError().text();
        return false;
    }

    // Clear graph layout cache
    q.prepare("DELETE FROM graph_layout WHERE entity_type = 'Person' AND entity_id = ?");
    q.addBindValue(personId);
    q.exec();

    q.prepare("DELETE FROM people WHERE id = ?");
    q.addBindValue(personId);
    if (!q.exec()) {
        qWarning() << "deletePersonCascade failed on people:" << q.lastError().text();
        return false;
    }
    return true;
}

void PeopleWidget::onDeleteSelectedMembers() {
    QModelIndexList selectedIndexes = m_peopleTable->selectionModel()->selectedIndexes();
    QSet<int> rowSet;
    for (const QModelIndex& idx : selectedIndexes) {
        rowSet.insert(idx.row());
    }
    if (rowSet.isEmpty() && m_peopleTable->currentIndex().isValid()) {
        rowSet.insert(m_peopleTable->currentIndex().row());
    }

    if (rowSet.isEmpty() && m_currentPersonId <= 0) {
        QMessageBox::warning(this, "No Selection", "Please select one or more people from the roster table first.");
        return;
    }

    QStringList names;
    QList<int> ids;
    for (int r : rowSet) {
        QModelIndex proxyIdx = m_proxyModel->index(r, 0);
        QModelIndex srcIdx = m_proxyModel->mapToSource(proxyIdx);
        int id = m_peopleModel->record(srcIdx.row()).value("id").toInt();
        if (id <= 0) id = m_peopleModel->data(m_peopleModel->index(srcIdx.row(), 0)).toInt();
        if (id <= 0) continue;

        QString fn = m_peopleModel->data(m_peopleModel->index(srcIdx.row(), 2)).toString();
        QString ln = m_peopleModel->data(m_peopleModel->index(srcIdx.row(), 3)).toString();
        ids << id;
        names << QString("• %1 %2").arg(fn, ln);
    }

    if (ids.isEmpty() && m_currentPersonId > 0) {
        ids << m_currentPersonId;
        names << QString("• %1 %2").arg(m_firstNameEdit->text(), m_lastNameEdit->text());
    }

    if (ids.isEmpty()) {
        QMessageBox::warning(this, "No Selection", "Please select one or more people from the roster table first.");
        return;
    }

    auto res = QMessageBox::question(
        this, "Confirm Batch Deletion",
        QString("Are you sure you want to permanently delete the following %1 selected member(s)?\n\n%2")
            .arg(ids.size()).arg(names.join("\n")),
        QMessageBox::Yes | QMessageBox::No
    );

    if (res == QMessageBox::Yes) {
        QSqlDatabase db = QSqlDatabase::database();
        db.transaction();

        QSqlQuery q(db);
        int deleted = 0;
        for (int id : ids) {
            if (deletePersonCascade(q, id)) {
                ++deleted;
                DatabaseManager::logAudit("people", id, "record", "Batch Delete", "DELETED", "admin");
            }
        }
        if (deleted > 0) {
            db.commit();
        } else {
            db.rollback();
        }

        m_peopleModel->revertAll();
        m_peopleModel->select();
        reapplyFilter();
        if (m_proxyModel->rowCount() > 0) {
            m_peopleTable->selectRow(0);
        } else {
            m_currentPersonId = 0;
            loadAvatar("");
        }
        QMessageBox::information(this, "Success", QString("Successfully deleted %1 people.").arg(deleted));
    }
}


void PeopleWidget::onBatchSetStatus() {
    QModelIndexList selectedRows = m_peopleTable->selectionModel()->selectedRows();
    if (selectedRows.isEmpty()) {
        QMessageBox::warning(this, "No Selection", "Please select members in the table first.");
        return;
    }

    QStringList options = {"Visitor", "Regular Attender", "Member", "Inactive"};
    bool ok;
    QString status = QInputDialog::getItem(this, "Batch Set Status", "Select new status for selected members:", options, 0, false, &ok);
    if (ok && !status.isEmpty()) {
        QSqlDatabase db = QSqlDatabase::database();
        db.transaction();

        QSqlQuery q(db);
        int count = 0;
        for (const QModelIndex& idx : selectedRows) {
            QModelIndex srcIdx = m_proxyModel->mapToSource(idx);
            int id = m_peopleModel->record(srcIdx.row()).value("id").toInt();
            q.prepare("UPDATE people SET membership_status = ? WHERE id = ?");
            q.addBindValue(status);
            q.addBindValue(id);
            if (q.exec()) count++;
        }
        db.commit();
        m_peopleModel->select();
        reapplyFilter();
        QMessageBox::information(this, "Success", QString("Updated status to '%1' for %2 members.").arg(status).arg(count));
    }
}

void PeopleWidget::onDeleteHousehold() {
    QSqlDatabase db = DatabaseManager::instance().database();
    
    QDialog dlg(this);
    dlg.setWindowTitle("Purge & Manage Families / Households");
    dlg.resize(500, 360);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);
    lay->setSpacing(10);

    QLabel* info = new QLabel("Select a family / household to view members or purge record:", &dlg);
    lay->addWidget(info);

    QListWidget* houseList = new QListWidget(&dlg);
    lay->addWidget(houseList);

    auto refreshHouseholds = [houseList, &db]() {
        houseList->clear();
        QSqlQuery q("SELECT h.id, h.family_name, COUNT(p.id) FROM households h LEFT JOIN people p ON h.id = p.household_id GROUP BY h.id, h.family_name ORDER BY h.family_name", db);
        while (q.next()) {
            int id = q.value(0).toInt();
            QString name = q.value(1).toString();
            int memberCount = q.value(2).toInt();
            QListWidgetItem* item = new QListWidgetItem(QString("%1 (%2 member%3)").arg(name).arg(memberCount).arg(memberCount == 1 ? "" : "s"), houseList);
            item->setData(Qt::UserRole, id);
            item->setData(Qt::UserRole + 1, name);
            item->setData(Qt::UserRole + 2, memberCount);
        }
    };
    refreshHouseholds();

    QHBoxLayout* btnRow = new QHBoxLayout();
    QPushButton* btnAdd = new QPushButton(" Add Family", &dlg);
    btnAdd->setIcon(IconManager::instance().getPrimaryIcon("plus", 14));
    btnAdd->setObjectName("primaryBtn");

    QPushButton* btnEdit = new QPushButton(" Rename / Edit", &dlg);
    btnEdit->setIcon(IconManager::instance().getIcon("edit-3", QColor(), 14));
    btnEdit->setObjectName("secondaryBtn");

    QPushButton* btnDelete = new QPushButton(" Purge Record", &dlg);
    btnDelete->setIcon(IconManager::instance().getDangerIcon("trash-2", 14));
    btnDelete->setObjectName("dangerBtn");

    QPushButton* btnClose = new QPushButton("Close", &dlg);
    btnClose->setObjectName("secondaryBtn");

    btnRow->addWidget(btnAdd);
    btnRow->addWidget(btnEdit);
    btnRow->addWidget(btnDelete);
    btnRow->addStretch();
    btnRow->addWidget(btnClose);
    lay->addLayout(btnRow);

    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);

    auto refreshRelationalModels = [this]() {
        populateHouseholdCombo();
        m_peopleModel->select();
    };

    // 1. Add Family Handler
    connect(btnAdd, &QPushButton::clicked, &dlg, [this, &dlg, &db, refreshHouseholds, refreshRelationalModels]() {
        bool ok;
        QString familyName = QInputDialog::getText(&dlg, "Add New Household",
                                                   "Enter Family / Household Name:", QLineEdit::Normal,
                                                   "", &ok);
        if (ok && !familyName.trimmed().isEmpty()) {
            QSqlQuery q(db);
            q.prepare("INSERT INTO households (family_name) VALUES (?)");
            q.addBindValue(familyName.trimmed());
            if (q.exec()) {
                DatabaseManager::logAudit("households", q.lastInsertId().toInt(), "family_name", familyName.trimmed(), "CREATED", "admin");
                refreshRelationalModels();
                refreshHouseholds();
                QMessageBox::information(&dlg, "Success", QString("Household '%1' added successfully.").arg(familyName.trimmed()));
            } else {
                QMessageBox::critical(&dlg, "Error", "Failed to create household: " + q.lastError().text());
            }
        }
    });

    // 2. Edit Family Name Handler
    connect(btnEdit, &QPushButton::clicked, &dlg, [this, houseList, &dlg, &db, refreshHouseholds, refreshRelationalModels]() {
        QListWidgetItem* current = houseList->currentItem();
        if (!current) {
            QMessageBox::warning(houseList, "No Selection", "Please select a household from the list first.");
            return;
        }
        int hId = current->data(Qt::UserRole).toInt();
        QString hName = current->data(Qt::UserRole + 1).toString();

        if (hId == 1 || hName.toLower() == "unassigned") {
            QMessageBox::warning(houseList, "Action Denied", "The system default 'Unassigned' household cannot be renamed.");
            return;
        }

        bool ok;
        QString newName = QInputDialog::getText(&dlg, "Rename Household",
                                                QString("New name for '%1':").arg(hName),
                                                QLineEdit::Normal, hName, &ok);
        if (ok && !newName.trimmed().isEmpty() && newName.trimmed() != hName) {
            QSqlQuery q(db);
            q.prepare("UPDATE households SET family_name = ? WHERE id = ?");
            q.addBindValue(newName.trimmed());
            q.addBindValue(hId);
            if (q.exec()) {
                DatabaseManager::logAudit("households", hId, "family_name", newName.trimmed(), "UPDATED", "admin");
                refreshRelationalModels();
                refreshHouseholds();
                QMessageBox::information(&dlg, "Success", QString("Household renamed to '%1'.").arg(newName.trimmed()));
            } else {
                QMessageBox::critical(&dlg, "Error", "Failed to rename household: " + q.lastError().text());
            }
        }
    });

    // 3. Purge Household Handler
    connect(btnDelete, &QPushButton::clicked, &dlg, [this, houseList, &dlg, &db, refreshHouseholds, refreshRelationalModels]() {
        QListWidgetItem* current = houseList->currentItem();
        if (!current) {
            QMessageBox::warning(houseList, "No Selection", "Please select a household to purge.");
            return;
        }
        int hId = current->data(Qt::UserRole).toInt();
        QString hName = current->data(Qt::UserRole + 1).toString();
        int memberCount = current->data(Qt::UserRole + 2).toInt();

        if (hId == 1 || hName.toLower() == "unassigned") {
            QMessageBox::warning(houseList, "Action Denied", "The system default 'Unassigned' household record cannot be purged.");
            return;
        }

        auto res = QMessageBox::question(&dlg, "Confirm Household Purge",
                                         QString("Are you sure you want to purge '%1'?\n\n%2 member(s) will be reassigned to 'Unassigned'.")
                                             .arg(hName).arg(memberCount),
                                         QMessageBox::Yes | QMessageBox::No);
        if (res == QMessageBox::Yes) {
            db.transaction();

            QSqlQuery q1(db);
            q1.prepare("UPDATE people SET household_id = 1 WHERE household_id = ?");
            q1.addBindValue(hId);
            q1.exec();

            QSqlQuery q2(db);
            q2.prepare("DELETE FROM households WHERE id = ?");
            q2.addBindValue(hId);
            if (q2.exec()) {
                db.commit();
                DatabaseManager::logAudit("households", hId, "family_name", hName, "PURGED", "admin");
                refreshRelationalModels();
                refreshHouseholds();
                QMessageBox::information(&dlg, "Success", QString("Household '%1' purged. Members reassigned to Unassigned.").arg(hName));
            } else {
                db.rollback();
                QMessageBox::critical(&dlg, "Database Error", "Failed to purge household: " + q2.lastError().text());
            }
        }
    });

    dlg.exec();
}

void PeopleWidget::onPrintDirectory() {
    QString fileName = QFileDialog::getSaveFileName(this, "Export Member Directory PDF", "Member_Directory.pdf", "PDF Files (*.pdf)");
    if (fileName.isEmpty()) return;

    QPdfWriter writer(fileName);
    writer.setPageSize(QPageSize(QPageSize::Letter));
    writer.setPageMargins(QMarginsF(30, 30, 30, 30));

    QTextDocument doc;
    QString html = "<html><head><style>"
                   "body { font-family: sans-serif; color: #11111b; }"
                   "h1 { color: #89b4fa; border-bottom: 2px solid #89b4fa; padding-bottom: 5px; }"
                   "table { width: 100%; border-collapse: collapse; margin-top: 15px; }"
                   "th { background-color: palette(mid); color: #ffffff; padding: 8px; text-align: left; font-size: 11px; }"
                   "td { padding: 6px; border-bottom: 1px solid #cdd6f4; font-size: 10px; }"
                   "tr:nth-child(even) { background-color: #f2f4f8; }"
                   "</style></head><body>"                    + ChurchProfile::renderHtmlHeader("Official Member Directory",
                                                      QString("Exported on %1").arg(AppDateFormatter::formatDate(QDate::currentDate())),
                                                      /*includeTaxId=*/false)
                    + "<table>"
                    "<tr><th>Name</th><th>Household</th><th>Phone</th><th>Email</th><th>Status</th></tr>";

    for (int r = 0; r < m_proxyModel->rowCount(); ++r) {
        QString name = m_proxyModel->index(r, 0).data().toString();
        QString house = m_proxyModel->index(r, 1).data().toString();
        QString phone = m_proxyModel->index(r, 2).data().toString();
        QString email = m_proxyModel->index(r, 3).data().toString();
        QString status = m_proxyModel->index(r, 4).data().toString();

        html += QString("<tr><td><b>%1</b></td><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>")
                    .arg(name, house, phone, email, status);
    }

    html += "</table></body></html>";
    doc.setHtml(html);
    doc.print(&writer);

    QMessageBox::information(this, "Directory Printed", QString("Member directory PDF successfully exported to:\n%1").arg(fileName));
}

QStringList PeopleWidget::parseCsvLine(const QString& line) {
    QStringList fields;
    QString current;
    bool inQuotes = false;
    for (int i = 0; i < line.length(); ++i) {
        QChar c = line.at(i);
        if (c == '"') {
            if (inQuotes && i + 1 < line.length() && line.at(i + 1) == '"') {
                // Escaped double quote
                current += '"';
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == ',' && !inQuotes) {
            fields.append(current.trimmed());
            current.clear();
        } else {
            current += c;
        }
    }
    fields.append(current.trimmed());
    return fields;
}

QString PeopleWidget::generateVCard(const QString& firstName, const QString& lastName, const QString& phone,
                                   const QString& email, const QString& bday, const QString& status,
                                   const QString& careFlag, const QString& notes) {
    QString vcf;
    vcf += "BEGIN:VCARD\r\n";
    vcf += "VERSION:3.0\r\n";
    vcf += QString("N:%1;%2;;;\r\n").arg(lastName, firstName);
    vcf += QString("FN:%1 %2\r\n").arg(firstName, lastName).trimmed();
    if (!phone.isEmpty()) {
        vcf += QString("TEL;TYPE=CELL,VOICE:%1\r\n").arg(phone);
    }
    if (!email.isEmpty()) {
        vcf += QString("EMAIL;TYPE=INTERNET,PREF:%1\r\n").arg(email);
    }
    if (!bday.isEmpty() && bday != "1900-01-01") {
        vcf += QString("BDAY:%1\r\n").arg(bday);
    }
    if (!status.isEmpty()) {
        vcf += QString("ROLE:%1\r\n").arg(status);
        vcf += QString("CATEGORIES:OpenEkklesia,%1\r\n").arg(status);
    } else {
        vcf += "CATEGORIES:OpenEkklesia\r\n";
    }
    // Confidentiality: the vCard intentionally carries no care flag and no
    // admin/pastoral notes — a contact export must never leak the care trail.
    Q_UNUSED(careFlag);
    Q_UNUSED(notes);
    vcf += "ORG:OpenEkklesia Congregation\r\n";
    vcf += "END:VCARD\r\n";
    return vcf;
}

void PeopleWidget::onExportVCard() {
    int personId = m_currentPersonId;
    if (personId <= 0) {
        QModelIndex current = m_peopleTable->currentIndex();
        if (current.isValid()) {
            QModelIndex sourceIndex = m_proxyModel->mapToSource(current);
            personId = m_peopleModel->record(sourceIndex.row()).value("id").toInt();
        }
    }

    if (personId <= 0) {
        QMessageBox::warning(this, "No Selection", "Please select a member profile from the table to export as vCard.");
        return;
    }

    QString fn = m_firstNameEdit->text().trimmed();
    QString ln = m_lastNameEdit->text().trimmed();
    QString phone = m_phoneEdit->text().trimmed();
    QString email = m_emailEdit->text().trimmed();
    QString bday = (m_birthDateEdit->date() == QDate(1900, 1, 1)) ? "" : m_birthDateEdit->date().toString("yyyy-MM-dd");
    QString status = m_statusCombo->currentText();
    QString careFlag = m_careFlagCombo ? m_careFlagCombo->currentText() : "Normal";
    QString notes = m_adminNotesText ? m_adminNotesText->toPlainText().trimmed() : "";

    QString defaultFileName = QString("%1_%2.vcf").arg(fn, ln).replace(" ", "_");
    if (defaultFileName == "_.vcf" || defaultFileName.isEmpty()) defaultFileName = "contact.vcf";

    QString savePath = QFileDialog::getSaveFileName(this, "Export Contact as vCard", defaultFileName, "vCard Files (*.vcf)");
    if (savePath.isEmpty()) return;

    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Export Error", "Could not open file for writing: " + file.errorString());
        return;
    }

    QString vcardText = generateVCard(fn, ln, phone, email, bday, status, careFlag, notes);
    file.write(vcardText.toUtf8());
    file.close();

    QMessageBox::information(this, "vCard Exported", QString("vCard contact for '%1 %2' successfully exported to:\n%3").arg(fn, ln, savePath));
}

void PeopleWidget::onImportCSV() {
    QString fileName = QFileDialog::getOpenFileName(this, "Import Members from CSV", "", "CSV Files (*.csv)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "File Error", "Could not open selected CSV file.");
        return;
    }

    QTextStream in(&file);
    if (in.atEnd()) return;

    QString headerLine = in.readLine();
    QStringList headerFields = parseCsvLine(headerLine);

    QDialog mapDlg(this);
    mapDlg.setWindowTitle("Import Members — Column Alignment Wizard");
    QVBoxLayout* dlgLayout = new QVBoxLayout(&mapDlg);
    QLabel* intro = new QLabel("Map CSV Columns to Member Directory Fields:", &mapDlg);
    dlgLayout->addWidget(intro);

    QFormLayout* formLayout = new QFormLayout();
    QStringList options = {"-- Ignore --"};
    for (int i = 0; i < headerFields.size(); ++i) {
        options << QString("Col %1: %2").arg(i + 1).arg(headerFields.at(i));
    }

    QComboBox* comboFirst = new QComboBox(&mapDlg); comboFirst->addItems(options);
    QComboBox* comboLast = new QComboBox(&mapDlg); comboLast->addItems(options);
    QComboBox* comboPhone = new QComboBox(&mapDlg); comboPhone->addItems(options);
    QComboBox* comboEmail = new QComboBox(&mapDlg); comboEmail->addItems(options);
    QComboBox* comboStatus = new QComboBox(&mapDlg); comboStatus->addItems(options);

    formLayout->addRow("First Name *:", comboFirst);
    formLayout->addRow("Last Name *:", comboLast);
    formLayout->addRow("Phone Number:", comboPhone);
    formLayout->addRow("Email Address:", comboEmail);
    formLayout->addRow("Membership Status:", comboStatus);

    dlgLayout->addLayout(formLayout);

    QPushButton* btnDoImport = new QPushButton("Import Members", &mapDlg);
    btnDoImport->setObjectName("primaryBtn");
    dlgLayout->addWidget(btnDoImport);
    connect(btnDoImport, &QPushButton::clicked, &mapDlg, &QDialog::accept);

    if (mapDlg.exec() != QDialog::Accepted) return;

    QSqlDatabase db = QSqlDatabase::database();
    db.transaction();

    // Default to first available household (or 1)
    int defaultHouseholdId = 1;
    QSqlQuery hq("SELECT id FROM households ORDER BY id ASC LIMIT 1", db);
    if (hq.next()) {
        defaultHouseholdId = hq.value(0).toInt();
    }

    int importedCount = 0;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        QStringList cols = parseCsvLine(line);

        auto getVal = [&cols](QComboBox* cb) -> QString {
            int idx = cb->currentIndex() - 1;
            return (idx >= 0 && idx < cols.size()) ? cols.at(idx).trimmed() : "";
        };

        QString fn = getVal(comboFirst);
        QString ln = getVal(comboLast);
        QString phone = getVal(comboPhone);
        QString email = getVal(comboEmail);
        QString status = getVal(comboStatus);

        if (fn.isEmpty() && ln.isEmpty()) continue;

        QSqlQuery q(db);
        q.prepare("INSERT INTO people (first_name, last_name, phone_number, email, membership_status, household_id) VALUES (?, ?, ?, ?, ?, ?)");
        q.addBindValue(fn);
        q.addBindValue(ln);
        q.addBindValue(phone);
        q.addBindValue(email);
        q.addBindValue(status.isEmpty() ? "Member" : status);
        q.addBindValue(defaultHouseholdId);
        if (q.exec()) importedCount++;
    }

    db.commit();
    m_peopleModel->revertAll();
    m_peopleModel->select();
    reapplyFilter();
    QMessageBox::information(this, "Success", QString("Successfully imported %1 members!").arg(importedCount));
}

void PeopleWidget::onPeopleTableContextMenu(const QPoint& pos) {
    QModelIndex proxyIdx = m_peopleTable->indexAt(pos);
    if (!proxyIdx.isValid()) return;

    QModelIndex srcIdx = m_proxyModel->mapToSource(proxyIdx);
    int row = srcIdx.row();
    QSqlRecord rec = m_peopleModel->record(row);
    int personId = rec.value("id").toInt();
    if (personId <= 0) personId = m_peopleModel->data(m_peopleModel->index(row, 0)).toInt();
    QString firstName = rec.value("first_name").toString();
    QString lastName = rec.value("last_name").toString();
    QString phone = rec.value("phone_number").toString();
    QString email = rec.value("email").toString();
    QString careFlag = rec.value("care_flag").toString();
    QString status = rec.value("membership_status").toString();
    QString bday = rec.value("date_of_birth").toString();
    QString notes = rec.value("admin_notes").toString();
    QString fullName = QString("%1 %2").arg(firstName, lastName).trimmed();
    if (fullName.isEmpty()) fullName = "Member";

    QMenu menu(this);
    QAction* actProfile = menu.addAction(IconManager::instance().getIcon("user", QColor(), 14), QString("View %1's Dossier").arg(fullName));
    QAction* actCareNote = menu.addAction(IconManager::instance().getIcon("heart", QColor(), 14), "Log Pastoral Care Note...");
    QAction* actAssignGroup = menu.addAction(IconManager::instance().getIcon("user-plus", QColor(), 14), "Assign to Small Group...");
    
    QMenu* menuCare = menu.addMenu(IconManager::instance().getIcon("flag", QColor(), 14), "Set Pastoral Flag");
    QAction* actFlagNormal = menuCare->addAction("Normal / None");
    QAction* actFlagLeader = menuCare->addAction("Leader");
    QAction* actFlagRisk = menuCare->addAction("At-Risk");
    QAction* actFlagContact = menuCare->addAction("Needs Contact");
    QAction* actFlagGrief = menuCare->addAction("Grief Support");

    QMenu* menuStatus = menu.addMenu(IconManager::instance().getIcon("tag", QColor(), 14), "Set Membership Status");
    QAction* actStatMember = menuStatus->addAction("Member");
    QAction* actStatAttender = menuStatus->addAction("Regular Attender");
    QAction* actStatVisitor = menuStatus->addAction("Visitor");
    QAction* actStatInactive = menuStatus->addAction("Inactive");

    menu.addSeparator();
    QAction* actCopyPhone = nullptr;
    if (!phone.isEmpty()) actCopyPhone = menu.addAction(IconManager::instance().getIcon("phone", QColor(), 14), QString("Copy Phone (%1)").arg(phone));
    QAction* actCopyEmail = nullptr;
    if (!email.isEmpty()) actCopyEmail = menu.addAction(IconManager::instance().getIcon("mail", QColor(), 14), QString("Copy Email (%1)").arg(email));
    QAction* actExportVCard = menu.addAction(IconManager::instance().getIcon("file-text", QColor(), 14), "Export Contact as vCard (.vcf)...");

    menu.addSeparator();
    QAction* actDelete = menu.addAction(IconManager::instance().getDangerIcon("trash-2", 14), "Delete Profile...");

    QAction* selected = menu.exec(m_peopleTable->viewport()->mapToGlobal(pos));
    if (!selected) return;

    if (selected == actProfile) {
        selectPerson(personId);
    } else if (selected == actCareNote) {
        selectPerson(personId);
        onLogCareEntry();
    } else if (selected == actAssignGroup) {
        QDialog dlg(this);
        dlg.setWindowTitle(QString("Assign %1 to Group").arg(fullName));
        dlg.resize(320, 140);
        QFormLayout* form = new QFormLayout(&dlg);
        QComboBox* groupCombo = new QComboBox(&dlg);
        QSqlQuery gq("SELECT id, name FROM groups WHERE is_archived = 0 ORDER BY name");
        while (gq.next()) groupCombo->addItem(gq.value(1).toString(), gq.value(0).toInt());
        QLineEdit* roleEdit = new QLineEdit("Member", &dlg);
        form->addRow("Group:", groupCombo);
        form->addRow("Role:", roleEdit);
        QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form->addRow(bb);
        if (dlg.exec() == QDialog::Accepted && groupCombo->count() > 0) {
            int gid = groupCombo->currentData().toInt();
            QSqlQuery ins;
            ins.prepare("INSERT INTO entity_relationships (source_type, source_id, target_type, target_id, relationship_type, role) "
                        "VALUES ('Person', ?, 'Group', ?, 'member_of', ?)");
            ins.addBindValue(personId);
            ins.addBindValue(gid);
            ins.addBindValue(roleEdit->text().trimmed());
            ins.exec();
            QMessageBox::information(this, "Success", QString("%1 assigned to %2").arg(fullName, groupCombo->currentText()));
        }
    } else if (menuCare->actions().contains(selected)) {
        QString flag = selected->text();
        if (selected == actFlagNormal) flag = "Normal";
        QSqlQuery uq;
        uq.prepare("UPDATE people SET care_flag = ? WHERE id = ?");
        uq.addBindValue(flag);
        uq.addBindValue(personId);
        uq.exec();
        m_peopleModel->select();
        reapplyFilter();
        if (m_currentPersonId == personId) selectPerson(personId);
    } else if (menuStatus->actions().contains(selected)) {
        QString st = selected->text();
        QSqlQuery uq;
        uq.prepare("UPDATE people SET membership_status = ? WHERE id = ?");
        uq.addBindValue(st);
        uq.addBindValue(personId);
        uq.exec();
        m_peopleModel->select();
        reapplyFilter();
        if (m_currentPersonId == personId) selectPerson(personId);
    } else if (selected == actCopyPhone && !phone.isEmpty()) {
        QGuiApplication::clipboard()->setText(phone);
    } else if (selected == actCopyEmail && !email.isEmpty()) {
        QGuiApplication::clipboard()->setText(email);
    } else if (selected == actExportVCard) {
        selectPerson(personId);
        onExportVCard();
    } else if (selected == actDelete) {
        selectPerson(personId);
        onDeletePerson();
    }
}

void PeopleWidget::onTimelineContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_historyList->itemAt(pos);
    if (!item) return;

    int logId = item->data(Qt::UserRole).toInt();
    QString srcTable = item->data(Qt::UserRole + 1).toString();
    QString noteText = item->data(Qt::UserRole + 2).toString();

    if (logId <= 0 || srcTable.isEmpty()) return;

    QMenu menu(this);
    QAction* actEdit = menu.addAction(IconManager::instance().getIcon("edit-3", QColor(), 14), "Edit Log Note...");
    QAction* actDelete = menu.addAction(IconManager::instance().getDangerIcon("trash-2", 14), "Delete Log Entry");

    QAction* selected = menu.exec(m_historyList->viewport()->mapToGlobal(pos));
    if (!selected) return;

    if (selected == actEdit) {
        bool ok = false;
        QString newText = QInputDialog::getMultiLineText(this, "Edit Care Note", "Notes / Details:", noteText, &ok);
        if (ok && !newText.trimmed().isEmpty()) {
            QSqlQuery uq;
            if (srcTable == "care_logs") {
                uq.prepare("UPDATE care_logs SET notes = ? WHERE id = ?");
            } else {
                uq.prepare("UPDATE member_history SET details = ? WHERE id = ?");
            }
            uq.addBindValue(newText.trimmed());
            uq.addBindValue(logId);
            uq.exec();
            updateHistoryTimeline(m_currentPersonId);
        }
    } else if (selected == actDelete) {
        if (QMessageBox::question(this, "Delete Log Entry", "Are you sure you want to delete this care timeline entry?") == QMessageBox::Yes) {
            QSqlQuery dq;
            if (srcTable == "care_logs") {
                dq.prepare("DELETE FROM care_logs WHERE id = ?");
            } else {
                dq.prepare("DELETE FROM member_history WHERE id = ?");
            }
            dq.addBindValue(logId);
            dq.exec();
            updateHistoryTimeline(m_currentPersonId);
        }
    }
}

void PeopleWidget::applyDrillThroughFilter(const QString& type, const QString& val) {
    if (type == "status") {
        if (val == "Active") m_statusFilterGroup->button(1)->setChecked(true);
        else if (val == "Visitor" || val == "Visitors") m_statusFilterGroup->button(2)->setChecked(true);
        else if (val == "Regular Attender" || val == "Attenders") m_statusFilterGroup->button(3)->setChecked(true);
        else if (val == "Member" || val == "Members") m_statusFilterGroup->button(4)->setChecked(true);
        else if (val == "Inactive") m_statusFilterGroup->button(5)->setChecked(true);
        else m_statusFilterGroup->button(0)->setChecked(true);
        m_searchEdit->clear();
        reapplyFilter();
    } else if (type == "care_flag") {
        m_statusFilterGroup->button(0)->setChecked(true);
        m_searchEdit->setText(val);
    } else if (type == "unconnected") {
        m_statusFilterGroup->button(0)->setChecked(true);
        m_searchEdit->clear();
        m_peopleModel->setFilter("id NOT IN (SELECT DISTINCT source_id FROM entity_relationships WHERE source_type = 'Person' AND target_type = 'Group')");
        m_peopleModel->select();
        while (m_peopleModel->canFetchMore()) {
            m_peopleModel->fetchMore();
        }
        onSortByChanged(m_cmbSortBy->currentIndex());
    } else if (type == "lapsed_giver") {
        // Givers with a giving history but nothing since the inactivity window
        // (same rule as the dashboard and Pastoral Analytics).
        const int inactivityDays = qBound(7, QSettings("OpenEkklesia", "CHMS").value("inactivityDays", 30).toInt(), 365);
        m_statusFilterGroup->button(0)->setChecked(true);
        m_searchEdit->clear();
        m_peopleModel->setFilter(QString(
            "id IN (SELECT DISTINCT person_id FROM contributions) "
            "AND id NOT IN (SELECT DISTINCT person_id FROM contributions WHERE contribution_date >= date('now', '-%1 days'))")
            .arg(inactivityDays));
        m_peopleModel->select();
        while (m_peopleModel->canFetchMore()) {
            m_peopleModel->fetchMore();
        }
        onSortByChanged(m_cmbSortBy->currentIndex());
    } else {
        m_statusFilterGroup->button(0)->setChecked(true);
        m_searchEdit->setText(val);
    }
}
