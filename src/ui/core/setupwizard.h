#ifndef SETUPWIZARD_H
#define SETUPWIZARD_H

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

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
//
// The wizard also hosts the language selector (first form row). It is the
// ONE surface with live retranslation (SPANISH_I18N_BRIEF.md §2a): picking a
// language persists it immediately via AppSettings and retranslates the
// wizard's own labels so the user sees it work before committing. The rest
// of the app applies the language on restart.
class SetupWizard : public QDialog {
    Q_OBJECT

public:
    explicit SetupWizard(QWidget* parent = nullptr);

    // Writes the profile fields plus "profileComplete" to AppSettings.
    // Public so the test suite can exercise the save path without exec().
    void applyProfile();

private:
    // Re-sets every translatable text through AppTranslator::t(). Called on
    // construction and whenever the language combo fires activated().
    void retranslateUi();

    QComboBox* m_cmbLanguage;
    QLabel* m_lblHeader;
    QLabel* m_lblIntro;
    QLabel* m_lblLanguage;
    QLabel* m_lblName;
    QLabel* m_lblTagline;
    QLabel* m_lblDenomination;
    QLabel* m_lblAddress;
    QLabel* m_lblPhone;
    QLabel* m_lblEmail;
    QLabel* m_lblWebsite;
    QLabel* m_lblTaxId;
    QLineEdit* m_txtName;
    QLineEdit* m_txtSubtitle;
    QLineEdit* m_txtDenomination;
    QLineEdit* m_txtAddress;
    QLineEdit* m_txtPhone;
    QLineEdit* m_txtEmail;
    QLineEdit* m_txtWebsite;
    QLineEdit* m_txtTaxId;
    QPushButton* m_btnSkip;
    QPushButton* m_btnSave;
};

#endif // SETUPWIZARD_H
