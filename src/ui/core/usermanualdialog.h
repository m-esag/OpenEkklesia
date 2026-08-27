#ifndef USERMANUALDIALOG_H
#define USERMANUALDIALOG_H

#include <QDialog>
#include <QString>
#include <QMap>

class QListWidget;
class QListWidgetItem;
class QTextBrowser;
class QLineEdit;
class QPushButton;
class QLabel;

struct ManualTopic {
    QString id;
    QString title;
    QString lucideIcon;
};

/**
 * @brief In-App Interactive User Manual and Help Documentation System for OpenEkklesia.
 * Features searchable topics, clickable internal hyperlinks, keyboard shortcuts,
 * zoom controls, Lucide icon integration, and comprehensive documentation for all platform modules.
 */
class UserManualDialog : public QDialog {
    Q_OBJECT

public:
    explicit UserManualDialog(QWidget* parent = nullptr, const QString& initialSection = QString());
    ~UserManualDialog() override = default;

    static void showManual(QWidget* parent = nullptr, const QString& initialSection = QString());

    // Returns the complete generated HTML user manual documentation with embedded Lucide icons.
    static QString getFullManualHtml();
    static QList<ManualTopic> getTopicList();

    // Helper to generate base64 data URI for embedding Lucide icons into QTextBrowser HTML
    static QString getLucideDataUri(const QString& name, const QColor& color, int size = 20);

private slots:
    void onTopicSelected(QListWidgetItem* item);
    void onSearchTextChanged(const QString& query);
    void onAnchorClicked(const QUrl& url);
    void onZoomIn();
    void onZoomOut();
    void onPrint();

private:
    void setupUI();
    void populateTopics();
    void selectSection(const QString& sectionId);

    QLineEdit* m_searchEdit;
    QListWidget* m_topicList;
    QTextBrowser* m_contentBrowser;
    QPushButton* m_btnBack;
    QPushButton* m_btnForward;
    QPushButton* m_btnHome;
    QPushButton* m_btnZoomIn;
    QPushButton* m_btnZoomOut;
    QPushButton* m_btnPrint;
    QLabel* m_lblStatus;

    int m_currentFontSize;
};

#endif // USERMANUALDIALOG_H
