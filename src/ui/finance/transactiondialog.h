#ifndef TRANSACTIONDIALOG_H
#define TRANSACTIONDIALOG_H

#include <QDialog>
#include <QDateEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QList>
#include <QKeyEvent>

struct SplitRow {
    QWidget* container = nullptr;
    QComboBox* accountCombo = nullptr;
    QDoubleSpinBox* amountSpin = nullptr;
    QComboBox* memberCombo = nullptr;
    QPushButton* btnRemove = nullptr;
};

class TransactionDialog : public QDialog {
    Q_OBJECT

public:
    explicit TransactionDialog(QWidget *parent = nullptr);
    ~TransactionDialog();

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
};

#endif // TRANSACTIONDIALOG_H
