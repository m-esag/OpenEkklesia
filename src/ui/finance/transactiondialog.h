#ifndef TRANSACTIONDIALOG_H
#define TRANSACTIONDIALOG_H

#include <QDialog>
#include <QDate>
#include <QDateEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QList>
#include <QSqlDatabase>
#include <QKeyEvent>

struct SplitRow {
    QWidget* container = nullptr;
    QComboBox* accountCombo = nullptr;
    QDoubleSpinBox* amountSpin = nullptr;
    QComboBox* memberCombo = nullptr;
    QPushButton* btnRemove = nullptr;
};

// One line of a double-entry journal entry, as consumed by
// TransactionDialog::saveTransaction(). `amount` is in currency units (not
// cents); `personId` identifies the optional donor/member attribution — 0
// means "no person" (stored as NULL).
struct LedgerSplit {
    int accountId = 0;
    double amount = 0.0;
    int personId = 0;
};

class TransactionDialog : public QDialog {
    Q_OBJECT

public:
    explicit TransactionDialog(QWidget *parent = nullptr);
    ~TransactionDialog();

    // Validates and persists one balanced journal entry (one row in
    // `transactions` plus one row per split) inside a single database
    // transaction. Returns false and fills *error (when non-null) on any
    // validation or SQL failure, rolling back so nothing is left half-written.
    // Static and database-injectable so it can be unit-tested without a dialog.
    static bool saveTransaction(QSqlDatabase& db,
                                const QDate& date,
                                const QString& description,
                                const QList<LedgerSplit>& splits,
                                QString* error = nullptr);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onAddSplitRow();
    void onRemoveSplitRow();
    void onValuesChanged();
    void onSaveClicked();

private:
    void setupUI();
    void loadDropdownData();
    void updateBalanceVerification();

    QDateEdit* m_dateEdit;
    QLineEdit* m_descEdit;
    QVBoxLayout* m_splitsLayout;
    QLabel* m_lblBalance;
    QPushButton* m_btnSave;

    QList<SplitRow> m_splitRows;
    QList<QPair<int, QString>> m_accountsList; // id, name
    QList<QPair<int, QString>> m_membersList;  // id, full_name
    bool m_saving = false; // save guard: set while saving, stays set once committed
};

#endif // TRANSACTIONDIALOG_H
