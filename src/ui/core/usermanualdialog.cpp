#include "usermanualdialog.h"
#include "thememanager.h"
#include "iconmanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QToolBar>
#include <QPrinter>
#include <QPrintDialog>
#include <QKeySequence>
#include <QShortcut>
#include <QRegularExpression>
#include <QScrollBar>
#include <QBuffer>

UserManualDialog::UserManualDialog(QWidget* parent, const QString& initialSection)
    : QDialog(parent), m_currentFontSize(13) {
    setupUI();
    populateTopics();

    if (!initialSection.isEmpty()) {
        selectSection(initialSection);
    } else {
        selectSection("intro");
    }
}

void UserManualDialog::showManual(QWidget* parent, const QString& initialSection) {
    UserManualDialog* dlg = new UserManualDialog(parent, initialSection);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

QList<ManualTopic> UserManualDialog::getTopicList() {
    return {
        {"intro", "1. Executive Overview & Quick Start", "compass"},
        {"shortcuts", "2. Keyboard Shortcuts & Command Palette", "command"},
        {"people", "3. People & 360° Pastoral Dossier", "users"},
        {"groups", "4. Fixed Ministries & Serving Groups", "layers"},
        {"smartbridge", "5. Smart Bridge v2.0 & Synergy Engine", "sparkles"},
        {"graph", "6. Connection Graph & Relational Visualizer", "network"},
        {"shepherd", "7. Shepherd's Workbench & Pastoral Care", "heart-handshake"},
        {"service", "8. Service Liturgy Builder & Run-Sheet", "list-music"},
        {"planning", "9. Ministry Planning Matrix", "calendar"},
        {"facilities", "10. Facilities Matrix & Asset Inventory", "landmark"},
        {"finance", "11. Giving, Contributions & Double-Entry Ledger", "hand-coins"},
        {"vault", "12. Document Vault & Data Hygiene", "shield-check"},
        {"settings", "13. Settings, RBAC & Theme Contrast Guardian", "settings"}
    };
}

QString UserManualDialog::getLucideDataUri(const QString& name, const QColor& color, int size) {
    QPixmap px = IconManager::instance().getPixmap(name, color, size * 2); // 2x for retina crispness
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    px.save(&buffer, "PNG");
    return QString("data:image/png;base64,%1").arg(QString::fromLatin1(bytes.toBase64()));
}

void UserManualDialog::setupUI() {
    setWindowTitle("OpenEkklesia — Comprehensive User Manual & Help Center");
    resize(1150, 780);

    ThemeTokens tok = ThemeManager::instance().currentTokens();
    const QString onAccent = ThemeManager::textOn(tok.accentPrimary).name();
    setStyleSheet(QString(
        "QDialog { background-color: %1; color: %2; }"
        "QSplitter::handle { background-color: %3; width: 1px; }"
        "QListWidget { background-color: %4; border: 1px solid %3; border-radius: 8px; color: %2; padding: 4px; }"
        "QListWidget::item { padding: 8px 10px; border-radius: 6px; margin: 2px 0px; font-size: 12px; }"
        "QListWidget::item:hover { background-color: palette(mid); }"
        "QListWidget::item:selected { background-color: %5; color: %6; font-weight: bold; }"
        "QTextBrowser { background-color: %4; border: 1px solid %3; border-radius: 8px; color: %2; padding: 16px; selection-background-color: %5; }"
        "QLineEdit { background-color: %4; border: 1px solid %3; border-radius: 6px; padding: 6px 10px; color: %2; font-size: 12px; }"
        "QPushButton { background-color: %4; border: 1px solid %3; border-radius: 6px; padding: 6px 12px; color: %2; font-size: 12px; font-weight: 500; }"
        "QPushButton:hover { background-color: palette(mid); border-color: %5; }"
        "QPushButton#primaryBtn { background-color: %5; color: %6; font-weight: bold; border: none; }"
        "QPushButton#primaryBtn:hover { background-color: %7; }"
    ).arg(tok.bgBase.name(), tok.textPrimary.name(), tok.borderColor.name(), tok.bgSurface.name(), tok.accentPrimary.name(), onAccent, tok.accentPrimary.darker(115).name()));

    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(16, 16, 16, 16);
    rootLayout->setSpacing(12);

    // --- Header & Navigation Toolbar ---
    QHBoxLayout* topBar = new QHBoxLayout();
    topBar->setSpacing(8);

    m_btnBack = new QPushButton(" Back", this);
    m_btnBack->setIcon(IconManager::instance().getIcon("arrow-left", tok.textPrimary, 14));
    m_btnBack->setToolTip("Navigate to previous section (Alt+Left)");

    m_btnForward = new QPushButton("Forward ", this);
    m_btnForward->setIcon(IconManager::instance().getIcon("arrow-right", tok.textPrimary, 14));
    m_btnForward->setLayoutDirection(Qt::RightToLeft);
    m_btnForward->setToolTip("Navigate to next section (Alt+Right)");

    m_btnHome = new QPushButton(" Home", this);
    m_btnHome->setIcon(IconManager::instance().getIcon("home", tok.textPrimary, 14));
    m_btnHome->setToolTip("Return to Executive Overview");

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search user manual topics or keywords (Ctrl+F)...");
    m_searchEdit->setClearButtonEnabled(true);

    m_btnZoomIn = new QPushButton(this);
    m_btnZoomIn->setIcon(IconManager::instance().getIcon("zoom-in", tok.textPrimary, 14));
    m_btnZoomIn->setToolTip("Increase font size (Ctrl++)");
    m_btnZoomIn->setFixedWidth(36);

    m_btnZoomOut = new QPushButton(this);
    m_btnZoomOut->setIcon(IconManager::instance().getIcon("zoom-out", tok.textPrimary, 14));
    m_btnZoomOut->setToolTip("Decrease font size (Ctrl+-)");
    m_btnZoomOut->setFixedWidth(36);

    m_btnPrint = new QPushButton(" Print", this);
    m_btnPrint->setIcon(IconManager::instance().getIcon("printer", tok.textPrimary, 14));
    m_btnPrint->setToolTip("Print or export manual documentation to PDF");

    QPushButton* btnClose = new QPushButton(" Close", this);
    btnClose->setIcon(IconManager::instance().getPrimaryIcon("x", 14));
    btnClose->setObjectName("primaryBtn");
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);

    topBar->addWidget(m_btnBack);
    topBar->addWidget(m_btnForward);
    topBar->addWidget(m_btnHome);
    topBar->addWidget(m_searchEdit, 1);
    topBar->addWidget(m_btnZoomIn);
    topBar->addWidget(m_btnZoomOut);
    topBar->addWidget(m_btnPrint);
    topBar->addWidget(btnClose);

    rootLayout->addLayout(topBar);

    // --- Main Content Splitter ---
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    // Left Panel: Table of Contents
    QWidget* leftPanel = new QWidget(splitter);
    QVBoxLayout* leftLay = new QVBoxLayout(leftPanel);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(6);

    QLabel* lblToc = new QLabel("<b>Table of Contents</b>", leftPanel);
    lblToc->setStyleSheet(QString("font-size: 13px; color: %1;").arg(tok.accentPrimary.name()));
    leftLay->addWidget(lblToc);

    m_topicList = new QListWidget(leftPanel);
    m_topicList->setMinimumWidth(270);
    m_topicList->setIconSize(QSize(16, 16));
    leftLay->addWidget(m_topicList, 1);

    m_lblStatus = new QLabel("13 Chapters Available", leftPanel);
    m_lblStatus->setStyleSheet(QString("font-size: 11px; color: %1; opacity: 0.8;").arg(tok.textSecondary.name()));
    leftLay->addWidget(m_lblStatus);

    splitter->addWidget(leftPanel);

    // Right Panel: Rich Documentation Browser
    m_contentBrowser = new QTextBrowser(splitter);
    m_contentBrowser->setOpenExternalLinks(false);
    m_contentBrowser->setHtml(getFullManualHtml());
    splitter->addWidget(m_contentBrowser);

    splitter->setSizes({280, 840});
    rootLayout->addWidget(splitter, 1);

    // --- Signal Wiring ---
    connect(m_topicList, &QListWidget::itemClicked, this, &UserManualDialog::onTopicSelected);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &UserManualDialog::onSearchTextChanged);
    connect(m_contentBrowser, &QTextBrowser::anchorClicked, this, &UserManualDialog::onAnchorClicked);
    connect(m_btnBack, &QPushButton::clicked, m_contentBrowser, &QTextBrowser::backward);
    connect(m_btnForward, &QPushButton::clicked, m_contentBrowser, &QTextBrowser::forward);
    connect(m_btnHome, &QPushButton::clicked, this, [this]() { selectSection("intro"); });
    connect(m_btnZoomIn, &QPushButton::clicked, this, &UserManualDialog::onZoomIn);
    connect(m_btnZoomOut, &QPushButton::clicked, this, &UserManualDialog::onZoomOut);
    connect(m_btnPrint, &QPushButton::clicked, this, &UserManualDialog::onPrint);

    // Keyboard Shortcuts
    new QShortcut(QKeySequence::Find, this, [this]() { m_searchEdit->setFocus(); m_searchEdit->selectAll(); });
    new QShortcut(QKeySequence::ZoomIn, this, this, &UserManualDialog::onZoomIn);
    new QShortcut(QKeySequence::ZoomOut, this, this, &UserManualDialog::onZoomOut);
    new QShortcut(QKeySequence::Back, this, [this]() { m_contentBrowser->backward(); });
    new QShortcut(QKeySequence::Forward, this, [this]() { m_contentBrowser->forward(); });
}

void UserManualDialog::populateTopics() {
    m_topicList->clear();
    ThemeTokens tok = ThemeManager::instance().currentTokens();
    const auto topics = getTopicList();
    for (const auto& t : topics) {
        QListWidgetItem* item = new QListWidgetItem(t.title, m_topicList);
        item->setData(Qt::UserRole, t.id);
        item->setIcon(IconManager::instance().getIcon(t.lucideIcon, tok.accentPrimary, 16));
    }
}

void UserManualDialog::selectSection(const QString& sectionId) {
    for (int i = 0; i < m_topicList->count(); ++i) {
        QListWidgetItem* item = m_topicList->item(i);
        if (item->data(Qt::UserRole).toString() == sectionId) {
            m_topicList->setCurrentItem(item);
            break;
        }
    }
    m_contentBrowser->scrollToAnchor(sectionId);
}

void UserManualDialog::onTopicSelected(QListWidgetItem* item) {
    if (!item) return;
    QString sectionId = item->data(Qt::UserRole).toString();
    m_contentBrowser->scrollToAnchor(sectionId);
}

void UserManualDialog::onSearchTextChanged(const QString& query) {
    QString q = query.trimmed().toLower();
    int visibleCount = 0;
    for (int i = 0; i < m_topicList->count(); ++i) {
        QListWidgetItem* item = m_topicList->item(i);
        bool matches = q.isEmpty() || item->text().toLower().contains(q) || item->data(Qt::UserRole).toString().toLower().contains(q);
        item->setHidden(!matches);
        if (matches) visibleCount++;
    }

    if (!q.isEmpty()) {
        m_lblStatus->setText(QString("%1 matching chapters").arg(visibleCount));
        m_contentBrowser->find(q);
    } else {
        m_lblStatus->setText("13 Chapters Available");
    }
}

void UserManualDialog::onAnchorClicked(const QUrl& url) {
    QString fragment = url.fragment();
    if (fragment.isEmpty()) {
        fragment = url.toString();
        if (fragment.startsWith("#")) fragment = fragment.mid(1);
    }

    if (!fragment.isEmpty()) {
        selectSection(fragment);
    }
}

void UserManualDialog::onZoomIn() {
    m_currentFontSize += 1;
    if (m_currentFontSize > 22) m_currentFontSize = 22;
    m_contentBrowser->zoomIn(1);
}

void UserManualDialog::onZoomOut() {
    m_currentFontSize -= 1;
    if (m_currentFontSize < 9) m_currentFontSize = 9;
    m_contentBrowser->zoomOut(1);
}

void UserManualDialog::onPrint() {
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName("OpenEkklesia_User_Manual");
    QPrintDialog dlg(&printer, this);
    dlg.setWindowTitle("Print OpenEkklesia User Manual");
    if (dlg.exec() == QDialog::Accepted) {
        m_contentBrowser->print(&printer);
    }
}

QString UserManualDialog::getFullManualHtml() {
    ThemeTokens tok = ThemeManager::instance().currentTokens();
    QString accent = tok.accentPrimary.name();
    QString textCol = tok.textPrimary.name();
    QString secText = tok.textSecondary.name();
    QString surface = tok.bgSurface.name();
    QString border = tok.borderColor.name();
    QString bgBase = tok.bgBase.name();

    // Lucide Icons as Embedded Data URIs
    QString iconCompass = getLucideDataUri("compass", tok.accentPrimary, 20);
    QString iconCommand = getLucideDataUri("command", tok.accentPrimary, 20);
    QString iconUsers = getLucideDataUri("users", tok.accentPrimary, 20);
    QString iconLayers = getLucideDataUri("layers", tok.accentPrimary, 20);
    QString iconSparkles = getLucideDataUri("sparkles", tok.accentPrimary, 20);
    QString iconNetwork = getLucideDataUri("network", tok.accentPrimary, 20);
    QString iconShepherd = getLucideDataUri("heart-handshake", tok.accentPrimary, 20);
    QString iconMusic = getLucideDataUri("list-music", tok.accentPrimary, 20);
    QString iconCalendar = getLucideDataUri("calendar", tok.accentPrimary, 20);
    QString iconLandmark = getLucideDataUri("landmark", tok.accentPrimary, 20);
    QString iconFinance = getLucideDataUri("hand-coins", tok.accentPrimary, 20);
    QString iconVault = getLucideDataUri("shield-check", tok.accentPrimary, 20);
    QString iconSettings = getLucideDataUri("settings", tok.accentPrimary, 20);
    QString iconAlert = getLucideDataUri("alert-triangle", QColor("#f9e2af"), 18);
    QString iconTip = getLucideDataUri("sparkles", QColor("#a6e3a1"), 18);

    return QString(R"rawhtml(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<style>
    body {
        font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, Roboto, Helvetica, Arial, sans-serif;
        line-height: 1.6;
        color: %2;
        background-color: %6;
        padding: 10px 20px;
    }
    h1 {
        font-size: 26px;
        color: %1;
        border-bottom: 2px solid %1;
        padding-bottom: 8px;
        margin-top: 20px;
        margin-bottom: 12px;
    }
    h2 {
        font-size: 19px;
        color: %1;
        border-bottom: 1px solid %5;
        padding-bottom: 6px;
        margin-top: 32px;
        margin-bottom: 12px;
    }
    h3 {
        font-size: 15px;
        color: %2;
        margin-top: 18px;
        margin-bottom: 8px;
        font-weight: bold;
    }
    p, li {
        font-size: 13px;
        color: %2;
    }
    a {
        color: %1;
        text-decoration: none;
        font-weight: bold;
    }
    a:hover {
        text-decoration: underline;
    }
    .badge {
        display: inline-block;
        padding: 2px 8px;
        border-radius: 4px;
        font-size: 11px;
        font-weight: bold;
        margin-right: 4px;
    }
    .badge-catalyst { background-color: rgba(250, 179, 135, 0.2); color: #fab387; border: 1px solid #fab387; }
    .badge-shepherd { background-color: rgba(166, 227, 161, 0.2); color: #a6e3a1; border: 1px solid #a6e3a1; }
    .badge-architect { background-color: rgba(137, 180, 250, 0.2); color: #89b4fa; border: 1px solid #89b4fa; }
    .badge-spark { background-color: rgba(249, 226, 175, 0.2); color: #f9e2af; border: 1px solid #f9e2af; }
    .badge-anchor { background-color: rgba(203, 166, 247, 0.2); color: #cba6f7; border: 1px solid #cba6f7; }
    
    .alert-card {
        background-color: %4;
        border-left: 4px solid %1;
        border-radius: 6px;
        padding: 12px 16px;
        margin: 14px 0;
    }
    .alert-warning {
        background-color: rgba(249, 226, 175, 0.1);
        border-left: 4px solid #f9e2af;
        border-radius: 6px;
        padding: 12px 16px;
        margin: 14px 0;
    }
    .alert-tip {
        background-color: rgba(166, 227, 161, 0.1);
        border-left: 4px solid #a6e3a1;
        border-radius: 6px;
        padding: 12px 16px;
        margin: 14px 0;
    }
    table {
        width: 100%;
        border-collapse: collapse;
        margin: 14px 0;
        font-size: 12px;
    }
    th, td {
        border: 1px solid %5;
        padding: 8px 12px;
        text-align: left;
    }
    th {
        background-color: %4;
        color: %1;
        font-weight: bold;
    }
    tr:nth-child(even) {
        background-color: rgba(255, 255, 255, 0.02);
    }
    kbd {
        background-color: %4;
        border: 1px solid %5;
        border-radius: 4px;
        padding: 2px 6px;
        font-family: 'Consolas', 'Courier New', monospace;
        font-size: 11px;
        font-weight: bold;
        color: %1;
    }
    .nav-grid {
        margin: 16px 0;
    }
    .nav-item {
        display: inline-block;
        width: 48%;
        margin-bottom: 8px;
    }
    .header-icon {
        vertical-align: middle;
        margin-right: 8px;
        width: 20px;
        height: 20px;
    }
</style>
</head>
<body>

<a name="intro"></a>
<h1><img class="header-icon" src="%7"> OpenEkklesia — Comprehensive User Manual</h1>
<p>Welcome to the official operating guide and manual of use for <b>OpenEkklesia</b>, the privacy-first, open-source Church Management System (ChMS) and Pastoral Analytics Suite. Designed with SQLite WAL database architecture, relational network visualizers, liturgical worship planners, Lucide vector icons, and the Smart Bridge v2.0 synergy engine.</p>

<div class="alert-tip">
    <img src="%21" width="16" height="16" style="vertical-align: middle; margin-right: 6px;"> <b>Quick Navigation Index:</b> Click any of the topics below to jump directly to that chapter:
    <div class="nav-grid">
        <div class="nav-item">👉 <a href="#intro">1. Executive Overview & Quick Start</a></div>
        <div class="nav-item">👉 <a href="#shortcuts">2. Keyboard Shortcuts & Command Palette</a></div>
        <div class="nav-item">👉 <a href="#people">3. People & 360° Pastoral Dossier</a></div>
        <div class="nav-item">👉 <a href="#groups">4. Fixed Ministries & Serving Groups</a></div>
        <div class="nav-item">👉 <a href="#smartbridge">5. Smart Bridge v2.0 & Synergy Engine</a></div>
        <div class="nav-item">👉 <a href="#graph">6. Connection Graph & Visualizer</a></div>
        <div class="nav-item">👉 <a href="#shepherd">7. Shepherd's Workbench & Care</a></div>
        <div class="nav-item">👉 <a href="#service">8. Service Liturgy Builder & Run-Sheet</a></div>
        <div class="nav-item">👉 <a href="#planning">9. Ministry Planning Matrix</a></div>
        <div class="nav-item">👉 <a href="#facilities">10. Facilities Matrix & Inventory</a></div>
        <div class="nav-item">👉 <a href="#finance">11. Giving & Double-Entry Ledger</a></div>
        <div class="nav-item">👉 <a href="#vault">12. Document Vault & Data Hygiene</a></div>
        <div class="nav-item">👉 <a href="#settings">13. Settings, Security & RBAC</a></div>
    </div>
</div>

<hr>

<a name="shortcuts"></a>
<h2><img class="header-icon" src="%8"> 2. Keyboard Shortcuts & Command Palette</h2>
<p>OpenEkklesia provides high-speed keyboard shortcuts for pastors, administrators, and volunteers to navigate without taking their hands off the keyboard:</p>

<table>
    <tr><th>Shortcut</th><th>Action</th><th>Description</th></tr>
    <tr><td><kbd>Ctrl+K</kbd></td><td>Command Palette</td><td>Fuzzy search across all people, groups, services, and commands</td></tr>
    <tr><td><kbd>F1</kbd></td><td>User Manual</td><td>Open this interactive in-app help manual from anywhere</td></tr>
    <tr><td><kbd>Ctrl+1</kbd></td><td>Dashboard</td><td>Switch to Main Executive KPI & Summary Dashboard</td></tr>
    <tr><td><kbd>Ctrl+2</kbd></td><td>People</td><td>Open Congregation Directory & Member Dossier</td></tr>
    <tr><td><kbd>Ctrl+3</kbd></td><td>Groups & Ministries</td><td>Open Fixed Ministry Hierarchy & Active Serving Rosters</td></tr>
    <tr><td><kbd>Ctrl+4</kbd></td><td>Giving & Batches</td><td>Open Contributions, Batches, and Financial Ledger</td></tr>
    <tr><td><kbd>Ctrl+5</kbd></td><td>Service Builder</td><td>Open Liturgy Flow Designer & Volunteer Run-Sheet</td></tr>
    <tr><td><kbd>Ctrl+Q</kbd></td><td>Exit Program</td><td>Safely save state and close OpenEkklesia</td></tr>
</table>

<hr>

<a name="people"></a>
<h2><img class="header-icon" src="%9"> 3. People & 360° Pastoral Dossier</h2>
<p>The <b>People Module</b> is the central repository for member records, growth tracking, spiritual gifts assessment, and relational connection mapping.</p>

<h3>Key Features:</h3>
<ul>
    <li><b>360° Member Dossier:</b> View complete member lifecycle: contact info, household relationships, membership stage (Member, Regular Attender, Visitor, Inactive), care timeline, and giving history.</li>
    <li><b>Spiritual Gifts & Ministry Fit Profile (Tab 4):</b>
        <ul>
            <li><b>Primary Behavioral Archetype:</b> Choose between 5 human-facing ministry archetypes:
                <span class="badge badge-catalyst">🔥 Catalyst</span>
                <span class="badge badge-shepherd">🌿 Shepherd</span>
                <span class="badge badge-architect">📐 Architect</span>
                <span class="badge badge-spark">⚡ Spark</span>
                <span class="badge badge-anchor">⚓ Anchor</span>
            </li>
            <li><b>Secondary Archetype:</b> Optional secondary behavioral complement.</li>
            <li><b>Spiritual Gifts Selection:</b> Multi-select gift chips strictly capped at a maximum of 4 gifts with real-time counters.</li>
            <li><b>Relational Style Vector:</b> Direct assignment of primary behavioral styles (Organizer, Listener, Doer, Energizer, Strategist, Connector).</li>
        </ul>
    </li>
    <li><b>Care Flags:</b> Tag individuals as <code>Normal</code>, <code>Needs Contact</code>, <code>Follow-up</code>, <code>At-Risk</code>, <code>Grief Support</code>, or <code>Leader</code>. Flags synchronize with the Shepherd's Workbench queue and dashboard alerts.</li>
    <li><b>vCard Export:</b> One-click contact export for mobile devices and email clients.</li>
</ul>

<hr>

<a name="groups"></a>
<h2><img class="header-icon" src="%10"> 4. Fixed Ministries & Serving Groups</h2>
<p>OpenEkklesia implements a <b>"Ministry = Blueprint, Group = Roster + Override"</b> dual-layer hierarchy.</p>

<h3>Architecture Overview:</h3>
<ul>
    <li><b>Level 0 (Top-Level Fixed Ministries):</b> The 11 Canonical Ministries (<a href="#smartbridge">see blueprints</a>) exist as permanent root nodes. They are system-locked and cannot be deleted or archived.</li>
    <li><b>Level 1+ (Dynamic Child Serving Teams):</b> Serving sub-teams (e.g. <i>"Band A"</i> under <i>Worship & Creative Arts</i>, or <i>"Sunday Tech Crew"</i> under <i>Media & AV Tech</i>) are nested underneath their parent root ministry.</li>
    <li><b>Automatic Blueprint Inheritance:</b> When creating a new serving group under a parent ministry, it automatically inherits the parent blueprint's category, ideal archetypes, target vector, and required spiritual gifts.</li>
    <li><b>Macro Department Aggregation:</b> Selecting a root ministry aggregates all active volunteers across its child teams, displaying a combined department roster and macro-level archetype composition.</li>
</ul>

<hr>

<a name="smartbridge"></a>
<h2><img class="header-icon" src="%11"> 5. Smart Bridge v2.0 & Synergy Engine</h2>
<p>The <b>Smart Bridge v2.0</b> engine calculates mathematical chemistry and relational equilibrium when placing volunteers onto ministry teams.</p>

<h3>Chemistry Scoring (0–100, clamped to 15–98):</h3>
<ol>
    <li><b>Void Law (+30 / +25 / +20):</b> Awarded when a candidate fills a critical missing role — <span style="color:#a6e3a1;">+30</span> for an Anchor/Architect joining a team with zero execution backbone, <span style="color:#a6e3a1;">+25</span> for a Shepherd joining a team with no relational care, and <span style="color:#a6e3a1;">+20</span> for a strong secondary-archetype complement.</li>
    <li><b>Clash Law (<span style="color:#f38ba8;">−20 / −15 Penalty</span>):</b> Applied when the team is over-saturated with competing visionaries (&ge;3 Catalysts, −20) or high-energy surges (half or more of the roster as Energizers, −15) without sufficient grounding.</li>
    <li><b>Equilibrium Law:</b> Measures the Euclidean distance delta between the current team's relational vector and the canonical target blueprint (&times;50 pts). Positive delta indicates improved balance.</li>
    <li><b>Gifts &amp; Interests Match (&le; +35 Boost):</b> Proportional bonus for matching required spiritual gifts (max +20) and declared ministry interests (max +15).</li>
    <li><b>Co-Leadership Pairing (+15 Boost):</b> High-synergy pairing bonus for complementary leadership models (Catalyst + Architect, Shepherd + Spark, or Spark + Anchor).</li>
    <li><b>Additional Adjustments:</b> Household family tie (+15), geographic proximity (+10), isolation magnetism for unconnected members (+20), pastoral-care routing into small supportive circles (+15), and a sabbatical guardrail that strongly discourages high-stress placements during a rest period.</li>
</ol>

<div class="alert-warning">
    <img src="%20" width="16" height="16" style="vertical-align: middle; margin-right: 6px;"> <b>Structural Gatekeeper (hard block):</b>
    Candidates are never recommended for <b>elected governance bodies</b> (Church Board, Elders, Deacons, Director-led bodies) or <b>Closed / Seasonal cohorts</b> — the cohort itself is not open to placement.
</div>

<div class="alert-tip">
    <img src="%21" width="16" height="16" style="vertical-align: middle; margin-right: 6px;"> <b>Advisory Warnings — pastor review, not blocks:</b>
    The engine flags — but never silently hides — a candidate when:
    <ul>
        <li><b>Serving Overload:</b> already active in 3 or more serving groups (threshold configurable in Settings).</li>
        <li><b>Schedule Conflict:</b> group meeting day and time clashes with an existing commitment.</li>
        <li><b>Stale Group:</b> no logged activity in over 180 days.</li>
        <li><b>Leader Under Care:</b> the group leader carries an <code>At-Risk</code> or <code>Needs Contact</code> care flag.</li>
        <li><b>Oversaturation:</b> &ge;3 Catalysts or &ge;4 Sparks already on the team.</li>
        <li><b>Pastoral Care Safety:</b> the candidate is in active care and the team is currently understaffed.</li>
    </ul>
    Each flag appears as a yellow warning on the candidate card; "⚡ Connect" stays enabled, and connecting despite a warning is logged as an override in the audit trail.
</div>

<div class="alert-tip">
    <img src="%21" width="16" height="16" style="vertical-align: middle; margin-right: 6px;"> <b>Always Advisory:</b> Smart Bridge only makes <i>recommendations</i> — it never edits the roster itself. Every candidate card shows its fit score, the specific reason, and any pastor-review flags; a pastor or administrator can always add any member to any group directly from group management, regardless of the engine's suggestion.
</div>

<hr>

<a name="graph"></a>
<h2><img class="header-icon" src="%12"> 6. Connection Graph & Relational Visualizer</h2>
<p>The <b>Connection Graph</b> renders force-directed physics visualizations of household and relationship networks, small group clusters, and Smart Bridge recommendations.</p>

<h3>Canvas Features:</h3>
<ul>
    <li><b>Node Interaction:</b> Click on any Person or Group to inspect relational ties and open the Smart Bridge match panel.</li>
    <li><b>Relational Edge Legend:</b>
        <ul>
            <li><span style="color:#f9e2af;">Gold Solid</span>: Verified household family relationships.</li>
            <li><span style="color:#89b4fa;">Cyan Solid</span>: Ministry leadership ties (e.g. group Leader).</li>
            <li><span style="color:#a6adc8;">Gray Dashed</span>: General member-to-group connections.</li>
            <li><span style="color:#f38ba8;">Red Dashed</span>: Burnout-risk links.</li>
            <li><span style="color:#89b4fa;">Cyan Dashed</span>: Algorithmic Smart Bridge synergy match recommendations.</li>
        </ul>
    </li>
    <li><b>1-Click "⚡ Connect":</b> Instant group placement with automated audit logging (<code>connection_source = 'smart_bridge'</code>, fit score, and timestamp).</li>
    <li><b>Layout Controls:</b> Freeze / unfreeze the force-directed physics, switch between Force-Directed and Clustered layouts, toggle all relational links on/off, and choose connector thickness (Thin / Normal / Bold).</li>
    <li><b>Working View Persistence:</b> Filter toggles (people, households, groups, burnout, isolation, hygiene, bridge, family) and the chosen layout are remembered between sessions.</li>
</ul>

<hr>

<a name="shepherd"></a>
<h2><img class="header-icon" src="%13"> 7. Shepherd's Workbench & Pastoral Care</h2>
<p>The <b>Shepherd's Workbench</b> gives pastoral staff a focused, privacy-conscious cockpit for member care and follow-up.</p>

<ul>
    <li><b>Daily Pastoral Queue:</b> Automatically surfaces members with pending follow-up dates due today or overdue.</li>
    <li><b>Touchpoint Logging:</b> Record pastoral visits, phone calls, counseling sessions, hospital visitations, and prayer requests.</li>
    <li><b>Confidentiality:</b> Pastoral care notes are strictly segregated from general volunteer member dossiers.</li>
    <li><b>Care Flag Synchronization:</b> Updating a care status automatically synchronizes across the People module and Shepherd queue.</li>
</ul>

<hr>

<a name="service"></a>
<h2><img class="header-icon" src="%14"> 8. Service Liturgy Builder & Run-Sheet</h2>
<p>Plan weekly worship services, order of liturgy, sermon elements, and volunteer production rosters.</p>

<ul>
    <li><b>Drag-and-Drop Liturgy Flow:</b> Drag reusable elements (Songs, Scripture, Media, Text / Liturgy) into the run sheet, or double-click to add — including Worship, Welcome, Sermon, Communion, Offering, and Benediction items.</li>
    <li><b>Element Library:</b> Reusable catalog of songs, liturgical prayers, and video cues with pre-set standard durations, live type filtering, search, and element-usage statistics.</li>
    <li><b>Live Timer & Duration Tracking:</b> Total service duration calculates in real time, with per-item start times and over-time warnings (amber above target, red on overrun).</li>
    <li><b>Volunteer Run-Sheet:</b> Assign roles (Worship Leader, Audio Engineer, Video Switcher, Scripture Reader, Usher, and more), mark attendance, export formatted printable run-sheets (PDF), and generate volunteer calendar exports.</li>
</ul>

<hr>

<a name="planning"></a>
<h2><img class="header-icon" src="%15"> 9. Ministry Planning Matrix</h2>
<p>The <b>Ministry Planning Matrix</b> coordinates ministry initiatives, leadership retreats, seasonal programs, and volunteer schedules on a shared timeline.</p>

<ul>
    <li><b>Ministry Product Roadmap:</b> 12-month initiative benchmarks organized by Ministry / Team, with quarterly milestone coloring.</li>
    <li><b>Event Project Gantt Chart:</b> A 20-day rolling window anchored to a real calendar date (or a dated service event) showing each activity's owner, status, and start/end span.</li>
    <li><b>Task Lifecycle:</b> Add, edit, and remove initiatives with assigned ministry team, owner, planned vs. actual dates, and status transitions (Planned → In-Progress → Completed).</li>
    <li><b>Event Anchoring:</b> Link a task to a dated service from the Service Builder so the Gantt window follows real worship dates.</li>
</ul>

<hr>

<a name="facilities"></a>
<h2><img class="header-icon" src="%16"> 10. Facilities Matrix & Asset Inventory</h2>
<p>Track church buildings, rooms, AV equipment, vehicles, and loanable assets.</p>

<ul>
    <li><b>Facility Booking Matrix:</b> Schedule sanctuaries, fellowship halls, classrooms, and equipment with automated collision prevention — double-bookings of the same space, date, and time are rejected, and reservations export to calendar (.ics).</li>
    <li><b>Asset Barcode Checkout:</b> Scan or log equipment check-outs with condition notes and return dates, plus a full chain-of-custody log per asset.</li>
    <li><b>Asset Tracking:</b> Manage serial numbers, purchase dates, locations, and current condition for every piece of equipment.</li>
</ul>

<hr>

<a name="finance"></a>
<h2><img class="header-icon" src="%17"> 11. Giving, Contributions & Double-Entry Ledger</h2>
<p>OpenEkklesia contains a full <b>double-entry accounting engine</b> for tithes, offerings, and church financial stewardship.</p>

<ul>
    <li><b>Multi-Fund Contributions:</b> Record gifts split across General Fund, Building Fund, Missions, and Youth Ministries.</li>
    <li><b>Batch Entry:</b> Rapid envelope number entry for Sunday counting teams with batch reconciliation before posting.</li>
    <li><b>General Ledger:</b> Complete Chart of Accounts (Assets, Liabilities, Equity, Income, Expenses).</li>
    <li><b>Tax Receipts &amp; Statements:</b> Generate annual donor contribution statements formatted as U.S. IRS-style reports.</li>
    <li><b>Anonymous Giving Support:</b> Record anonymous offerings while maintaining balanced double-entry ledger books.</li>
</ul>

<hr>

<a name="vault"></a>
<h2><img class="header-icon" src="%18"> 12. Document Vault & Data Hygiene</h2>
<p>The <b>Document Vault</b> provides local, privacy-first document storage linked to people, households, or ministry teams.</p>

<ul>
    <li><b>Document Categories:</b> Membership, Meeting Minutes, Legal, Financial, Sign-up Sheets, or Other — each document can be linked to a person, a group, or neither.</li>
    <li><b>Database Backup &amp; Restore:</b> Create full WAL-aware SQLite snapshots (manual export, or scheduled daily / weekly / monthly with retention pruning) or export portable JSON archives. Restore replaces the working database from a chosen backup.</li>
    <li><b>Purge Demo Data:</b> Reset all sample/test data with 1 click while safely preserving your core structural Chart of Accounts and Ministry Blueprints.</li>
</ul>

<hr>

<a name="settings"></a>
<h2><img class="header-icon" src="%19"> 13. Settings, Security & RBAC</h2>
<p>Customize OpenEkklesia to your church's governance and visual preferences.</p>

<ul>
    <li><b>Role-Based Access Control (RBAC):</b>
        <table>
            <tr><th>Role</th><th>PIN</th><th>Permissions</th></tr>
            <tr><td><b>Administrator</b></td><td><kbd>1234</kbd></td><td>Full access to every module — People, Shepherd's Workbench, Groups, Services, Planning, Facilities, Inventory, Giving &amp; Ledger, Vault, and Settings.</td></tr>
            <tr><td><b>Pastor</b></td><td><kbd>5555</kbd></td><td>Full access to People, Shepherd's Workbench, Groups, Services, and Care Logs, plus the financial ledger and giving module.</td></tr>
            <tr><td><b>Finance</b></td><td><kbd>7777</kbd></td><td>Full access to contributions, batches, funds, and the double-entry ledger, alongside the other ministry modules.</td></tr>
            <tr><td><b>Guest</b></td><td><kbd>0000</kbd></td><td>Read-only access across the whole application — every Save, Delete, and edit action is disabled.</td></tr>
        </table>
    </li>
    <li><b>Theme Contrast Guardian:</b> Targets WCAG 2.1 AA color-contrast standards across dark and light palettes (with AAA-level contrast on body text where practical).</li>
    <li><b>KPI Card Customizer:</b> Configure 2 to 8 key metrics on your main dashboard for personalized ministry monitoring.</li>
</ul>

<hr>
<p style="text-align: center; color: %3; font-size: 11px; margin-top: 30px;">
    OpenEkklesia Church Management System • Built with C++20, Qt 6 & Lucide Icons • Soli Deo Gloria
</p>

</body>
</html>
)rawhtml")
    .arg(accent, textCol, secText, surface, border, bgBase)
    .arg(iconCompass, iconCommand, iconUsers, iconLayers, iconSparkles, iconNetwork, iconShepherd)
    .arg(iconMusic, iconCalendar, iconLandmark, iconFinance, iconVault, iconSettings, iconAlert)
    .arg(iconTip);
}
