#include "peoplewidget.h"
#include "../database/databasemanager.h"
#include "../core/thememanager.h"
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

        if (col == 0) { // Merged Name: "Last Name, First Name"
            QString firstName = sourceModel()->data(sourceModel()->index(srcRow, 2)).toString();
            QString lastName = sourceModel()->data(sourceModel()->index(srcRow, 3)).toString();
            if (lastName.isEmpty() && firstName.isEmpty()) return "—";
            if (lastName.isEmpty()) return firstName;
            if (firstName.isEmpty()) return lastName;
            return QString("%1, %2").arg(lastName, firstName);
        } else if (col == 1) { // Household / Family
            return sourceModel()->data(sourceModel()->index(srcRow, 1));
        } else if (col == 2) { // Phone
            return sourceModel()->data(sourceModel()->index(srcRow, 4));
        } else if (col == 3) { // Email
            return sourceModel()->data(sourceModel()->index(srcRow, 5));
        } else if (col == 4) { // Status
            return sourceModel()->data(sourceModel()->index(srcRow, 11));
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
    int col = left.column();
    if (col == 0) { // Sort by Last Name, then First Name
        QModelIndex leftSrc = mapToSource(left);
        QModelIndex rightSrc = mapToSource(right);

        QString leftLast = sourceModel()->data(sourceModel()->index(leftSrc.row(), 3)).toString().toLower();
        QString rightLast = sourceModel()->data(sourceModel()->index(rightSrc.row(), 3)).toString().toLower();

        if (leftLast != rightLast) {
            return leftLast < rightLast;
        }

        QString leftFirst = sourceModel()->data(sourceModel()->index(leftSrc.row(), 2)).toString().toLower();
        QString rightFirst = sourceModel()->data(sourceModel()->index(rightSrc.row(), 2)).toString().toLower();
        return leftFirst < rightFirst;
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

    // ----------------------------------------------------
    // LEFT PANEL: Master Directory Table
    // ----------------------------------------------------
    QWidget* leftPanel = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);

    QHBoxLayout* searchSortRow = new QHBoxLayout();
    searchSortRow->setSpacing(8);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("🔍 Search members by name, email, phone, status...");
    m_searchEdit->setObjectName("searchEdit");
    connect(m_searchEdit, &QLineEdit::textChanged, this, &PeopleWidget::onSearchTextChanged);

    m_cmbSortBy = new QComboBox(this);
    m_cmbSortBy->addItem("🔤 Last Name (A - Z)");
    m_cmbSortBy->addItem("🔤 Last Name (Z - A)");
    m_cmbSortBy->addItem("👤 First Name (A - Z)");
    m_cmbSortBy->addItem("👤 First Name (Z - A)");
    m_cmbSortBy->addItem("🏠 Household / Family (A - Z)");
    m_cmbSortBy->addItem("🏷️ Membership Status");
    m_cmbSortBy->addItem("📅 Join Date (Newest First)");
    m_cmbSortBy->addItem("📅 Join Date (Oldest First)");
    m_cmbSortBy->addItem("🎂 Birth Date / Age");
    m_cmbSortBy->addItem("♂️♀️ Gender");
    connect(m_cmbSortBy, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PeopleWidget::onSortByChanged);

    searchSortRow->addWidget(m_searchEdit, 3);
    QLabel* lblSort = new QLabel("Sort By:", this);
    lblSort->setStyleSheet("font-weight: bold;");
    searchSortRow->addWidget(lblSort);
    searchSortRow->addWidget(m_cmbSortBy, 2);

    leftLayout->addLayout(searchSortRow);

    // Membership-status filter row
    QHBoxLayout* statusFilterRow = new QHBoxLayout();
    statusFilterRow->setSpacing(6);
    m_statusFilterGroup = new QButtonGroup(this);
    m_statusFilterGroup->setExclusive(true);

    static const QHash<int, QString> filters = {
        {0, "All"}, {1, "Visitors"}, {2, "Attenders"},
        {3, "Members"}, {4, "Inactive"}
    };
    for (auto it = filters.constBegin(); it != filters.constEnd(); ++it) {
        QRadioButton* btn = new QRadioButton(it.value(), this);
        btn->setStyleSheet("QRadioButton { color: #cdd6f4; padding: 2px 6px; }"
                           "QRadioButton::indicator { width: 12px; height: 12px; }");
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
    m_peopleTable->verticalHeader()->setDefaultSectionSize(36);
    m_peopleTable->setAlternatingRowColors(true);
    m_peopleTable->setSortingEnabled(true);
    leftLayout->addWidget(m_peopleTable);

    // Action Toolbar Row 1
    QHBoxLayout* row1 = new QHBoxLayout();
    row1->setSpacing(6);
    m_btnAddPerson = new QPushButton("➕ Add Person", this);
    m_btnAddPerson->setObjectName("primaryBtn");
    connect(m_btnAddPerson, &QPushButton::clicked, this, &PeopleWidget::onAddPerson);

    m_btnAddHousehold = new QPushButton("🏠 Add Family", this);
    m_btnAddHousehold->setObjectName("accentBtnPink");
    connect(m_btnAddHousehold, &QPushButton::clicked, this, &PeopleWidget::onAddHousehold);

    m_btnManageHouseholds = new QPushButton(" Purge/Manage Families", this);
    m_btnManageHouseholds->setObjectName("dangerBtn");
    connect(m_btnManageHouseholds, &QPushButton::clicked, this, &PeopleWidget::onDeleteHousehold);

    row1->addWidget(m_btnAddPerson);
    row1->addWidget(m_btnAddHousehold);
    row1->addWidget(m_btnManageHouseholds);

    // Action Toolbar Row 2
    QHBoxLayout* row2 = new QHBoxLayout();
    row2->setSpacing(6);

    m_btnDeleteSelected = new QPushButton("🗑️ Delete Selected", this);
    m_btnDeleteSelected->setObjectName("dangerGhostBtn");
    m_btnDeleteSelected->setStyleSheet(
        "QPushButton#dangerGhostBtn { background-color: transparent; border: 1px solid #f38ba8; color: #f38ba8; border-radius: 6px; padding: 6px 12px; font-weight: bold; }"
        "QPushButton#dangerGhostBtn:hover { background-color: rgba(243, 139, 168, 0.15); }"
    );
    connect(m_btnDeleteSelected, &QPushButton::clicked, this, &PeopleWidget::onDeleteSelectedMembers);

    m_btnBatchStatus = new QPushButton("🏷️ Batch Set Status", this);
    m_btnBatchStatus->setObjectName("secondaryBtn");
    connect(m_btnBatchStatus, &QPushButton::clicked, this, &PeopleWidget::onBatchSetStatus);

    QPushButton* btnCopyRoster = new QPushButton("📋 Roster SMS", this);
    btnCopyRoster->setObjectName("secondaryBtn");
    connect(btnCopyRoster, &QPushButton::clicked, this, [this]() {
        QStringList phoneNumbers;
        for (int r = 0; r < m_proxyModel->rowCount(); ++r) {
            QModelIndex idx = m_proxyModel->index(r, 2); // Phone
            QString phone = idx.data().toString().trimmed();
            if (!phone.isEmpty()) {
                phoneNumbers << phone;
            }
        }
        if (!phoneNumbers.isEmpty()) {
            QGuiApplication::clipboard()->setText(phoneNumbers.join(", "));
            QMessageBox::information(this, "SMS Roster Exported", 
                QString("Copied %1 phone numbers to clipboard, ready to paste in your SMS client.").arg(phoneNumbers.size()));
        } else {
            QMessageBox::warning(this, "Roster Empty", "No valid phone numbers found in current directory view.");
        }
    });

    QPushButton* btnPrintDirectory = new QPushButton("🖨️ Print Directory", this);
    btnPrintDirectory->setObjectName("secondaryBtn");
    connect(btnPrintDirectory, &QPushButton::clicked, this, &PeopleWidget::onPrintDirectory);

    m_btnImportCSV = new QPushButton("📥 Import CSV", this);
    m_btnImportCSV->setObjectName("secondaryBtn");
    connect(m_btnImportCSV, &QPushButton::clicked, this, &PeopleWidget::onImportCSV);

    row2->addWidget(m_btnDeleteSelected);
    row2->addWidget(m_btnBatchStatus);
    row2->addWidget(btnCopyRoster);
    row2->addWidget(btnPrintDirectory);
    row2->addWidget(m_btnImportCSV);

    leftLayout->addLayout(row1);
    leftLayout->addLayout(row2);

    // ----------------------------------------------------
    // RIGHT PANEL: Profile Detail View (5 Structured Section Cards)
    // ----------------------------------------------------
    QWidget* rightPanel = new QWidget(this);
    rightPanel->setObjectName("detailPanel");
    
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(15, 15, 15, 15);
    rightLayout->setSpacing(15);

    QLabel* detailHeader = new QLabel("Person Profile Detail", this);
    detailHeader->setObjectName("detailHeader");
    rightLayout->addWidget(detailHeader);

    // --- CARD 1: IDENTITY SECTION ---
    QFrame* cardIdentity = new QFrame(this);
    cardIdentity->setObjectName("cardWidget");
    QVBoxLayout* layIdent = new QVBoxLayout(cardIdentity);
    layIdent->setContentsMargins(14, 12, 14, 12);
    layIdent->setSpacing(10);

    QLabel* lblTitleIdent = new QLabel("🆔 IDENTITY", cardIdentity);
    lblTitleIdent->setStyleSheet("font-size: 12px; font-weight: bold; color: #89b4fa; text-transform: uppercase;");
    layIdent->addWidget(lblTitleIdent);

    QHBoxLayout* avatarRow = new QHBoxLayout();
    m_avatarLabel = new ClickableAvatarLabel(cardIdentity);
    m_avatarLabel->setFixedSize(100, 100);
    m_avatarLabel->setObjectName("avatarLabel");
    m_avatarLabel->setAlignment(Qt::AlignCenter);
    connect(m_avatarLabel, &ClickableAvatarLabel::clicked, this, &PeopleWidget::onChangeAvatar);

    QFormLayout* formIdent = new QFormLayout();
    formIdent->setSpacing(8);
    formIdent->setLabelAlignment(Qt::AlignRight);

    m_firstNameEdit = new QLineEdit(cardIdentity);
    m_lastNameEdit = new QLineEdit(cardIdentity);
    m_genderCombo = new QComboBox(cardIdentity);
    m_genderCombo->addItems({"Male", "Female", "Unspecified"});
    m_birthDateEdit = new QDateEdit(QDate(1900, 1, 1), cardIdentity);
    m_birthDateEdit->setCalendarPopup(true);
    m_birthDateEdit->setDisplayFormat("yyyy-MM-dd");
    m_birthDateEdit->setMinimumDate(QDate(1900, 1, 1));
    m_birthDateEdit->setSpecialValueText("—");

    formIdent->addRow("First Name:", m_firstNameEdit);
    formIdent->addRow("Last Name:", m_lastNameEdit);
    formIdent->addRow("Gender:", m_genderCombo);
    formIdent->addRow("Birth Date:", m_birthDateEdit);

    avatarRow->addWidget(m_avatarLabel);
    avatarRow->addSpacing(15);
    avatarRow->addLayout(formIdent, 1);
    layIdent->addLayout(avatarRow);

    m_avatarPathEdit = new QLineEdit(cardIdentity);
    m_avatarPathEdit->setVisible(false);

    rightLayout->addWidget(cardIdentity);

    // --- CARD 2: CONTACT SECTION ---
    QFrame* cardContact = new QFrame(this);
    cardContact->setObjectName("cardWidget");
    QVBoxLayout* layContact = new QVBoxLayout(cardContact);
    layContact->setContentsMargins(14, 12, 14, 12);
    layContact->setSpacing(10);

    QLabel* lblTitleContact = new QLabel("📞 CONTACT INFORMATION", cardContact);
    lblTitleContact->setStyleSheet("font-size: 12px; font-weight: bold; color: #89b4fa; text-transform: uppercase;");
    layContact->addWidget(lblTitleContact);

    QFormLayout* formContact = new QFormLayout();
    formContact->setSpacing(8);
    formContact->setLabelAlignment(Qt::AlignRight);

    m_phoneEdit = new QLineEdit(cardContact);
    m_emailEdit = new QLineEdit(cardContact);

    formContact->addRow("Phone Number:", m_phoneEdit);
    formContact->addRow("Email Address:", m_emailEdit);
    layContact->addLayout(formContact);

    rightLayout->addWidget(cardContact);

    // --- CARD 3: HOUSEHOLD & FAMILY SECTION ---
    QFrame* cardHousehold = new QFrame(this);
    cardHousehold->setObjectName("cardWidget");
    QVBoxLayout* layHousehold = new QVBoxLayout(cardHousehold);
    layHousehold->setContentsMargins(14, 12, 14, 12);
    layHousehold->setSpacing(10);

    QLabel* lblTitleHousehold = new QLabel("🏠 HOUSEHOLD & FAMILY", cardHousehold);
    lblTitleHousehold->setStyleSheet("font-size: 12px; font-weight: bold; color: #89b4fa; text-transform: uppercase;");
    layHousehold->addWidget(lblTitleHousehold);

    QFormLayout* formHousehold = new QFormLayout();
    formHousehold->setSpacing(8);
    formHousehold->setLabelAlignment(Qt::AlignRight);

    m_householdCombo = new QComboBox(cardHousehold);
    connect(m_householdCombo, &QComboBox::currentIndexChanged, this, [this]() {
        int hId = m_householdCombo->currentData().toInt();
        QModelIndex current = m_peopleTable->currentIndex();
        int pId = 0;
        if (current.isValid()) {
            QModelIndex srcIdx = m_proxyModel->mapToSource(current);
            pId = m_peopleModel->record(srcIdx.row()).value("id").toInt();
        }
        refreshHouseholdMembersList(hId, pId);
    });
    formHousehold->addRow("Family Assignment:", m_householdCombo);
    layHousehold->addLayout(formHousehold);

    QLabel* lblMembersInHouse = new QLabel("Family Members in Household:", cardHousehold);
    lblMembersInHouse->setStyleSheet("font-size: 11px; font-weight: bold; opacity: 0.8;");
    layHousehold->addWidget(lblMembersInHouse);

    m_lstHouseholdMembers = new QListWidget(cardHousehold);
    m_lstHouseholdMembers->setMaximumHeight(90);
    connect(m_lstHouseholdMembers, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        int pId = item->data(Qt::UserRole).toInt();
        if (pId > 0) selectPerson(pId);
    });
    layHousehold->addWidget(m_lstHouseholdMembers);

    rightLayout->addWidget(cardHousehold);

    // --- CARD 4: MINISTRY PROFILE & TALENTS ---
    QFrame* cardMinistry = new QFrame(this);
    cardMinistry->setObjectName("cardWidget");
    QVBoxLayout* layMin = new QVBoxLayout(cardMinistry);
    layMin->setContentsMargins(14, 12, 14, 12);
    layMin->setSpacing(10);

    QLabel* lblTitleMin = new QLabel("🕊️ MINISTRY PROFILE & TALENTS", cardMinistry);
    lblTitleMin->setStyleSheet("font-size: 12px; font-weight: bold; color: #89b4fa; text-transform: uppercase;");
    layMin->addWidget(lblTitleMin);

    // A. Spiritual Gifts Chips
    QLabel* lblGiftsHeader = new QLabel("Spiritual Gifts (Biblical Taxonomy):", cardMinistry);
    lblGiftsHeader->setStyleSheet("font-size: 11px; font-weight: bold; color: #cdd6f4;");
    layMin->addWidget(lblGiftsHeader);

    m_giftsEdit = new QLineEdit(cardMinistry);
    m_giftsEdit->setVisible(false);

    static const QStringList kSpiritualGifts = {
        "Administration", "Evangelism", "Helps", "Hospitality", "Leadership",
        "Mercy", "Pastoral Care", "Prophecy", "Teaching", "Worship / Music"
    };

    QVBoxLayout* giftsChipBox = new QVBoxLayout();
    giftsChipBox->setSpacing(6);
    QHBoxLayout* giftsRow1 = new QHBoxLayout(); giftsRow1->setSpacing(6);
    QHBoxLayout* giftsRow2 = new QHBoxLayout(); giftsRow2->setSpacing(6);

    for (int i = 0; i < kSpiritualGifts.size(); ++i) {
        QString g = kSpiritualGifts.at(i);
        QPushButton* chip = new QPushButton(g, cardMinistry);
        chip->setCheckable(true);
        chip->setProperty("rawName", g);
        chip->setStyleSheet(
            "QPushButton { background-color: #313244; border: 1px solid #45475a; border-radius: 12px; color: #cdd6f4; padding: 4px 10px; font-size: 11px; font-weight: bold; }"
            "QPushButton:hover { border: 1px solid #89b4fa; color: #89b4fa; }"
            "QPushButton:checked { background-color: rgba(137, 180, 250, 0.25); border: 1px solid #89b4fa; color: #89b4fa; }"
        );
        connect(chip, &QPushButton::toggled, this, [this, chip](bool checked) {
            QString raw = chip->property("rawName").toString();
            chip->setText(checked ? QString("✓ %1").arg(raw) : raw);
            updateGiftsFromChips();
        });
        m_giftChips.append(chip);

        if (i < 5) giftsRow1->addWidget(chip);
        else giftsRow2->addWidget(chip);
    }
    giftsRow1->addStretch();
    giftsRow2->addStretch();
    giftsChipBox->addLayout(giftsRow1);
    giftsChipBox->addLayout(giftsRow2);
    layMin->addLayout(giftsChipBox);

    // B. Relational Style Chips
    QLabel* lblStyleHeader = new QLabel("Relational Style — Team Role Dynamics (Belbin / Ministry Taxonomy):", cardMinistry);
    lblStyleHeader->setStyleSheet("font-size: 11px; font-weight: bold; color: #cdd6f4; margin-top: 6px;");
    layMin->addWidget(lblStyleHeader);

    m_styleEdit = new QLineEdit(cardMinistry);
    m_styleEdit->setVisible(false);

    static const QStringList kRelationalStyles = {
        "Organizer", "Listener", "Doer", "Energizer", "Strategist", "Connector"
    };

    QHBoxLayout* styleRow = new QHBoxLayout();
    styleRow->setSpacing(6);
    for (const QString& s : kRelationalStyles) {
        QPushButton* chip = new QPushButton(s, cardMinistry);
        chip->setCheckable(true);
        chip->setProperty("rawName", s);
        chip->setStyleSheet(
            "QPushButton { background-color: #313244; border: 1px solid #45475a; border-radius: 12px; color: #cdd6f4; padding: 4px 10px; font-size: 11px; font-weight: bold; }"
            "QPushButton:hover { border: 1px solid #a6e3a1; color: #a6e3a1; }"
            "QPushButton:checked { background-color: rgba(166, 227, 161, 0.25); border: 1px solid #a6e3a1; color: #a6e3a1; }"
        );
        connect(chip, &QPushButton::toggled, this, [this, chip](bool checked) {
            QString raw = chip->property("rawName").toString();
            chip->setText(checked ? QString("✓ %1").arg(raw) : raw);
            updateStylesFromChips();
        });
        m_styleChips.append(chip);
        styleRow->addWidget(chip);
    }
    styleRow->addStretch();
    layMin->addLayout(styleRow);

    // C. Interest Tags Widget
    QLabel* lblTagsHeader = new QLabel("Interest Tags:", cardMinistry);
    lblTagsHeader->setStyleSheet("font-size: 11px; font-weight: bold; color: #cdd6f4; margin-top: 6px;");
    layMin->addWidget(lblTagsHeader);

    m_tagsEdit = new QLineEdit(cardMinistry);
    m_tagsEdit->setVisible(false);

    m_tagsChipsContainer = new QWidget(cardMinistry);
    layMin->addWidget(m_tagsChipsContainer);

    QHBoxLayout* tagInputRow = new QHBoxLayout();
    tagInputRow->setSpacing(6);
    m_tagInputField = new QLineEdit(cardMinistry);
    m_tagInputField->setPlaceholderText("Type a tag (e.g. Young Families, Tech/AV) & press Enter...");
    QPushButton* btnAddTag = new QPushButton("+ Add Tag", cardMinistry);
    btnAddTag->setObjectName("secondaryBtn");

    auto triggerAddTag = [this]() {
        addInterestTag(m_tagInputField->text());
    };
    connect(m_tagInputField, &QLineEdit::returnPressed, this, triggerAddTag);
    connect(btnAddTag, &QPushButton::clicked, this, triggerAddTag);

    tagInputRow->addWidget(m_tagInputField, 1);
    tagInputRow->addWidget(btnAddTag);
    layMin->addLayout(tagInputRow);

    rightLayout->addWidget(cardMinistry);

    // --- CARD 5: PASTORAL & ADMIN CARE SECTION ---
    QFrame* cardPastoral = new QFrame(this);
    cardPastoral->setObjectName("cardWidget");
    QVBoxLayout* layPastoral = new QVBoxLayout(cardPastoral);
    layPastoral->setContentsMargins(14, 12, 14, 12);
    layPastoral->setSpacing(10);

    QLabel* lblTitlePastoral = new QLabel("📋 PASTORAL & ADMIN CARE", cardPastoral);
    lblTitlePastoral->setStyleSheet("font-size: 12px; font-weight: bold; color: #89b4fa; text-transform: uppercase;");
    layPastoral->addWidget(lblTitlePastoral);

    QFormLayout* formPastoral = new QFormLayout();
    formPastoral->setSpacing(8);
    formPastoral->setLabelAlignment(Qt::AlignRight);

    m_statusCombo = new QComboBox(cardPastoral);
    m_statusCombo->addItems({"Visitor", "Regular Attender", "Member", "Inactive"});

    QDate nullDate(1900, 1, 1);
    m_firstVisitEdit = new QDateEdit(nullDate, cardPastoral);
    m_firstVisitEdit->setCalendarPopup(true);
    m_firstVisitEdit->setDisplayFormat("yyyy-MM-dd");
    m_firstVisitEdit->setMinimumDate(nullDate);
    m_firstVisitEdit->setSpecialValueText("—");

    m_joinDateEdit = new QDateEdit(nullDate, cardPastoral);
    m_joinDateEdit->setCalendarPopup(true);
    m_joinDateEdit->setDisplayFormat("yyyy-MM-dd");
    m_joinDateEdit->setMinimumDate(nullDate);
    m_joinDateEdit->setSpecialValueText("—");

    m_careFlagCombo = new QComboBox(cardPastoral);
    m_careFlagCombo->addItems({"Normal", "At-Risk", "Follow-up", "Leader"});

    m_adminNotesText = new QTextEdit(cardPastoral);
    m_adminNotesText->setMaximumHeight(70);
    m_adminNotesText->setPlaceholderText("Pastoral notes, counseling logs, or internal comments...");

    formPastoral->addRow("Membership Status:", m_statusCombo);
    formPastoral->addRow("First Visit:", m_firstVisitEdit);
    formPastoral->addRow("Join Date:", m_joinDateEdit);
    formPastoral->addRow("Care Flag:", m_careFlagCombo);
    formPastoral->addRow("Admin Notes:", m_adminNotesText);

    layPastoral->addLayout(formPastoral);
    rightLayout->addWidget(cardPastoral);

    // --- PROFILE ACTIONS & DELETION BAR ---
    QVBoxLayout* actionBox = new QVBoxLayout();
    actionBox->setSpacing(12);

    m_btnSave = new QPushButton("Save Profile Modifications", this);
    m_btnSave->setObjectName("primaryBtn");
    m_btnSave->setFixedHeight(38);
    connect(m_btnSave, &QPushButton::clicked, this, &PeopleWidget::onSaveProfile);
    actionBox->addWidget(m_btnSave);

    QFrame* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #313244;");
    actionBox->addWidget(sep);

    QHBoxLayout* deleteRow = new QHBoxLayout();
    m_btnDeletePerson = new QPushButton("🗑️ Delete Person Profile", this);
    m_btnDeletePerson->setObjectName("ghostDangerBtn");
    m_btnDeletePerson->setStyleSheet(
        "QPushButton#ghostDangerBtn { background: transparent; border: none; color: #f38ba8; font-size: 11px; font-weight: bold; }"
        "QPushButton#ghostDangerBtn:hover { text-decoration: underline; color: #ff5555; }"
    );
    connect(m_btnDeletePerson, &QPushButton::clicked, this, &PeopleWidget::onDeletePerson);
    deleteRow->addStretch();
    deleteRow->addWidget(m_btnDeletePerson);
    deleteRow->addStretch();
    actionBox->addLayout(deleteRow);

    rightLayout->addLayout(actionBox);

    QLabel* connLabel = new QLabel("Active Ministry Connections & Serving Teams", this);
    connLabel->setObjectName("detailSubHeader");
    rightLayout->addWidget(connLabel);

    m_connectionsList = new QListWidget(this);
    m_connectionsList->setObjectName("historyList");
    m_connectionsList->setMaximumHeight(100);
    rightLayout->addWidget(m_connectionsList);

    QHBoxLayout* historyHeaderRow = new QHBoxLayout();
    QLabel* historyLabel = new QLabel("Activity & Pastoral Care Timeline", this);
    historyLabel->setObjectName("detailSubHeader");
    historyHeaderRow->addWidget(historyLabel);
    historyHeaderRow->addStretch();
    m_btnLogCare = new QPushButton("➕ Log Care Entry", this);
    m_btnLogCare->setObjectName("secondaryBtn");
    m_btnLogCare->setFixedHeight(26);
    connect(m_btnLogCare, &QPushButton::clicked, this, &PeopleWidget::onLogCareEntry);
    historyHeaderRow->addWidget(m_btnLogCare);
    rightLayout->addLayout(historyHeaderRow);

    m_historyList = new QListWidget(this);
    m_historyList->setObjectName("historyList");
    m_historyList->setMaximumHeight(110);
    rightLayout->addWidget(m_historyList);

    rightLayout->addStretch();

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setWidget(rightPanel);
    scrollArea->setStyleSheet("QScrollArea { background: transparent; }");

    QSplitter* mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->addWidget(leftPanel);
    mainSplitter->addWidget(scrollArea);
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
    m_peopleModel->select();

    // Setup Custom Proxy Model for merged Name column & searching
    m_proxyModel = new PeopleProxyModel(this);
    m_proxyModel->setSourceModel(m_peopleModel);
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
            "QPushButton { background-color: #313244; color: #89b4fa; border: 1px solid #45475a; border-radius: 12px; padding: 3px 8px; font-size: 11px; font-weight: bold; }"
            "QPushButton:hover { background-color: #f38ba8; color: #11111b; }"
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

void PeopleWidget::onSelectionChanged(const QModelIndex& current, const QModelIndex& previous) {
    Q_UNUSED(previous);
    if (!current.isValid()) return;

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
    {
        QSqlQuery hq;
        hq.prepare("SELECT household_id, gender, first_name, last_name, phone_number, email, "
                   "date_of_birth, avatar_path, care_flag, admin_notes, membership_status, "
                   "first_visit_date, join_date, spiritual_gifts, relational_style, interest_tags "
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

    updateChipsFromGiftsText(giftsText);
    updateChipsFromStyleText(styleText);
    renderTagChips();
    refreshHouseholdMembersList(householdId, personId);
    loadAvatar(avatarPath);

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
}

void PeopleWidget::updateHistoryTimeline(int personId) {
    if (!m_historyList) return;
    m_historyList->clear();

    QSqlQuery qHist;
    qHist.prepare(
        "SELECT timestamp, type, text, follow_up FROM ("
        "  SELECT timestamp, activity_type AS type, details AS text, 0 AS follow_up "
        "  FROM member_history WHERE person_id = ? "
        "  UNION ALL "
        "  SELECT timestamp, care_type AS type, notes AS text, follow_up "
        "  FROM care_logs WHERE person_id = ?"
        ") ORDER BY timestamp DESC LIMIT 15");
    qHist.addBindValue(personId);
    qHist.addBindValue(personId);
    if (qHist.exec()) {
        while (qHist.next()) {
            QString ts = qHist.value(0).toString();
            QString type = qHist.value(1).toString();
            QString text = qHist.value(2).toString();
            bool followUp = qHist.value(3).toInt() != 0;
            QString prefix = followUp ? "🔔 " : "📅 ";
            m_historyList->addItem(QString("%1[%2] %3 — %4").arg(prefix, ts, type, text));
        }
    }
    if (m_historyList->count() == 0) {
        m_historyList->addItem("No pastoral care entries logged.");
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
    q.addBindValue(followUpCheck->isChecked() ? 1 : 0);
    if (!q.exec()) {
        QMessageBox::critical(this, "Database Error", "Failed to save care entry: " + q.lastError().text());
        return;
    }

    updateHistoryTimeline(m_currentPersonId);
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
        case 1: conditions << "membership_status = 'Visitor'"; break;
        case 2: conditions << "membership_status = 'Regular Attender'"; break;
        case 3: conditions << "membership_status = 'Member'"; break;
        case 4: conditions << "membership_status = 'Inactive'"; break;
        default: break;
    }

    m_peopleModel->setFilter(conditions.join(" AND "));
    m_peopleModel->select();
}

void PeopleWidget::refreshModel() {
    populateHouseholdCombo();
    m_householdModel->select();
    m_peopleModel->select();
    refreshTagCompleter();
}

void PeopleWidget::onSortByChanged(int index) {
    switch (index) {
        case 0: m_proxyModel->sort(0, Qt::AscendingOrder); break;
        case 1: m_proxyModel->sort(0, Qt::DescendingOrder); break;
        case 2: m_proxyModel->sort(0, Qt::AscendingOrder); break;
        case 3: m_proxyModel->sort(0, Qt::DescendingOrder); break;
        case 4: m_proxyModel->sort(1, Qt::AscendingOrder); break;
        case 5: m_proxyModel->sort(4, Qt::AscendingOrder); break;
        default: m_proxyModel->sort(0, Qt::AscendingOrder); break;
    }
}

void PeopleWidget::onAddPerson() {
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
    }
}

void PeopleWidget::loadAvatar(const QString& path) {
    if (!path.isEmpty() && QFile::exists(path)) {
        QPixmap pix(path);
        if (!pix.isNull()) {
            QPixmap scaled = pix.scaled(100, 100, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            QPixmap rounded(100, 100);
            rounded.fill(Qt::transparent);
            QPainter p(&rounded);
            p.setRenderHint(QPainter::Antialiasing);
            QPainterPath clipPath;
            clipPath.addRoundedRect(0, 0, 100, 100, 50, 50);
            p.setClipPath(clipPath);
            p.drawPixmap(0, 0, scaled);
            p.end();

            m_avatarLabel->setPixmap(rounded);
            return;
        }
    }

    m_avatarLabel->clear();
    m_avatarLabel->setText("👤");
    const ThemeTokens tok = ThemeManager::instance().currentTokens();
    m_avatarLabel->setStyleSheet(
        QString("border: 2px dashed %1; border-radius: 50px; background-color: %2; font-size: 32px; color: %3;")
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
              "interest_tags = :tags "
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
    q.bindValue(":id", personId);

    if (q.exec()) {
        m_peopleModel->revertAll();
        m_peopleModel->select();
        reapplyFilter();
        selectPerson(personId);
        refreshTagCompleter();
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
    // Remove child records first so foreign-key constraints don't block the
    // person delete (previously the DELETE failed for anyone with
    // contributions, relationships, care logs, attendance, etc.).
    const QStringList childTables = {
        "pastoral_activities", "member_history", "care_logs", "attendance",
        "member_skills", "service_volunteers", "checkout_logs",
        "contributions", "splits"
    };
    for (const QString& t : childTables) {
        q.prepare(QString("DELETE FROM %1 WHERE person_id = ?").arg(t));
        q.addBindValue(personId);
        if (!q.exec()) {
            qWarning() << "deletePersonCascade failed on" << t << ":" << q.lastError().text();
            return false;
        }
    }
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
    QPushButton* btnAdd = new QPushButton("➕ Add Family", &dlg);
    btnAdd->setObjectName("primaryBtn");

    QPushButton* btnEdit = new QPushButton("✏️ Rename / Edit", &dlg);
    btnEdit->setObjectName("accentBtnOrange");

    QPushButton* btnDelete = new QPushButton("🗑️ Purge Record", &dlg);
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
                   "th { background-color: #313244; color: #ffffff; padding: 8px; text-align: left; font-size: 11px; }"
                   "td { padding: 6px; border-bottom: 1px solid #cdd6f4; font-size: 10px; }"
                   "tr:nth-child(even) { background-color: #f2f4f8; }"
                   "</style></head><body>"
                   "<h1>OpenEkklesia — Official Member Directory</h1>"
                   "<p>Exported on: " + QDate::currentDate().toString("yyyy-MM-dd") + "</p>"
                   "<table>"
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
    QStringList headerFields = headerLine.split(",");

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

    int importedCount = 0;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        QStringList cols = line.split(",");

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
        q.prepare("INSERT INTO people (first_name, last_name, phone_number, email, membership_status, household_id) VALUES (?, ?, ?, ?, ?, 1)");
        q.addBindValue(fn);
        q.addBindValue(ln);
        q.addBindValue(phone);
        q.addBindValue(email);
        q.addBindValue(status.isEmpty() ? "Member" : status);
        if (q.exec()) importedCount++;
    }

    db.commit();
    m_peopleModel->select();
    reapplyFilter();
    QMessageBox::information(this, "Success", QString("Successfully imported %1 members!").arg(importedCount));
}

void PeopleWidget::applyDrillThroughFilter(const QString& type, const QString& val) {
    if (type == "status") {
        if (val == "Visitor") m_statusFilterGroup->button(1)->setChecked(true);
        else if (val == "Regular Attender") m_statusFilterGroup->button(2)->setChecked(true);
        else if (val == "Member") m_statusFilterGroup->button(3)->setChecked(true);
        else if (val == "Inactive") m_statusFilterGroup->button(4)->setChecked(true);
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
    } else {
        m_statusFilterGroup->button(0)->setChecked(true);
        m_searchEdit->setText(val);
    }
}
