#ifndef CONTRIBUTIONSWIDGET_H
#define CONTRIBUTIONSWIDGET_H

#include <QWidget>
#include <QDateEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QTableView>
#include <QSqlQueryModel>
#include <QKeyEvent>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>

struct FundSplitRow {
    QWidget* container = nullptr;
    QDoubleSpinBox* amountSpin = nullptr;
    QComboBox* fundCombo = nullptr;
    QPushButton* btnRemove = nullptr;
};

class ContributionsWidget : public QWidget {
    Q_OBJECT

public:
    explicit ContributionsWidget(QWidget *parent = nullptr);
    ~ContributionsWidget() override;

    void applyRole(const QString& role);
    void refreshContributions();
    void loadBatch(int batchId);

signals:
    // Emitted after a double-entry ledger transaction has been committed, so
    // the dashboard KPIs (notably Total Assets = SUM of ASSET-account splits)
    // can refresh immediately instead of waiting for the next page visit.
    void ledgerChanged();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onSaveContribution();
    void onResetDonationForm();
    void onAddSplitRow(double initialAmount = 0.0, int fundId = -1);
    void onRemoveSplitRow(QWidget* container);
    void onUpdateSplitTotals();
    void onHistoryContextMenu(const QPoint& pos);
    void onManageFunds();
    void onExportGnuCashCsv();
    void onNewLedgerTransaction();
    void onNewBatchClicked();
    void onDeleteBatchClicked();
    void onToggleBatchCommit();
    void onBatchDefaultsClicked();
    void onBatchFilterChanged();
    void onBatchSelected(QListWidgetItem* item);
    void onSaveBatchHeaderDetails();

private:
    void setupUI();
    void setupModels();
    void refreshHistoryModel();
    void refreshFundCombos();
    void refreshBatchesList();
    void refreshFundPills();
    void applyBatchDefaultsToEntry();
    void updateTabOrder();
    bool isReadOnlyRole() const;

    static constexpr int kAnonymousIndex = 0;

    // --- State ---
    int m_currentBatchId = -1;
    QString m_batchStatus = "In Progress";
    QString m_userRole;
    bool m_roleApplied = false;
    QList<QPair<int, QString>> m_peopleList; // id, name
    QList<QPair<int, QString>> m_fundsList;  // id, name
    QList<FundSplitRow> m_splitRows;

    // --- Left/Main Batch Workspace ---
    // Batch Header Bar
    QLineEdit* m_txtBatchName;
    QDateEdit* m_batchDateEdit;
    QLabel* m_lblBatchStatusBadge;
    QLabel* m_lblBatchMeta;
    QPushButton* m_btnBatchDefaults;
    QPushButton* m_btnDeleteBatch;
    QPushButton* m_btnCommitBatch;

    // Inline Donation Entry Card (Planning Center Style)
    QFrame* m_entryCard;
    QComboBox* m_personCombo;
    QLabel* m_lblTotalHeadline;
    QWidget* m_splitsContainer;
    QVBoxLayout* m_splitsLayout;
    QPushButton* m_btnAddSplit;
    QDateEdit* m_dateEdit;
    QComboBox* m_methodCombo;
    QLineEdit* m_checkNumEdit;
    QLineEdit* m_noteEdit;
    QPushButton* m_btnSave;
    QPushButton* m_btnClearForm;

    // Real-Time Fund Aggregation Pill Strip
    QWidget* m_pillsContainer;
    QHBoxLayout* m_pillsLayout;

    // Batch Donations Ledger Table
    QTableView* m_historyTable;
    QSqlQueryModel* m_historyModel;
    QLabel* m_lblBatchTotalSummary;

    // Footer Actions
    QPushButton* m_btnManageFunds;
    QPushButton* m_btnGnuCashExport;
    QPushButton* m_btnLedgerEntry;

    // --- Right Drawer: Batches Navigation ---
    QPushButton* m_tabInProgress;
    QPushButton* m_tabCommitted;
    QPushButton* m_btnNewBatch;
    QLineEdit* m_txtBatchSearch;
    QListWidget* m_batchesList;
};

#endif // CONTRIBUTIONSWIDGET_H
