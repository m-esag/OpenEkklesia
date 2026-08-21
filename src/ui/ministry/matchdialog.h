#ifndef MATCHDIALOG_H
#define MATCHDIALOG_H

#include <QDialog>
#include <QList>
#include "ministrymatch.h"

class QSpinBox;
class QComboBox;
class QCheckBox;
class QTableWidget;
class QLabel;

// "Suggest People" dialog for a single group: adjust criteria, review ranked
// matches with human-readable reasons, bulk-add selected people as Members.
class MatchDialog : public QDialog {
    Q_OBJECT

signals:
    void openProfile(int personId);

public:
    explicit MatchDialog(int groupId, QWidget* parent = nullptr);

    // Person ids actually inserted as Members during this dialog session.
    QList<int> addedPersonIds() const { return m_addedPersonIds; }

private slots:
    void runMatch();
    void addSelected();
    void viewProfile();

private:
    void setupUI();
    MatchCriteria criteria() const;
    void populateTable();

    int m_groupId;
    QString m_groupName;
    QStringList m_groupSkills;
    int m_memberCount = 0;
    QList<MatchResult> m_results;
    QList<int> m_addedPersonIds;

    QSpinBox* m_spinMinAge;
    QSpinBox* m_spinMaxAge;
    QComboBox* m_cmbGender;
    QCheckBox* m_chkRequireSkill;
    QSpinBox* m_spinMinAttendance;
    QSpinBox* m_spinMaxGroups;
    QSpinBox* m_spinMaxResults;
    QTableWidget* m_table;
    QLabel* m_lblStatus;
    QLabel* m_lblGroupSummary;
};

#endif // MATCHDIALOG_H
