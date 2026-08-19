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
#include <QProgressBar>
#include <QPushButton>

class ContributionsWidget : public QWidget {
    Q_OBJECT

public:
    explicit ContributionsWidget(QWidget *parent = nullptr);
    ~ContributionsWidget();

    void applyRole(const QString& role);
    void refreshContributions();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onSaveContribution();
    void onPaymentMethodChanged(int index);
    void onExportTaxStatements();
    void onHistoryContextMenu(const QPoint& pos);

private:
    void setupUI();
    void setupModels();
    void refreshHistoryModel();

    // Inputs (Left Panel)
    QDateEdit* m_dateEdit;
    QComboBox* m_personCombo;
    QDoubleSpinBox* m_amountSpin;
    QComboBox* m_fundCombo;
    QComboBox* m_paymentCombo;
    QLineEdit* m_checkNumberEdit;
    QPushButton* m_btnSave;
    QPushButton* m_btnExportPDF;
    QString m_userRole;
    
    // History Table (Right Panel)
    QTableView* m_historyTable;
    QSqlQueryModel* m_historyModel;

    // Actions & Progress
    QProgressBar* m_progressBar;

    // Database structures
    QList<QPair<int, QString>> m_peopleList; // id, name
    QList<QPair<int, QString>> m_fundsList;  // id, name
};

#endif // CONTRIBUTIONSWIDGET_H
