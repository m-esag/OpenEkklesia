#ifndef VAULTWIDGET_H
#define VAULTWIDGET_H

#include <QWidget>
#include <QTableView>
#include <QSqlQueryModel>
#include <QPushButton>
#include <QTimer>

class VaultWidget : public QWidget {
    Q_OBJECT

public:
    explicit VaultWidget(QWidget *parent = nullptr);
    ~VaultWidget();

    void refreshVault();
    void refreshBoardMinutes();
    void applyRole(const QString& role);

    // Canonical vault directory: derived from the database location so the
    // documents always live under the same data root as the DB. Adopts the
    // old lowercase pre-refactor location on first call and keeps the
    // directory private (0700). Shared with PeopleWidget's document section.
    static QString vaultDirPath();
    // Resolves a stored file_path against the vault directory and enforces
    // containment: returns the absolute path only if it stays inside the
    // vault; empty string otherwise. Guards against tampered DB rows
    // redirecting open/export/delete outside the vault (audit oe-003).
    static QString containedVaultPath(const QString& storedPath);

private slots:
    void onUploadClicked();
    void onOpenClicked();
    void onDeleteClicked();
    void onSearchChanged(const QString& text);
    void doVaultSearch(const QString& text);
    void onSweepOrphans();
    void onAddBoardMinutes();
    void onDeleteMinutes();
    void onExportMinutesPDF();

private:
    void setupUI();

    QLineEdit* m_searchEdit;
    QTimer* m_searchTimer = nullptr;
    QString m_pendingSearch;
    QTableView* m_tableView;
    QSqlQueryModel* m_queryModel;

    QTableView* m_minutesView;
    QSqlQueryModel* m_minutesModel;

    QPushButton* m_btnUpload;
    QPushButton* m_btnOpen;
    QPushButton* m_btnDelete;
    QPushButton* m_btnSweep;
    QPushButton* m_btnAddMinutes;
    QPushButton* m_btnDeleteMinutes;
    QPushButton* m_btnExportMinutesPDF;

    QString m_userRole;
};

#endif // VAULTWIDGET_H
