#include "matchdialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QHeaderView>
#include <QSqlQuery>
#include <QMessageBox>

#include "../database/databasemanager.h"
#include "../core/iconmanager.h"
#include "../core/thememanager.h"

MatchDialog::MatchDialog(int groupId, QWidget* parent)
    : QDialog(parent), m_groupId(groupId)
{
    // Group name + match profile for the summary line and default criteria.
    GroupMatchProfile matchProfile;
    {
        QSqlDatabase db = DatabaseManager::instance().database();
        QSqlQuery q(db);
        q.prepare("SELECT name FROM groups WHERE id = ?");
        q.addBindValue(groupId);
        if (q.exec() && q.next()) m_groupName = q.value(0).toString();

        QSqlQuery c(db);
        c.prepare("SELECT COUNT(*) FROM entity_relationships "
                  "WHERE source_type = 'Person' AND target_type = 'Group' AND target_id = ?");
        c.addBindValue(groupId);
        if (c.exec() && c.next()) m_memberCount = c.value(0).toInt();

        matchProfile = MinistryRecommender::groupMatchProfile(db, groupId);
        if (matchProfile.skillsDeclared) {
            m_groupSkills = matchProfile.requiredSkills;
        } else {
            m_groupSkills = MinistryRecommender::groupSkillProfile(db, groupId, 6);
        }
        m_groupMinAge = matchProfile.minAge;
        m_groupMaxAge = matchProfile.maxAge;
        m_groupAgeBandSet = matchProfile.ageBandSet;
    }
    if (m_groupName.isEmpty()) m_groupName = QString("Group #%1").arg(groupId);

    setupUI();
}

void MatchDialog::setupUI() {
    setWindowTitle(QString("✨ Ministry Match — %1").arg(m_groupName));
    resize(860, 620);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 16, 16, 16);
    lay->setSpacing(10);

    m_lblGroupSummary = new QLabel(this);
    m_lblGroupSummary->setWordWrap(true);
    m_lblGroupSummary->setStyleSheet("font-size: 12px; color: palette(text);");
    lay->addWidget(m_lblGroupSummary);

    // --- Criteria row -----------------------------------------------------
    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setSpacing(8);

    m_spinMinAge = new QSpinBox(this);
    m_spinMinAge->setRange(0, 120);
    m_spinMinAge->setValue(0);
    m_spinMaxAge = new QSpinBox(this);
    m_spinMaxAge->setRange(0, 120);
    m_spinMaxAge->setValue(120);
    // Pre-fill with the group's declared age band when it has one.
    if (m_groupAgeBandSet) {
        m_spinMinAge->setValue(m_groupMinAge);
        m_spinMaxAge->setValue(m_groupMaxAge);
    }
    auto* ageRow = new QHBoxLayout();
    ageRow->addWidget(m_spinMinAge);
    ageRow->addWidget(new QLabel("–", this));
    ageRow->addWidget(m_spinMaxAge);
    ageRow->addStretch();
    form->addRow("Age band:", ageRow);

    m_cmbGender = new QComboBox(this);
    m_cmbGender->addItem("Any", QString());
    m_cmbGender->addItem("Male", QString("Male"));
    m_cmbGender->addItem("Female", QString("Female"));
    form->addRow("Gender:", m_cmbGender);

    m_chkRequireSkill = new QCheckBox("Only people with a matching skill", this);
    m_chkRequireSkill->setChecked(true);
    m_chkRequireSkill->setEnabled(!m_groupSkills.isEmpty());
    form->addRow("Skills:", m_chkRequireSkill);

    m_spinMinAttendance = new QSpinBox(this);
    m_spinMinAttendance->setRange(0, 50);
    m_spinMinAttendance->setValue(0);
    m_spinMinAttendance->setToolTip("Minimum services attended in the last 90 days");
    form->addRow("Min services (90d):", m_spinMinAttendance);

    m_spinMaxGroups = new QSpinBox(this);
    m_spinMaxGroups->setRange(0, 15);
    m_spinMaxGroups->setValue(3);
    m_spinMaxGroups->setToolTip("Burnout guard: skip people already serving in more groups than this");
    form->addRow("Serving limit:", m_spinMaxGroups);

    m_spinMaxResults = new QSpinBox(this);
    m_spinMaxResults->setRange(1, 100);
    m_spinMaxResults->setValue(25);
    form->addRow("Max results:", m_spinMaxResults);

    lay->addLayout(form);

    auto* btnRun = new QPushButton(" Find Suggestions", this);
    btnRun->setIcon(IconManager::instance().getPrimaryIcon("search", 14));
    btnRun->setObjectName("primaryBtn");
    connect(btnRun, &QPushButton::clicked, this, &MatchDialog::runMatch);
    lay->addWidget(btnRun);

    m_lblStatus = new QLabel(this);
    m_lblStatus->setWordWrap(true);
    lay->addWidget(m_lblStatus);

    // --- Results table ----------------------------------------------------
    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({"Add", "Match", "Name", "Age", "Gender", "Why"});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_table->horizontalHeader()->setMinimumSectionSize(60);
    m_table->verticalHeader()->setVisible(false);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_table, 1);

    // --- Actions ----------------------------------------------------------
    auto* btnRow = new QHBoxLayout();
    auto* btnAdd = new QPushButton(" Add Selected to Group", this);
    btnAdd->setIcon(IconManager::instance().getPrimaryIcon("plus", 14));
    btnAdd->setObjectName("primaryBtn");
    connect(btnAdd, &QPushButton::clicked, this, &MatchDialog::addSelected);
    btnRow->addWidget(btnAdd);

    auto* btnProfile = new QPushButton(" View Profile", this);
    btnProfile->setIcon(IconManager::instance().getIcon("user", QColor(), 14));
    btnProfile->setObjectName("secondaryBtn");
    connect(btnProfile, &QPushButton::clicked, this, &MatchDialog::viewProfile);
    btnRow->addWidget(btnProfile);

    btnRow->addStretch();

    auto* btnClose = new QPushButton("Close", this);
    btnClose->setObjectName("secondaryBtn");
    connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(btnClose);
    lay->addLayout(btnRow);

    // Summary + kick off the first search so the dialog opens with results.
    QString needs = m_groupSkills.isEmpty()
        ? "no skills on record (skill filter disabled)"
        : m_groupSkills.join(", ");
    if (m_groupAgeBandSet) {
        m_lblGroupSummary->setText(QString("%1 · %2 member(s) · needs: %3 · preferred age %4–%5")
            .arg(m_groupName).arg(m_memberCount).arg(needs).arg(m_groupMinAge).arg(m_groupMaxAge));
    } else {
        m_lblGroupSummary->setText(QString("%1 · %2 member(s) · needs: %3")
            .arg(m_groupName).arg(m_memberCount).arg(needs));
    }
    runMatch();
}

MatchCriteria MatchDialog::criteria() const {
    MatchCriteria c;
    c.minAge = m_spinMinAge->value();
    c.maxAge = m_spinMaxAge->value();
    c.gender = m_cmbGender->currentData().toString();
    c.requireSkill = m_chkRequireSkill->isChecked();
    c.minAttendance90 = m_spinMinAttendance->value();
    c.maxGroupsServing = m_spinMaxGroups->value();
    c.maxResults = m_spinMaxResults->value();
    return c;
}

void MatchDialog::runMatch() {
    QSqlDatabase db = DatabaseManager::instance().database();
    m_results = MinistryRecommender::recommend(db, m_groupId, criteria());
    populateTable();

    if (m_results.isEmpty()) {
        m_lblStatus->setText("No matches for these criteria — try widening the age band, "
                             "raising the serving limit, or turning off the skill requirement.");
    } else {
        m_lblStatus->setText(QString("Found %1 candidate(s), ranked by match. Tick people and press “Add Selected”.")
                                 .arg(m_results.size()));
    }
}

void MatchDialog::populateTable() {
    const int n = m_results.size();
    m_table->setRowCount(n);
    for (int i = 0; i < n; ++i) {
        const MatchResult& r = m_results[i];
        const bool alreadyAdded = m_addedPersonIds.contains(r.personId);

        auto* chk = new QCheckBox(m_table);
        chk->setChecked(alreadyAdded);
        chk->setEnabled(!alreadyAdded);
        m_table->setCellWidget(i, 0, chk);

        const ThemeTokens tokMatch = ThemeManager::instance().currentTokens();
        QColor scoreColor;
        if (r.score >= 70) scoreColor = tokMatch.accentSuccess;
        else if (r.score >= 40) scoreColor = tokMatch.accentWarning;
        else scoreColor = tokMatch.accentDanger;
        auto* scoreItem = new QTableWidgetItem(QString("%1%").arg(r.score));
        scoreItem->setForeground(scoreColor);
        scoreItem->setTextAlignment(Qt::AlignCenter);
        scoreItem->setData(Qt::UserRole, r.personId);
        m_table->setItem(i, 1, scoreItem);

        m_table->setItem(i, 2, new QTableWidgetItem(alreadyAdded ? r.name + "  ✓" : r.name));
        m_table->setItem(i, 3, new QTableWidgetItem(r.age >= 0 ? QString::number(r.age) : "—"));
        m_table->setItem(i, 4, new QTableWidgetItem(r.gender));

        const QString why = r.reasons.join(" · ");
        auto* whyItem = new QTableWidgetItem(why);
        whyItem->setToolTip(why);
        m_table->setItem(i, 5, whyItem);
    }
}

void MatchDialog::addSelected() {
    QList<int> picked;
    for (int i = 0; i < m_results.size(); ++i) {
        auto* chk = qobject_cast<QCheckBox*>(m_table->cellWidget(i, 0));
        if (chk && chk->isChecked() && !m_addedPersonIds.contains(m_results[i].personId)) {
            picked.append(m_results[i].personId);
        }
    }
    if (picked.isEmpty()) {
        QMessageBox::information(this, "Nothing Selected", "Tick the people you want to add first.");
        return;
    }

    QSqlDatabase db = DatabaseManager::instance().database();
    const QList<int> added = MinistryRecommender::addMembers(db, m_groupId, picked);
    if (!added.isEmpty()) {
        m_addedPersonIds.append(added);
        m_lblStatus->setText(QString("Added %1 person(s) to %2. Roster and graph will refresh when you close this dialog.")
                                 .arg(added.size()).arg(m_groupName));
        populateTable(); // marks them "✓" and disables their checkboxes
    } else {
        m_lblStatus->setText("None added — those people are already Members of this group.");
    }
}

void MatchDialog::viewProfile() {
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_results.size()) {
        QMessageBox::information(this, "No Selection", "Select a person to view their profile.");
        return;
    }
    emit openProfile(m_results[row].personId);
}
