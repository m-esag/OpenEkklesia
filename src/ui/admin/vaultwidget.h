#ifndef VAULTWIDGET_H
#define VAULTWIDGET_H

#include <QWidget>
#include <QTableView>
#include <QSqlQueryModel>
#include <QPushButton>

class VaultWidget : public QWidget {
    Q_OBJECT

public:
    explicit VaultWidget(QWidget *parent = nullptr);
    ~VaultWidget();

    void refreshVault();
    void refreshBoardMinutes();
    void applyRole(const QString& role);

private slots:
    void onUploadClicked();
    void onOpenClicked();
    void onDeleteClicked();
    void onSearchChanged(const QString& text);
    void onAddBoardMinutes();
    void onDeleteMinutes();
    void onExportMinutesPDF();

private:
    void setupUI();
    QString vaultDirPath() const;

    QLineEdit* m_searchEdit;
    QTableView* m_tableView;
    QSqlQueryModel* m_queryModel;

    QTableView* m_minutesView;
    QSqlQueryModel* m_minutesModel;

    QPushButton* m_btnUpload;
    QPushButton* m_btnOpen;
    QPushButton* m_btnDelete;
    QPushButton* m_btnAddMinutes;
    QPushButton* m_btnDeleteMinutes;
    QPushButton* m_btnExportMinutesPDF;
};

#endif // VAULTWIDGET_H
