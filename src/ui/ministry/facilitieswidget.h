#ifndef FACILITIESWIDGET_H
#define FACILITIESWIDGET_H

#include <QWidget>
#include <QTableView>
#include <QSqlQueryModel>
#include <QPushButton>
#include <QDateEdit>
#include <QComboBox>
#include <QLineEdit>
#include <QTimeEdit>

class FacilitiesWidget : public QWidget {
    Q_OBJECT

public:
    explicit FacilitiesWidget(QWidget *parent = nullptr);
    ~FacilitiesWidget();

private slots:
    void onBookFacility();
    void onDeleteReservation();
    void onDateFilterChanged(const QDate& date);

private:
    void setupUI();
    void refreshReservations();

    QDateEdit* m_dateFilter;
    QTableView* m_table;
    QSqlQueryModel* m_model;

    QPushButton* m_btnBook;
    QPushButton* m_btnDelete;
};

#endif // FACILITIESWIDGET_H
