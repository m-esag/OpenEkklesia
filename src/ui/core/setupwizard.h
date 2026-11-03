#ifndef SETUPWIZARD_H
#define SETUPWIZARD_H

#include <QDialog>

class QLineEdit;

// First-run church profile collection. Shown once by main.cpp before login
// when the settings store has no "profileComplete" key yet. The collected
// details feed ChurchProfile, which prints on giving statements (tax
// receipts) — shipping placeholders here previously meant a church that hit
// "Save All Changes" without editing baked "123 Grace Ave, Springfield" into
// real receipts.
//
// "Skip for Now" records profileComplete=true so the wizard never nags
// again; the giving page still shows a completion nudge while the actual
// fields are empty. All fields are editable later in Settings -> General.
class SetupWizard : public QDialog {
    Q_OBJECT

public:
    explicit SetupWizard(QWidget* parent = nullptr);

    // Writes the profile fields plus "profileComplete" to AppSettings.
    // Public so the test suite can exercise the save path without exec().
    void applyProfile();

private:
    QLineEdit* m_txtName;
    QLineEdit* m_txtSubtitle;
    QLineEdit* m_txtDenomination;
    QLineEdit* m_txtAddress;
    QLineEdit* m_txtPhone;
    QLineEdit* m_txtEmail;
    QLineEdit* m_txtWebsite;
    QLineEdit* m_txtTaxId;
};

#endif // SETUPWIZARD_H
