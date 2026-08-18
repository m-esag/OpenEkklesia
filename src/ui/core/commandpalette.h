#ifndef COMMANDPALETTE_H
#define COMMANDPALETTE_H

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>

class CommandPaletteDialog : public QDialog {
    Q_OBJECT

public:
    explicit CommandPaletteDialog(QWidget* parent = nullptr);
    ~CommandPaletteDialog();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onSearchChanged(const QString& text);
    void onItemSelected(QListWidgetItem* item);
    void executeSelection();

private:
    void setupUI();
    void populateItems();
    void addPaletteItem(const QString& text, const QString& type, int id = -1);

    QLineEdit* m_searchEdit;
    QListWidget* m_listWidget;
};

#endif // COMMANDPALETTE_H
