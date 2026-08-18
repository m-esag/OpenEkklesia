#ifndef PASTORALANALYTICSWIDGET_H
#define PASTORALANALYTICSWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QListWidget>
#include <QFrame>
#include <QStringList>
#include <QMouseEvent>
#include <functional>
#include "../core/thememanager.h"

// Helper Clickable Card Frame with Callback
class ClickableCardFrame : public QFrame {
public:
    explicit ClickableCardFrame(QWidget* parent = nullptr) : QFrame(parent) {
        setCursor(Qt::PointingHandCursor);
    }

    std::function<void()> onClick;

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && onClick) {
            onClick();
        }
        QFrame::mousePressEvent(event);
    }
};

class PastoralAnalyticsWidget : public QWidget {
    Q_OBJECT

public:
    explicit PastoralAnalyticsWidget(QWidget *parent = nullptr);
    ~PastoralAnalyticsWidget();

    void refreshAnalytics();
    void applyTheme(const ThemeTokens& tokens);

signals:
    void drillThroughRequested(const QString& filterType, const QString& filterValue);

private slots:
    void onConfigureStagesClicked();
    void onScopeChanged(int index);

private:
    void setupUI();

    // Top Header & Scope Controls
    QLabel* m_lblSubtitle;
    QComboBox* m_cmbScopeSelector;
    QPushButton* m_btnConfigureStages;

    // Top-Level KPI Metric Cards Labels
    QLabel* m_lblTitle1;
    QLabel* m_lblVisitorsVal;
    QLabel* m_lblVisitorsTrend;

    QLabel* m_lblTitle2;
    QLabel* m_lblGroupCoverageVal;
    QLabel* m_lblGroupInverseVal;
    QLabel* m_lblGroupTrend;

    QLabel* m_lblTitle3;
    QLabel* m_lblAtRiskVal;
    QLabel* m_lblAtRiskTrend;

    QLabel* m_lblTitle4;
    QLabel* m_lblConversionVal;
    QLabel* m_lblConversionTrend;

    QLabel* m_lblTitle5;
    QLabel* m_lblServingVal;
    QLabel* m_lblServingTrend;

    // Funnel Pipeline Stage Elements
    QLabel* m_lblFunnelTitle;
    QFrame* m_cardStage1;
    QFrame* m_cardStage2;
    QFrame* m_cardStage3;
    QFrame* m_cardStage4;

    QLabel* m_lblStage1Title;
    QLabel* m_lblStage1Val;
    QLabel* m_lblStage2Title;
    QLabel* m_lblStage2Val;
    QLabel* m_lblStage3Title;
    QLabel* m_lblStage3Val;
    QLabel* m_lblStage4Title;
    QLabel* m_lblStage4Val;

    QLabel* m_lblConv1To2;
    QLabel* m_lblConv2To3;
    QLabel* m_lblConv3To4;

    // Pastoral Actions Needed List
    QLabel* m_lblPastoralActionsTitle;
    QListWidget* m_lstPastoralActions;

    // Configurable Discipleship Stage Names
    QStringList m_stageNames;
};

#endif // PASTORALANALYTICSWIDGET_H
