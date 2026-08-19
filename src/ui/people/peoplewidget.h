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

// Custom Proxy Model: Merges First & Last name into "Last Name, First Name"
class PeopleProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit PeopleProxyModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}

    int columnCount(const QModelIndex& parent = QModelIndex()) const override {
        Q_UNUSED(parent);
        return 5; // 0: Name (Last, First), 1: Household, 2: Phone, 3: Email, 4: Status
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
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

private slots:
    void onSearchTextChanged(const QString& text);
    void onSelectionChanged(const QModelIndex& current, const QModelIndex& previous);
    void onAddHousehold();
    void onChangeAvatar();
    void onSaveProfile();
    void onPrintDirectory();
    void onImportCSV();
    void onDeletePerson();
    void onDeleteHousehold();
    void onDeleteSelectedMembers();
    void onBatchSetStatus();
    void onStatusFilterChanged(int id);
    void onSortByChanged(int index);
    void onLogCareEntry();

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

    // Left Panel (Master)
    QLineEdit* m_searchEdit;
    QComboBox* m_cmbSortBy;
    QTableView* m_peopleTable;
    QPushButton* m_btnAddPerson;
    QPushButton* m_btnAddHousehold;
    QPushButton* m_btnManageHouseholds;
    QPushButton* m_btnDeleteSelected;
    QPushButton* m_btnBatchStatus;
    QPushButton* m_btnImportCSV;

    // Status filter row
    QButtonGroup* m_statusFilterGroup;

    // Right Panel (Detail)
    ClickableAvatarLabel* m_avatarLabel;
    QLineEdit* m_firstNameEdit;
    QLineEdit* m_lastNameEdit;
    QComboBox* m_householdCombo;
    QListWidget* m_lstHouseholdMembers;
    QComboBox* m_genderCombo;
    QLineEdit* m_phoneEdit;
    QLineEdit* m_emailEdit;
    QDateEdit* m_birthDateEdit;
    QComboBox* m_statusCombo;
    QDateEdit* m_firstVisitEdit;
    QDateEdit* m_joinDateEdit;
    QComboBox* m_careFlagCombo;
    QTextEdit* m_adminNotesText;

    // Ministry Profile Chips & Tags
    QLineEdit* m_giftsEdit;
    QList<QPushButton*> m_giftChips;
    QLineEdit* m_styleEdit;
    QList<QPushButton*> m_styleChips;
    QLineEdit* m_tagsEdit;
    QWidget* m_tagsChipsContainer;
    QLineEdit* m_tagInputField;
    QCompleter* m_tagCompleter;

    QLineEdit* m_avatarPathEdit;
    QPushButton* m_btnSave;
    QPushButton* m_btnDeletePerson;
    QPushButton* m_btnLogCare;
    QListWidget* m_connectionsList;
    QListWidget* m_historyList;

    int m_currentPersonId = 0;

    // Models & Mapping
    QSqlRelationalTableModel* m_peopleModel;
    QSqlTableModel* m_householdModel;
    PeopleProxyModel* m_proxyModel;
    QDataWidgetMapper* m_mapper;

    QString m_selectedAvatarPath;
    QCache<QString, QPixmap> m_avatarCache;
};

#endif // PEOPLEWIDGET_H
