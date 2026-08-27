#include "pastoralanalyticswidget.h"
#include "../database/databasemanager.h"
#include "../../common/appdateformatter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSqlQuery>
#include <QSqlError>
#include <QInputDialog>
#include <QMessageBox>
#include <QDialog>
#include <QLineEdit>
#include <QFormLayout>
#include <QScrollArea>
#include <QDateTime>
#include <QDate>
#include <QSettings>
#include <QDebug>

#include "thememanager.h"
#include "../core/iconmanager.h"

PastoralAnalyticsWidget::PastoralAnalyticsWidget(QWidget *parent)
    : QWidget(parent)
{
    m_stageNames = {
        "1. First-Time Visitors & Guests",
        "2. Regular Attenders",
        "3. Active Members & Disciples",
        "4. Ministry Leaders & Shepherds"
    };

    setupUI();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &PastoralAnalyticsWidget::applyTheme);
    applyTheme(ThemeManager::instance().currentTokens());

    refreshAnalytics();
}

PastoralAnalyticsWidget::~PastoralAnalyticsWidget() {}

void PastoralAnalyticsWidget::setupUI() {
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(20, 20, 20, 20);
    outerLayout->setSpacing(20);

    // --- Page Header & Controls ---
    QHBoxLayout* headerRow = new QHBoxLayout();
    headerRow->setSpacing(15);

    QVBoxLayout* titleBox = new QVBoxLayout();
    titleBox->setSpacing(4);

    QLabel* header = new QLabel("📊 Pastoral Analytics", this);
    header->setObjectName("pageHeader");

    m_lblSubtitle = new QLabel(QString("Current Snapshot as of %1 • Real-Time Congregation Health Indicators")
                                   .arg(AppDateFormatter::formatDate(QDate::currentDate())), this);
    m_lblSubtitle->setStyleSheet("color: palette(placeholderText); font-size: 12px; font-weight: bold;");

    titleBox->addWidget(header);
    titleBox->addWidget(m_lblSubtitle);

    headerRow->addLayout(titleBox, 1);

    // Controls: Scope Selector & Stage Configuration
    m_cmbScopeSelector = new QComboBox(this);
    {
        const QDate now = QDate::currentDate();
        const int quarter = (now.month() - 1) / 3 + 1;
        m_cmbScopeSelector->addItem("📅 Current Snapshot (Real-time)");
        m_cmbScopeSelector->addItem(QString("📅 Q%1 %2 Snapshot").arg(quarter).arg(now.year()));
        m_cmbScopeSelector->addItem(QString("📅 Year-to-Date %1").arg(now.year()));
        m_cmbScopeSelector->addItem("📅 Trailing 12 Months");
    }
    connect(m_cmbScopeSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PastoralAnalyticsWidget::onScopeChanged);

    m_btnConfigureStages = new QPushButton(" Configure Stages", this);
    m_btnConfigureStages->setIcon(IconManager::instance().getIcon("settings", QColor(), 14)); // theme-aware
    m_btnConfigureStages->setObjectName("secondaryBtn");
    connect(m_btnConfigureStages, &QPushButton::clicked, this, &PastoralAnalyticsWidget::onConfigureStagesClicked);

    headerRow->addWidget(m_cmbScopeSelector);
    headerRow->addWidget(m_btnConfigureStages);

    outerLayout->addLayout(headerRow);

    QWidget* scrollContent = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(20);

    // ----------------------------------------------------
    // 1. Top-Level KPI Metric Cards (Grouped in Clean Cards with Trends & Drill-Through)
    // ----------------------------------------------------
    QGridLayout* grid = new QGridLayout();
    grid->setSpacing(15);

    // Card 1: New Visitors
    ClickableCardFrame* card1 = new ClickableCardFrame(this);
    card1->setObjectName("card");
    QVBoxLayout* l1 = new QVBoxLayout(card1);
    l1->setContentsMargins(14, 12, 14, 12);
    l1->setSpacing(4);
    m_lblTitle1 = new QLabel("NEW VISITORS THIS MONTH", card1);
    m_lblVisitorsVal = new QLabel("0", card1);
    m_lblVisitorsTrend = new QLabel("↑ +2 vs. last month  •  Click to view roster", card1);
    l1->addWidget(m_lblTitle1);
    l1->addWidget(m_lblVisitorsVal);
    l1->addWidget(m_lblVisitorsTrend);
    card1->onClick = [this]() {
        emit drillThroughRequested("status", "Visitor");
    };
    grid->addWidget(card1, 0, 0);

    // Card 2: Small Group Coverage
    ClickableCardFrame* card2 = new ClickableCardFrame(this);
    card2->setObjectName("card");
    QVBoxLayout* l2 = new QVBoxLayout(card2);
    l2->setContentsMargins(14, 12, 14, 12);
    l2->setSpacing(4);
    m_lblTitle2 = new QLabel("SMALL GROUP COVERAGE", card2);
    m_lblGroupCoverageVal = new QLabel("0%", card2);
    m_lblGroupInverseVal = new QLabel("⚠️ 59% unconnected to a small group (Pastoral Concern)", card2);
    l2->addWidget(m_lblTitle2);
    l2->addWidget(m_lblGroupCoverageVal);
    l2->addWidget(m_lblGroupInverseVal);
    card2->onClick = [this]() {
        emit drillThroughRequested("unconnected", "");
    };
    grid->addWidget(card2, 0, 1);

    // Card 3: At-Risk Members
    ClickableCardFrame* card3 = new ClickableCardFrame(this);
    card3->setObjectName("card");
    QVBoxLayout* l3 = new QVBoxLayout(card3);
    l3->setContentsMargins(14, 12, 14, 12);
    l3->setSpacing(4);
    m_lblTitle3 = new QLabel("AT-RISK & LAPSED MEMBERS", card3);
    m_lblAtRiskVal = new QLabel("0", card3);
    m_lblAtRiskTrend = new QLabel("↓ -1 vs. last month  •  Click to review profiles", card3);
    l3->addWidget(m_lblTitle3);
    l3->addWidget(m_lblAtRiskVal);
    l3->addWidget(m_lblAtRiskTrend);
    card3->onClick = [this]() {
        emit drillThroughRequested("care_flag", "At-Risk");
    };
    grid->addWidget(card3, 0, 2);

    // Card 4: Visitor Conversion Rate
    ClickableCardFrame* card4 = new ClickableCardFrame(this);
    card4->setObjectName("card");
    QVBoxLayout* l4 = new QVBoxLayout(card4);
    l4->setContentsMargins(14, 12, 14, 12);
    l4->setSpacing(4);
    m_lblTitle4 = new QLabel("VISITOR CONVERSION RATE", card4);
    m_lblConversionVal = new QLabel("0%", card4);
    m_lblConversionTrend = new QLabel("↑ +4% vs. YTD  •  Visitors progressing to member", card4);
    l4->addWidget(m_lblTitle4);
    l4->addWidget(m_lblConversionVal);
    l4->addWidget(m_lblConversionTrend);
    card4->onClick = [this]() {
        emit drillThroughRequested("status", "Member");
    };
    grid->addWidget(card4, 1, 0);

    // Card 5: Serving Team Engagement
    ClickableCardFrame* card5 = new ClickableCardFrame(this);
    card5->setObjectName("card");
    QVBoxLayout* l5 = new QVBoxLayout(card5);
    l5->setContentsMargins(14, 12, 14, 12);
    l5->setSpacing(4);
    m_lblTitle5 = new QLabel("ACTIVE SERVING TEAMS", card5);
    m_lblServingVal = new QLabel("0%", card5);
    m_lblServingTrend = new QLabel("↑ +5% vs. last quarter  •  Click to view volunteers", card5);
    l5->addWidget(m_lblTitle5);
    l5->addWidget(m_lblServingVal);
    l5->addWidget(m_lblServingTrend);
    card5->onClick = [this]() {
        emit drillThroughRequested("status", "Member");
    };
    grid->addWidget(card5, 1, 1);

    mainLayout->addLayout(grid);

    // ----------------------------------------------------
    // 2. Visual Discipleship Pathway Funnel Visualization
    // ----------------------------------------------------
    QFrame* funnelCard = new QFrame(this);
    funnelCard->setObjectName("card");
    QVBoxLayout* funnelLayout = new QVBoxLayout(funnelCard);
    funnelLayout->setContentsMargins(20, 16, 20, 16);
    funnelLayout->setSpacing(12);

    m_lblFunnelTitle = new QLabel("🕊️ Discipleship Pathway Funnel (Progression & Drop-off)", this);
    m_lblFunnelTitle->setObjectName("detailHeader");
    funnelLayout->addWidget(m_lblFunnelTitle);

    // Stage 1 Card: Cool Blue
    ClickableCardFrame* c1 = new ClickableCardFrame(this);
    m_cardStage1 = c1;
    QHBoxLayout* lay1 = new QHBoxLayout(m_cardStage1);
    m_lblStage1Title = new QLabel(m_stageNames.at(0), m_cardStage1);
    m_lblStage1Val = new QLabel("0 people (0%)", m_cardStage1);
    lay1->addWidget(m_lblStage1Title);
    lay1->addStretch();
    lay1->addWidget(m_lblStage1Val);
    c1->onClick = [this]() {
        emit drillThroughRequested("status", "Visitor");
    };
    funnelLayout->addWidget(m_cardStage1);

    // Conversion Arrow 1 -> 2
    m_lblConv1To2 = new QLabel("⬇️ 74% conversion to Regular Attenders  •  26% drop-off", this);
    m_lblConv1To2->setAlignment(Qt::AlignCenter);
    funnelLayout->addWidget(m_lblConv1To2);

    // Stage 2 Card: Teal / Cyan
    ClickableCardFrame* c2 = new ClickableCardFrame(this);
    m_cardStage2 = c2;
    QHBoxLayout* lay2 = new QHBoxLayout(m_cardStage2);
    m_lblStage2Title = new QLabel(m_stageNames.at(1), m_cardStage2);
    m_lblStage2Val = new QLabel("0 people (0%)", m_cardStage2);
    lay2->addWidget(m_lblStage2Title);
    lay2->addStretch();
    lay2->addWidget(m_lblStage2Val);
    c2->onClick = [this]() {
        emit drillThroughRequested("status", "Regular Attender");
    };
    funnelLayout->addWidget(m_cardStage2);

    // Conversion Arrow 2 -> 3
    m_lblConv2To3 = new QLabel("⬇️ 81% conversion to Formal Members  •  19% drop-off", this);
    m_lblConv2To3->setAlignment(Qt::AlignCenter);
    funnelLayout->addWidget(m_lblConv2To3);

    // Stage 3 Card: Emerald Green
    ClickableCardFrame* c3 = new ClickableCardFrame(this);
    m_cardStage3 = c3;
    QHBoxLayout* lay3 = new QHBoxLayout(m_cardStage3);
    m_lblStage3Title = new QLabel(m_stageNames.at(2), m_cardStage3);
    m_lblStage3Val = new QLabel("0 people (0%)", m_cardStage3);
    lay3->addWidget(m_lblStage3Title);
    lay3->addStretch();
    lay3->addWidget(m_lblStage3Val);
    c3->onClick = [this]() {
        emit drillThroughRequested("status", "Member");
    };
    funnelLayout->addWidget(m_cardStage3);

    // Conversion Arrow 3 -> 4
    m_lblConv3To4 = new QLabel("⬇️ 65% conversion to Serving Leaders  •  35% drop-off", this);
    m_lblConv3To4->setAlignment(Qt::AlignCenter);
    funnelLayout->addWidget(m_lblConv3To4);

    // Stage 4 Card: Warm Gold
    ClickableCardFrame* c4 = new ClickableCardFrame(this);
    m_cardStage4 = c4;
    QHBoxLayout* lay4 = new QHBoxLayout(m_cardStage4);
    m_lblStage4Title = new QLabel(m_stageNames.at(3), m_cardStage4);
    m_lblStage4Val = new QLabel("0 people (0%)", m_cardStage4);
    lay4->addWidget(m_lblStage4Title);
    lay4->addStretch();
    lay4->addWidget(m_lblStage4Val);
    c4->onClick = [this]() {
        emit drillThroughRequested("status", "Member");
    };
    funnelLayout->addWidget(m_cardStage4);

    mainLayout->addWidget(funnelCard);

    // ----------------------------------------------------
    // 3. "Pastoral Actions Needed" Section (Interactive Recommendations)
    // ----------------------------------------------------
    QFrame* actionCard = new QFrame(this);
    actionCard->setObjectName("card");
    QVBoxLayout* actLayout = new QVBoxLayout(actionCard);
    actLayout->setContentsMargins(20, 16, 20, 16);
    actLayout->setSpacing(12);

    m_lblPastoralActionsTitle = new QLabel("Pastoral Actions & Shepherd Recommendations", this);
    m_lblPastoralActionsTitle->setObjectName("detailHeader");
    actLayout->addWidget(m_lblPastoralActionsTitle);

    m_lstPastoralActions = new QListWidget(this);
    m_lstPastoralActions->setMaximumHeight(140);
    m_lstPastoralActions->setStyleSheet("QListWidget { background: transparent; border: none; font-size: 12px; }"
                                         "QListWidget::item { padding: 8px; border-bottom: 1px solid palette(mid); color: palette(text); font-weight: bold; }"
                                         "QListWidget::item:hover { background-color: palette(mid); color: palette(highlight); border-radius: 6px; }");

    connect(m_lstPastoralActions, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        QString type = item->data(Qt::UserRole).toString();
        QString val = item->data(Qt::UserRole + 1).toString();
        emit drillThroughRequested(type, val);
    });

    actLayout->addWidget(m_lstPastoralActions);
    mainLayout->addWidget(actionCard);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(scrollContent);
    outerLayout->addWidget(scroll);
}

void PastoralAnalyticsWidget::onScopeChanged(int index) {
    Q_UNUSED(index);
    QString text = m_cmbScopeSelector->currentText();
    m_lblSubtitle->setText(QString("Snapshot: %1 • Real-Time Congregation Health Indicators").arg(text));
    refreshAnalytics();
}

void PastoralAnalyticsWidget::onConfigureStagesClicked() {
    QDialog dlg(this);
    dlg.setWindowTitle("Configure Discipleship Pipeline Stages");
    dlg.resize(480, 300);
    QVBoxLayout* lay = new QVBoxLayout(&dlg);

    QLabel* info = new QLabel("Customize the discipleship pathway stage names for your church (e.g. CLASS 101/201/301/401 or custom pipeline):", &dlg);
    info->setWordWrap(true);
    lay->addWidget(info);

    QFormLayout* form = new QFormLayout();
    QLineEdit* edit1 = new QLineEdit(m_stageNames.value(0), &dlg);
    QLineEdit* edit2 = new QLineEdit(m_stageNames.value(1), &dlg);
    QLineEdit* edit3 = new QLineEdit(m_stageNames.value(2), &dlg);
    QLineEdit* edit4 = new QLineEdit(m_stageNames.value(3), &dlg);

    form->addRow("Stage 1 (Visitors):", edit1);
    form->addRow("Stage 2 (Attenders):", edit2);
    form->addRow("Stage 3 (Members):", edit3);
    form->addRow("Stage 4 (Leaders):", edit4);
    lay->addLayout(form);

    QHBoxLayout* btnRow = new QHBoxLayout();
    QPushButton* btnSave = new QPushButton("Save Pipeline Configuration", &dlg);
    btnSave->setObjectName("primaryBtn");
    QPushButton* btnCancel = new QPushButton("Cancel", &dlg);
    btnCancel->setObjectName("secondaryBtn");
    btnRow->addStretch();
    btnRow->addWidget(btnCancel);
    btnRow->addWidget(btnSave);
    lay->addLayout(btnRow);

    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnSave, &QPushButton::clicked, &dlg, [this, &dlg, edit1, edit2, edit3, edit4]() {
        m_stageNames[0] = edit1->text().trimmed();
        m_stageNames[1] = edit2->text().trimmed();
        m_stageNames[2] = edit3->text().trimmed();
        m_stageNames[3] = edit4->text().trimmed();

        m_lblStage1Title->setText(m_stageNames.at(0));
        m_lblStage2Title->setText(m_stageNames.at(1));
        m_lblStage3Title->setText(m_stageNames.at(2));
        m_lblStage4Title->setText(m_stageNames.at(3));

        dlg.accept();
    });

    dlg.exec();
}

PastoralAnalyticsWidget::AnalyticsSnapshot PastoralAnalyticsWidget::computeSnapshot(int scopeIndex) const {
    AnalyticsSnapshot s;
    s.snapshotDate = QDate::currentDate();
    QSqlDatabase db = DatabaseManager::instance().database();

    // Scope window. Semantics: "people who entered the pipeline (joined or
    // first visited) inside the selected window". Index 0 = all time.
    QString windowClausePeople;   // applied to people rows
    QString windowClauseJoined;   // applied to entity_relationships.joined_date
    QString windowClauseFirstVisit; // applied to people.first_visit_date only
    {
        QString startS, endS;
        if (scopeIndex == 1) {    // current calendar quarter
            const int q = (s.snapshotDate.month() - 1) / 3 + 1;
            const QDate start(s.snapshotDate.year(), (q - 1) * 3 + 1, 1);
            startS = start.toString("yyyy-MM-dd");
            endS = start.addMonths(3).addDays(-1).toString("yyyy-MM-dd");
        } else if (scopeIndex == 2) { // year to date
            startS = QDate(s.snapshotDate.year(), 1, 1).toString("yyyy-MM-dd");
            endS = s.snapshotDate.toString("yyyy-MM-dd");
        } else if (scopeIndex == 3) { // trailing 12 months
            startS = s.snapshotDate.addMonths(-12).toString("yyyy-MM-dd");
            endS = s.snapshotDate.toString("yyyy-MM-dd");
        }
        if (!startS.isEmpty()) {
            windowClausePeople = QString(" AND ((join_date BETWEEN '%1' AND '%2') OR (first_visit_date BETWEEN '%1' AND '%2'))").arg(startS, endS);
            windowClauseJoined = QString(" AND joined_date BETWEEN '%1' AND '%2'").arg(startS, endS);
            windowClauseFirstVisit = QString(" AND first_visit_date BETWEEN '%1' AND '%2'").arg(startS, endS);
        } else if (scopeIndex == 0) {
            // Live snapshot: "new visitors" means first visit within this month.
            const QDate mStart(s.snapshotDate.year(), s.snapshotDate.month(), 1);
            windowClauseFirstVisit = QString(" AND first_visit_date BETWEEN '%1' AND '%2'")
                                         .arg(mStart.toString("yyyy-MM-dd"), s.snapshotDate.toString("yyyy-MM-dd"));
        }
    }

    QSqlQuery q(db);
    auto countPeople = [&q, &windowClausePeople](const QString& extra) {
        if (!q.exec(QString("SELECT COUNT(*) FROM people WHERE 1=1%1%2").arg(windowClausePeople, extra))) return 0;
        return q.next() ? q.value(0).toInt() : 0;
    };

    s.totalPeople = countPeople(""); // raw count; percentages guard against 0
    s.visitors  = countPeople(" AND membership_status = 'Visitor'");
    s.attenders = countPeople(" AND membership_status = 'Regular Attender'");
    s.members   = countPeople(" AND membership_status = 'Member'");
    s.atRisk    = countPeople(" AND care_flag IN ('At-Risk', 'Needs Contact')");

    // Leaders: consistent with the rest of the app (Leader/Co-Leader/Teacher).
    if (q.exec(QString("SELECT COUNT(DISTINCT source_id) FROM entity_relationships "
                       "WHERE source_type = 'Person' AND target_type = 'Group' "
                       "AND relationship_type IN ('Leader', 'Co-Leader', 'Teacher')%1").arg(windowClauseJoined)))
        if (q.next()) s.leaders = q.value(0).toInt();

    // Small-group coverage + serving: only live (non-archived) groups count.
    const QString groupJoin = "entity_relationships er JOIN groups g ON er.target_id = g.id "
                              "WHERE er.source_type = 'Person' AND er.target_type = 'Group' AND g.is_archived = 0";
    if (q.exec(QString("SELECT COUNT(DISTINCT er.source_id) FROM %1%2").arg(groupJoin, windowClauseJoined)))
        if (q.next()) s.smallGroupConnected = q.value(0).toInt();
    if (q.exec(QString("SELECT COUNT(DISTINCT er.source_id), COUNT(DISTINCT er.target_id) FROM %1%2").arg(groupJoin, windowClauseJoined)))
        if (q.next()) { s.servingCount = q.value(0).toInt(); s.servingTeams = q.value(1).toInt(); }

    // New visitors in the scope window (this month for the live snapshot),
    // plus the previous calendar month for the trend.
    if (q.exec(QString("SELECT COUNT(*) FROM people WHERE first_visit_date IS NOT NULL AND first_visit_date != ''%1").arg(windowClauseFirstVisit)))
        if (q.next()) s.newVisitorsInScope = q.value(0).toInt();
    {
        const QDate prevStart = QDate(s.snapshotDate.year(), s.snapshotDate.month(), 1).addMonths(-1);
        const QDate prevEnd = prevStart.addMonths(1).addDays(-1);
        QSqlQuery qPrev(db);
        qPrev.prepare("SELECT COUNT(*) FROM people WHERE first_visit_date IS NOT NULL AND first_visit_date BETWEEN ? AND ?");
        qPrev.addBindValue(prevStart.toString("yyyy-MM-dd"));
        qPrev.addBindValue(prevEnd.toString("yyyy-MM-dd"));
        if (qPrev.exec() && qPrev.next()) s.newVisitorsLastMonth = qPrev.value(0).toInt();
    }

    // Overdue follow-ups: current workload, not scope-filtered.
    if (q.exec("SELECT COUNT(*) FROM pastoral_activities WHERE follow_up_date IS NOT NULL "
               "AND follow_up_date <= date('now', 'localtime') AND follow_up_done = 0"))
        if (q.next()) s.overdueFollowUps = q.value(0).toInt();

    // Lapsed givers: Members/Attenders with a giving history but nothing since
    // the configured inactivity window (same rule as the dashboard).
    QSettings set("OpenEkklesia", "CHMS");
    const int inactivityDays = qBound(7, set.value("inactivityDays", 30).toInt(), 365);
    QSqlQuery qLapsed(db);
    qLapsed.prepare(QString(
        "SELECT COUNT(*) FROM people "
        "WHERE membership_status IN ('Member', 'Regular Attender') "
        "  AND id IN (SELECT DISTINCT person_id FROM contributions) "
        "  AND id NOT IN (SELECT DISTINCT person_id FROM contributions WHERE contribution_date >= date('now', '-%1 days'))")
        .arg(inactivityDays));
    if (qLapsed.exec() && qLapsed.next()) s.lapsedGivers = qLapsed.value(0).toInt();

    const int active = s.visitors + s.attenders + s.members;
    const int denom = qMax(1, s.totalPeople);
    s.groupPct = qMin(100, (s.smallGroupConnected * 100) / denom);
    s.conversionPct = qMin(100, (s.members * 100) / qMax(1, active));
    s.servingPct = qMin(100, (s.servingCount * 100) / denom);
    return s;
}

void PastoralAnalyticsWidget::refreshAnalytics() {
    const int scopeIdx = m_cmbScopeSelector ? m_cmbScopeSelector->currentIndex() : 0;
    const AnalyticsSnapshot s = computeSnapshot(scopeIdx);

    // Top metric cards
    m_lblVisitorsVal->setText(QString::number(s.newVisitorsInScope));
    const int vDelta = s.newVisitorsInScope - s.newVisitorsLastMonth;
    m_lblVisitorsTrend->setText(vDelta > 0 ? QString("↑ +%1 vs. last month  •  Click to view roster").arg(vDelta)
                                : vDelta < 0 ? QString("↓ %1 vs. last month  •  Click to view roster").arg(vDelta)
                                : "— same as last month  •  Click to view roster");

    m_lblAtRiskVal->setText(QString::number(s.atRisk));
    m_lblAtRiskTrend->setText(QString("%1 follow-up%2 overdue in the Shepherd queue  •  Click to review")
                                  .arg(s.overdueFollowUps).arg(s.overdueFollowUps == 1 ? "" : "s"));

    m_lblGroupCoverageVal->setText(QString("%1%").arg(s.groupPct));
    m_lblGroupInverseVal->setText(QString("⚠️ %1% unconnected to a small group (%2 members) (Pastoral Concern)")
                                      .arg(100 - s.groupPct).arg(s.totalPeople - s.smallGroupConnected));

    m_lblConversionVal->setText(QString("%1%").arg(s.conversionPct));
    m_lblConversionTrend->setText(QString("%1 of %2 active are formal Members  •  Visitors progressing to member")
                                      .arg(s.members).arg(qMax(1, s.visitors + s.attenders + s.members)));

    m_lblServingVal->setText(QString("%1%").arg(s.servingPct));
    m_lblServingTrend->setText(QString("%1 volunteer%2 across %3 team%4  •  Click to view volunteers")
                                   .arg(s.servingCount).arg(s.servingCount == 1 ? "" : "s")
                                   .arg(s.servingTeams).arg(s.servingTeams == 1 ? "" : "s"));

    // Funnel stages (denominator guarded — snapshot carries raw counts)
    const int denom = qMax(1, s.totalPeople);
    m_lblStage1Val->setText(QString("%1 people (%2%)").arg(s.visitors).arg((s.visitors * 100) / denom));
    m_lblStage2Val->setText(QString("%1 people (%2%)").arg(s.attenders).arg((s.attenders * 100) / denom));
    m_lblStage3Val->setText(QString("%1 people (%2%)").arg(s.members).arg((s.members * 100) / denom));
    m_lblStage4Val->setText(QString("%1 people (%2%)").arg(s.leaders).arg((s.leaders * 100) / denom));

    const int conv1 = s.visitors > 0 ? qMin(100, (s.attenders * 100) / s.visitors) : 0;
    const int conv2 = s.attenders > 0 ? qMin(100, (s.members * 100) / s.attenders) : 0;
    const int conv3 = s.members > 0 ? qMin(100, (s.leaders * 100) / s.members) : 0;
    m_lblConv1To2->setText(QString("⬇️ %1% conversion to Regular Attenders  •  %2% drop-off").arg(conv1).arg(100 - conv1));
    m_lblConv2To3->setText(QString("⬇️ %1% conversion to Formal Members  •  %2% drop-off").arg(conv2).arg(100 - conv2));
    m_lblConv3To4->setText(QString("⬇️ %1% conversion to Ministry Leaders  •  %2% drop-off").arg(conv3).arg(100 - conv3));

    // Pastoral actions (every entry backed by a real count)
    m_lstPastoralActions->clear();
    auto addAction = [this](const QString& text, const QString& type, const QString& val) {
        QListWidgetItem* item = new QListWidgetItem(text, m_lstPastoralActions);
        item->setData(Qt::UserRole, type);
        item->setData(Qt::UserRole + 1, val);
    };

    if (s.atRisk > 0)
        addAction(QString("⚠️ %1 member%2 flagged At-Risk or needing contact (click to review)").arg(s.atRisk).arg(s.atRisk == 1 ? "" : "s"),
                  "care_flag", "At-Risk");
    if (s.overdueFollowUps > 0)
        addAction(QString("🕊️ %1 follow-up%2 overdue — work the Shepherd's queue").arg(s.overdueFollowUps).arg(s.overdueFollowUps == 1 ? "" : "s"),
                  "care_flag", "Follow-up");
    if (100 - s.groupPct > 0)
        addAction(QString("👥 %1% of congregation unconnected to a small group (%2 members)").arg(100 - s.groupPct).arg(s.totalPeople - s.smallGroupConnected),
                  "unconnected", "");
    if (s.newVisitorsInScope > 0)
        addAction(QString("🤝 %1 new visitor%2 awaiting welcome-team follow-up").arg(s.newVisitorsInScope).arg(s.newVisitorsInScope == 1 ? "" : "s"),
                  "status", "Visitor");
    if (s.lapsedGivers > 0)
        addAction(QString("💸 %1 lapsed giver%2 (no contribution in 30+ days) — click to view").arg(s.lapsedGivers).arg(s.lapsedGivers == 1 ? "" : "s"),
                  "lapsed_giver", "");
}

void PastoralAnalyticsWidget::applyTheme(const ThemeTokens& tokens) {
    m_lblSubtitle->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold;").arg(tokens.textSecondary.name()));

    bool dark = tokens.isDark;

    QString colBlue = dark ? "#89b4fa" : "#1d4ed8";
    QString colCyan = dark ? "#89dceb" : "#0891b2";
    QString colGreen = dark ? "#a6e3a1" : "#15803d";
    QString colAmber = dark ? "#f9e2af" : "#b45309";
    QString colOrange = dark ? "#fab387" : "#c2410c";
    QString colRed = dark ? "#f38ba8" : "#be123c";

    m_lblTitle1->setStyleSheet(QString("font-size: 11px; font-weight: bold; color: %1; text-transform: uppercase;").arg(colBlue));
    m_lblVisitorsVal->setStyleSheet(QString("font-size: 26px; font-weight: bold; color: %1;").arg(tokens.textPrimary.name()));
    m_lblVisitorsTrend->setStyleSheet(QString("font-size: 11px; font-weight: bold; color: %1;").arg(colGreen));

    m_lblTitle2->setStyleSheet(QString("font-size: 11px; font-weight: bold; color: %1; text-transform: uppercase;").arg(colBlue));
    m_lblGroupCoverageVal->setStyleSheet(QString("font-size: 26px; font-weight: bold; color: %1;").arg(tokens.textPrimary.name()));
    m_lblGroupInverseVal->setStyleSheet(QString("font-size: 11px; font-weight: bold; color: %1;").arg(colOrange));

    m_lblTitle3->setStyleSheet(QString("font-size: 11px; font-weight: bold; color: %1; text-transform: uppercase;").arg(colRed));
    m_lblAtRiskVal->setStyleSheet(QString("font-size: 26px; font-weight: bold; color: %1;").arg(colRed));
    m_lblAtRiskTrend->setStyleSheet(QString("font-size: 11px; font-weight: bold; color: %1;").arg(colAmber));

    m_lblTitle4->setStyleSheet(QString("font-size: 11px; font-weight: bold; color: %1; text-transform: uppercase;").arg(colBlue));
    m_lblConversionVal->setStyleSheet(QString("font-size: 26px; font-weight: bold; color: %1;").arg(tokens.textPrimary.name()));
    m_lblConversionTrend->setStyleSheet(QString("font-size: 11px; font-weight: bold; color: %1;").arg(colGreen));

    m_lblTitle5->setStyleSheet(QString("font-size: 11px; font-weight: bold; color: %1; text-transform: uppercase;").arg(colBlue));
    m_lblServingVal->setStyleSheet(QString("font-size: 26px; font-weight: bold; color: %1;").arg(tokens.textPrimary.name()));
    m_lblServingTrend->setStyleSheet(QString("font-size: 11px; font-weight: bold; color: %1;").arg(colGreen));

    m_cardStage1->setStyleSheet(QString("QFrame { background-color: %1; border: 1px solid %2; border-radius: 8px; padding: 10px; }")
                                    .arg(dark ? "rgba(137, 180, 250, 0.15)" : "rgba(29, 78, 216, 0.08)", colBlue));
    m_lblStage1Title->setStyleSheet(QString("font-size: 13px; font-weight: bold; color: %1;").arg(colBlue));
    m_lblStage1Val->setStyleSheet(QString("font-size: 13px; font-weight: bold; color: %1;").arg(tokens.textPrimary.name()));

    m_lblConv1To2->setStyleSheet(QString("font-size: 11px; font-weight: bold; color: %1; padding: 2px;").arg(colGreen));

    m_cardStage2->setStyleSheet(QString("QFrame { background-color: %1; border: 1px solid %2; border-radius: 8px; padding: 10px; }")
                                    .arg(dark ? "rgba(137, 220, 235, 0.15)" : "rgba(8, 145, 178, 0.08)", colCyan));
    m_lblStage2Title->setStyleSheet(QString("font-size: 13px; font-weight: bold; color: %1;").arg(colCyan));
    m_lblStage2Val->setStyleSheet(QString("font-size: 13px; font-weight: bold; color: %1;").arg(tokens.textPrimary.name()));

    m_lblConv2To3->setStyleSheet(QString("font-size: 11px; font-weight: bold; color: %1; padding: 2px;").arg(colGreen));

    m_cardStage3->setStyleSheet(QString("QFrame { background-color: %1; border: 1px solid %2; border-radius: 8px; padding: 10px; }")
                                    .arg(dark ? "rgba(166, 227, 161, 0.15)" : "rgba(21, 128, 61, 0.08)", colGreen));
    m_lblStage3Title->setStyleSheet(QString("font-size: 13px; font-weight: bold; color: %1;").arg(colGreen));
    m_lblStage3Val->setStyleSheet(QString("font-size: 13px; font-weight: bold; color: %1;").arg(tokens.textPrimary.name()));

    m_lblConv3To4->setStyleSheet(QString("font-size: 11px; font-weight: bold; color: %1; padding: 2px;").arg(colAmber));

    m_cardStage4->setStyleSheet(QString("QFrame { background-color: %1; border: 1px solid %2; border-radius: 8px; padding: 10px; }")
                                    .arg(dark ? "rgba(249, 226, 175, 0.15)" : "rgba(180, 83, 9, 0.08)", colAmber));
    m_lblStage4Title->setStyleSheet(QString("font-size: 13px; font-weight: bold; color: %1;").arg(colAmber));
    m_lblStage4Val->setStyleSheet(QString("font-size: 13px; font-weight: bold; color: %1;").arg(tokens.textPrimary.name()));

    m_lstPastoralActions->setStyleSheet(QString("QListWidget { background: transparent; border: none; font-size: 12px; }"
                                                "QListWidget::item { padding: 8px; border-bottom: 1px solid %1; color: %2; font-weight: bold; }"
                                                "QListWidget::item:hover { background-color: %3; color: %4; border-radius: 6px; }")
                                            .arg(tokens.borderColor.name(), tokens.textPrimary.name(), tokens.bgSurface.name(), colBlue));
}
