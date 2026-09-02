#ifndef PEOPLEWIDGET_H
#define PEOPLEWIDGET_H

#include <QWidget>
#include <QSqlRelationalTableModel>
#include <QSortFilterProxyModel>
#include <QDataWidgetMapper>
#include <QLineEdit>
#include <QTextEdit>
#include <QTableView>
#include <QComboBox>
#include <QDateEdit>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QButtonGroup>
#include <QRadioButton>
#include <QStyledItemDelegate>
#include <QCompleter>
#include <QCache>
#include <QMouseEvent>
#include <QStackedWidget>

// Custom Clickable Avatar Label with Camera Overlay on Hover
class ClickableAvatarLabel : public QLabel {
    Q_OBJECT
public:
    explicit ClickableAvatarLabel(QWidget* parent = nullptr) : QLabel(parent), m_isHovered(false) {
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
    }

signals:
    void clicked();

protected:
    void enterEvent(QEnterEvent* event) override {
        m_isHovered = true;
        update();
        QLabel::enterEvent(event);
    }
    void leaveEvent(QEvent* event) override {
        m_isHovered = false;
        update();
        QLabel::leaveEvent(event);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            emit clicked();
        }
        QLabel::mousePressEvent(event);
    }
    void paintEvent(QPaintEvent* event) override;

private:
    bool m_isHovered;
};

// Status Column Badge Delegate
class StatusItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit StatusItemDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

// Custom Proxy Model: Merges First & Last name into "Last Name, First Name" and handles multi-field sorting
class PeopleProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    enum SortMode {
        NormalColumnSort,
        SortByFirstName,
        SortByJoinDate,
        SortByBirthDate,
        SortByGender
    };

    explicit PeopleProxyModel(QObject* parent = nullptr)
        : QSortFilterProxyModel(parent), m_sortMode(NormalColumnSort) {}

    void setSortMode(SortMode mode) {
        m_sortMode = mode;
        invalidate();
    }

    SortMode sortMode() const { return m_sortMode; }

    int columnCount(const QModelIndex& parent = QModelIndex()) const override {
        Q_UNUSED(parent);
        return 5; // 0: Name (Last, First), 1: Household, 2: Phone, 3: Email, 4: Status
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    SortMode m_sortMode;
};

class PeopleWidget : public QWidget {
    Q_OBJECT

public:
    explicit PeopleWidget(QWidget *parent = nullptr);
    ~PeopleWidget();

    void selectPerson(int personId);
    void applyRole(const QString& role);
    void onAddPerson();
    void applyDrillThroughFilter(const QString& type, const QString& val);
    void refreshModel();

    static QStringList parseCsvLine(const QString& line);
    static QString generateVCard(const QString& firstName, const QString& lastName, const QString& phone,
                                 const QString& email, const QString& bday, const QString& status,
                                 const QString& careFlag, const QString& notes);

private slots:
    void onSearchTextChanged(const QString& text);
    void onSelectionChanged(const QModelIndex& current, const QModelIndex& previous);
    void onAddHousehold();
    void onChangeAvatar();
    void onSaveProfile();
    void onPrintDirectory();
    void onExportVCard();
    void onImportCSV();
    void onDeletePerson();
    void onDeleteHousehold();
    void onDeleteSelectedMembers();
    void onBatchSetStatus();
    void onStatusFilterChanged(int id);
    void onSortByChanged(int index);
    void onLogCareEntry();
    void onOpenPersonDocument(QListWidgetItem* item);
    void onPeopleTableContextMenu(const QPoint& pos);
    void onTimelineContextMenu(const QPoint& pos);

private:
    void setupUI();
    void setupModels();
    void populateHouseholdCombo();
    void loadAvatar(const QString& path);
    void reapplyFilter();

    void updateGiftsFromChips();
    void updateChipsFromGiftsText(const QString& giftsText);
    void updateStylesFromChips();
    void updateChipsFromStyleText(const QString& styleText);
    void renderTagChips();
    void addInterestTag(const QString& tag);
    void removeInterestTag(const QString& tag);
    void refreshHouseholdMembersList(int householdId, int currentPersonId);
    void refreshTagCompleter();
    void updateHistoryTimeline(int personId);
    // Clears a person's child records then deletes the person (FK-safe).
    // Caller owns the transaction. Returns true on success.
    bool deletePersonCascade(QSqlQuery& q, int personId);
    void updateGivingSummary(int personId);
    void updateDocumentsSection(int personId);
    void setDirty(bool dirty);
    void updateHeader(const QString& name, const QString& status, const QString& careFlag, const QString& household, const QString& joinDate);

    // Left Panel (Master)
    QLineEdit* m_searchEdit = nullptr;
    QComboBox* m_cmbSortBy = nullptr;
    QTableView* m_peopleTable = nullptr;
    QPushButton* m_btnAddPerson = nullptr;
    QPushButton* m_btnAddHousehold = nullptr;
    QPushButton* m_btnManageHouseholds = nullptr;
    QPushButton* m_btnDeleteSelected = nullptr;
    QPushButton* m_btnBatchStatus = nullptr;
    QPushButton* m_btnImportCSV = nullptr;

    // Status filter row
    QButtonGroup* m_statusFilterGroup = nullptr;

    // Right Panel (Detail - 360° Dossier)
    QWidget* m_emptyStateWidget = nullptr;
    QWidget* m_dossierContainer = nullptr;

    // Pinned Header
    QWidget* m_headerWidget = nullptr;
    ClickableAvatarLabel* m_avatarLabel = nullptr;
    QLabel* m_lblHeaderName = nullptr;
    QLabel* m_lblHeaderSub = nullptr;
    QLabel* m_lblHeaderStatusBadge = nullptr;
    QLabel* m_lblHeaderCareBadge = nullptr;

    // Navigation Rail & Stack
    QWidget* m_navRail = nullptr;
    QPushButton* m_btnNavIdentity = nullptr;
    QPushButton* m_btnNavPastoral = nullptr;
    QPushButton* m_btnNavGroups = nullptr;
    QPushButton* m_btnNavGiving = nullptr;
    QPushButton* m_btnNavMinistry = nullptr;
    QStackedWidget* m_detailStack = nullptr;

    // Tab 0: Identity & Family
    QLineEdit* m_firstNameEdit = nullptr;
    QLineEdit* m_lastNameEdit = nullptr;
    QComboBox* m_householdCombo = nullptr;
    QListWidget* m_lstHouseholdMembers = nullptr;
    QComboBox* m_genderCombo = nullptr;
    QLineEdit* m_phoneEdit = nullptr;
    QLineEdit* m_emailEdit = nullptr;
    QDateEdit* m_birthDateEdit = nullptr;
    QComboBox* m_statusCombo = nullptr;
    QDateEdit* m_firstVisitEdit = nullptr;
    QDateEdit* m_joinDateEdit = nullptr;

    // Tab 1: Pastoral Care & History
    QComboBox* m_careFlagCombo = nullptr;
    QTextEdit* m_adminNotesText = nullptr;
    QPushButton* m_btnLogCare = nullptr;
    QListWidget* m_historyList = nullptr;

    // Tab 2: Groups & Connections
    QListWidget* m_connectionsList = nullptr;

    // Tab 3: Giving & Stewardship
    QLabel* m_lblGivingYTD = nullptr;
    QLabel* m_lblGivingHousehold = nullptr;
    QLabel* m_lblGivingCount = nullptr;
    QLabel* m_lblGivingLastDate = nullptr;
    QListWidget* m_lstRecentGiving = nullptr;
    QPushButton* m_btnGenerateStatement = nullptr;

    // Tab 0: Linked Documents (vault records tied to this person)
    QListWidget* m_lstPersonDocs = nullptr;

    // Tab 4: Ministry Fit Archetypes, Gifts & Tags
    QLineEdit* m_primaryArchetypeEdit = nullptr;
    QList<QPushButton*> m_archetypeChips;
    QComboBox* m_cmbSecondaryArchetype = nullptr;
    QLineEdit* m_giftsEdit = nullptr;
    QList<QPushButton*> m_giftChips;
    QLineEdit* m_styleEdit = nullptr;
    QList<QPushButton*> m_styleChips;
    QLineEdit* m_tagsEdit = nullptr;
    QWidget* m_tagsChipsContainer = nullptr;
    QList<QPushButton*> m_popularTagChips;
    QLineEdit* m_tagInputField = nullptr;
    QCompleter* m_tagCompleter = nullptr;

    void updateArchetypesFromText(const QString& primary, const QString& secondary);
    void updateArchetypeFromChips();

    // Sticky Action Footer
    QWidget* m_footerFrame = nullptr;
    QLabel* m_lblDirtyIndicator = nullptr;
    QPushButton* m_btnSave = nullptr;
    QPushButton* m_btnDeletePerson = nullptr;
    QLineEdit* m_avatarPathEdit = nullptr;

    int m_currentPersonId = 0;
    bool m_isDirty = false;
    bool m_isReadOnly = false;

    // Models & Mapping
    QSqlRelationalTableModel* m_peopleModel = nullptr;
    QSqlTableModel* m_householdModel = nullptr;
    PeopleProxyModel* m_proxyModel = nullptr;
    QDataWidgetMapper* m_mapper = nullptr;

    QString m_selectedAvatarPath;
    QCache<QString, QPixmap> m_avatarCache;
};

#endif // PEOPLEWIDGET_H
