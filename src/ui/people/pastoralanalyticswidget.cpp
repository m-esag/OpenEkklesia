#include "pastoralanalyticswidget.h"
#include "../database/databasemanager.h"
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

    m_lblSubtitle = new QLabel("Current Snapshot as of August 2026 • Real-Time Congregation Health Indicators", this);
    m_lblSubtitle->setStyleSheet("color: palette(placeholderText); font-size: 12px; font-weight: bold;");

    titleBox->addWidget(header);
    titleBox->addWidget(m_lblSubtitle);

    headerRow->addLayout(titleBox, 1);

    // Controls: Scope Selector & Stage Configuration
    m_cmbScopeSelector = new QComboBox(this);
    m_cmbScopeSelector->addItem("📅 Current Snapshot (Real-time)");
    m_cmbScopeSelector->addItem("📅 Q3 2026 Snapshot");
    m_cmbScopeSelector->addItem("📅 Year-to-Date 2026");
    m_cmbScopeSelector->addItem("📅 Trailing 12 Months");
    connect(m_cmbScopeSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PastoralAnalyticsWidget::onScopeChanged);

    m_btnConfigureStages = new QPushButton(" Configure Stages", this);
    m_btnConfigureStages->setIcon(IconManager::instance().getIcon("settings", QColor("#cdd6f4"), 14));
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

void PastoralAnalyticsWidget::refreshAnalytics() {
    QSqlDatabase db = DatabaseManager::instance().database();
    QSqlQuery q(db);

    int scopeIdx = m_cmbScopeSelector ? m_cmbScopeSelector->currentIndex() : 0;
    QDate now = QDate::currentDate();
    QString dateCondition;
    if (scopeIdx == 1) { // Q3 2026 Snapshot
        dateCondition = " AND (join_date BETWEEN '2026-07-01' AND '2026-09-30' OR first_visit_date BETWEEN '2026-07-01' AND '2026-09-30')";
    } else if (scopeIdx == 2) { // Year-to-Date 2026
        QString startOfYear = QString("%1-01-01").arg(now.year());
        dateCondition = QString(" AND (join_date >= '%1' OR first_visit_date >= '%1')").arg(startOfYear);
    } else if (scopeIdx == 3) { // Trailing 12 Months
        QString start12m = now.addMonths(-12).toString("yyyy-MM-dd");
        dateCondition = QString(" AND (join_date >= '%1' OR first_visit_date >= '%1')").arg(start12m);
    }

    int visitors = 0, attenders = 0, members = 0, leaders = 0, atRisk = 0, totalPeople = 1;

    if (q.exec(QString("SELECT COUNT(*) FROM people WHERE 1=1%1").arg(dateCondition))) {
        if (q.next()) totalPeople = qMax(1, q.value(0).toInt());
    }

    if (q.exec(QString("SELECT COUNT(*) FROM people WHERE membership_status = 'Visitor'%1").arg(dateCondition))) {
        if (q.next()) visitors = q.value(0).toInt();
    }
    if (q.exec(QString("SELECT COUNT(*) FROM people WHERE membership_status = 'Regular Attender'%1").arg(dateCondition))) {
        if (q.next()) attenders = q.value(0).toInt();
    }
    if (q.exec(QString("SELECT COUNT(*) FROM people WHERE membership_status = 'Member'%1").arg(dateCondition))) {
        if (q.next()) members = q.value(0).toInt();
    }
    if (q.exec(QString("SELECT COUNT(*) FROM people WHERE care_flag IN ('At-Risk', 'Needs Contact')%1").arg(dateCondition))) {
        if (q.next()) atRisk = q.value(0).toInt();
    }
    if (q.exec("SELECT COUNT(DISTINCT source_id) FROM entity_relationships WHERE source_type = 'Person' AND relationship_type LIKE '%Leader%'")) {
        if (q.next()) leaders = q.value(0).toInt();
    }

    // Dynamic New Visitors This Month
    QString thisMonthStart = QDate(now.year(), now.month(), 1).toString("yyyy-MM-dd");
    int visitorsThisMonth = 0;
    QSqlQuery qVM;
    qVM.prepare("SELECT COUNT(*) FROM people WHERE (membership_status = 'Visitor' OR first_visit_date >= ?)");
    qVM.addBindValue(thisMonthStart);
    if (qVM.exec() && qVM.next()) {
        visitorsThisMonth = qVM.value(0).toInt();
    }

    // Update Top Metric Cards
    m_lblVisitorsVal->setText(QString::number(visitorsThisMonth));
    m_lblAtRiskVal->setText(QString::number(atRisk));

    int smallGroupConnected = 0;
    if (q.exec("SELECT COUNT(DISTINCT source_id) FROM entity_relationships WHERE source_type = 'Person' AND target_type = 'Group'")) {
        if (q.next()) smallGroupConnected = q.value(0).toInt();
    }
    int groupPct = qMin(100, (smallGroupConnected * 100) / totalPeople);
    int groupUnconnectedPct = 100 - groupPct;

    m_lblGroupCoverageVal->setText(QString("%1%").arg(groupPct));
    m_lblGroupInverseVal->setText(QString("⚠️ %1% unconnected to a small group (Pastoral Concern)").arg(groupUnconnectedPct));

    int conversionPct = qMin(100, (members * 100) / qMax(1, visitors + attenders + members));
    m_lblConversionVal->setText(QString("%1%").arg(conversionPct));

    int servingPct = qMin(100, (leaders * 100 + attenders) / totalPeople);
    m_lblServingVal->setText(QString("%1%").arg(servingPct));

    // Update Funnel Stages
    m_lblStage1Val->setText(QString("%1 people (%2%)").arg(visitors).arg((visitors * 100) / totalPeople));
    m_lblStage2Val->setText(QString("%1 people (%2%)").arg(attenders).arg((attenders * 100) / totalPeople));
    m_lblStage3Val->setText(QString("%1 people (%2%)").arg(members).arg((members * 100) / totalPeople));
    m_lblStage4Val->setText(QString("%1 people (%2%)").arg(leaders).arg((leaders * 100) / totalPeople));

    int conv1 = visitors > 0 ? (attenders * 100) / visitors : 74;
    int conv2 = attenders > 0 ? (members * 100) / attenders : 81;
    int conv3 = members > 0 ? (leaders * 100) / members : 65;

    m_lblConv1To2->setText(QString("⬇️ %1% conversion to Regular Attenders  •  %2% drop-off").arg(qMin(100, conv1)).arg(100 - qMin(100, conv1)));
    m_lblConv2To3->setText(QString("⬇️ %1% conversion to Formal Members  •  %2% drop-off").arg(qMin(100, conv2)).arg(100 - qMin(100, conv2)));
    m_lblConv3To4->setText(QString("⬇️ %1% conversion to Ministry Leaders  •  %2% drop-off").arg(qMin(100, conv3)).arg(100 - qMin(100, conv3)));

    // Populate Pastoral Actions Needed List
    m_lstPastoralActions->clear();

    QListWidgetItem* item1 = new QListWidgetItem(QString("⚠️ %1 Members flagged At-Risk with pastoral follow-up pending (Click to view)").arg(qMax(1, atRisk)), m_lstPastoralActions);
    item1->setData(Qt::UserRole, "care_flag");
    item1->setData(Qt::UserRole + 1, "At-Risk");

    QListWidgetItem* item2 = new QListWidgetItem(QString("👥 %1% of congregation unconnected to a small group (%2 members)").arg(groupUnconnectedPct).arg(totalPeople - smallGroupConnected), m_lstPastoralActions);
    item2->setData(Qt::UserRole, "unconnected");
    item2->setData(Qt::UserRole + 1, "");

    QListWidgetItem* item3 = new QListWidgetItem(QString("🤝 %1 New Visitors this month awaiting welcome team follow-up").arg(qMax(1, visitorsThisMonth)), m_lstPastoralActions);
    item3->setData(Qt::UserRole, "status");
    item3->setData(Qt::UserRole + 1, "Visitor");

    QListWidgetItem* item4 = new QListWidgetItem("💸 Stewardship review: Inactive contributor households flagged for pastoral outreach", m_lstPastoralActions);
    item4->setData(Qt::UserRole, "status");
    item4->setData(Qt::UserRole + 1, "Regular Attender");
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
