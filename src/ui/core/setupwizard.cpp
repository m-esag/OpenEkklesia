#include "setupwizard.h"
#include "appsettings.h"
#include "apptranslator.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>

SetupWizard::SetupWizard(QWidget* parent)
    : QDialog(parent)
{
    setModal(true);
    setMinimumWidth(460);

    QVBoxLayout* lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 24, 24, 20);
    lay->setSpacing(14);

    m_lblHeader = new QLabel(this);
    m_lblHeader->setObjectName("pageHeader");
    m_lblHeader->setStyleSheet("font-size: 20px; font-weight: bold;");
    lay->addWidget(m_lblHeader);

    m_lblIntro = new QLabel(this);
    m_lblIntro->setWordWrap(true);
    m_lblIntro->setStyleSheet("color: palette(placeholderText); font-size: 13px;");
    lay->addWidget(m_lblIntro);

    QFormLayout* form = new QFormLayout();
    form->setSpacing(10);

    // Language first (SPANISH_I18N_BRIEF.md §2a): entries display in their
    // own language — never through t() — and the choice persists immediately
    // so the rest of the wizard and the whole app (after restart) follow it.
    m_lblLanguage = new QLabel(this);
    m_cmbLanguage = new QComboBox(this);
    m_cmbLanguage->addItem("English", "en");
    m_cmbLanguage->addItem("Español", "es");
    const int savedIdx = m_cmbLanguage->findData(
        AppSettings().value("ui/language", "en").toString());
    m_cmbLanguage->setCurrentIndex(savedIdx < 0 ? 0 : savedIdx);
    connect(m_cmbLanguage, &QComboBox::activated, this, [this](int index) {
        const QString code = m_cmbLanguage->itemData(index).toString();
        AppSettings s;
        s.setValue("ui/language", code);
        AppTranslator::instance().setLanguage(code);
        retranslateUi();
    });
    form->addRow(m_lblLanguage, m_cmbLanguage);

    auto makeField = [this](const QString& placeholder) {
        QLineEdit* edit = new QLineEdit(this);
        edit->setPlaceholderText(placeholder);
        edit->setMinimumHeight(30);
        return edit;
    };
    m_txtName = makeField("My Church");
    m_txtSubtitle = makeField("Optional tagline");
    m_txtDenomination = makeField("Optional");
    m_txtAddress = makeField("Street, City, State ZIP");
    m_txtPhone = makeField("Optional");
    m_txtEmail = makeField("Optional");
    m_txtWebsite = makeField("Optional");
    m_txtTaxId = makeField("Shown on giving statements (EIN)");

    // Row labels are explicit widgets so retranslateUi() can reach them.
    m_lblName = new QLabel(this);
    m_lblTagline = new QLabel(this);
    m_lblDenomination = new QLabel(this);
    m_lblAddress = new QLabel(this);
    m_lblPhone = new QLabel(this);
    m_lblEmail = new QLabel(this);
    m_lblWebsite = new QLabel(this);
    m_lblTaxId = new QLabel(this);
    form->addRow(m_lblName, m_txtName);
    form->addRow(m_lblTagline, m_txtSubtitle);
    form->addRow(m_lblDenomination, m_txtDenomination);
    form->addRow(m_lblAddress, m_txtAddress);
    form->addRow(m_lblPhone, m_txtPhone);
    form->addRow(m_lblEmail, m_txtEmail);
    form->addRow(m_lblWebsite, m_txtWebsite);
    form->addRow(m_lblTaxId, m_txtTaxId);
    lay->addLayout(form);

    lay->addStretch();

    QHBoxLayout* btnRow = new QHBoxLayout();
    m_btnSkip = new QPushButton(this);
    m_btnSkip->setObjectName("secondaryBtn");
    m_btnSave = new QPushButton(this);
    m_btnSave->setObjectName("saveBtn");
    m_btnSave->setDefault(true);
    btnRow->addWidget(m_btnSkip);
    btnRow->addStretch();
    btnRow->addWidget(m_btnSave);
    lay->addLayout(btnRow);

    // Skip marks the profile complete so the wizard never nags again; the
    // giving page still nudges while the actual fields are empty.
    connect(m_btnSkip, &QPushButton::clicked, this, [this]() {
        AppSettings s;
        s.setValue("profileComplete", true);
        reject();
    });
    connect(m_btnSave, &QPushButton::clicked, this, [this]() {
        applyProfile();
        accept();
    });

    retranslateUi();
}

void SetupWizard::retranslateUi()
{
    using AT = AppTranslator;
    setWindowTitle(AT::t("Welcome to OpenEkklesia"));
    m_lblHeader->setText(AT::t("Welcome to OpenEkklesia"));
    m_lblIntro->setText(AT::t(
        "Tell us about your church. These details appear on giving statements "
        "and reports, and can be changed any time in Settings → General."));
    m_lblLanguage->setText(AT::t("Language:"));
    m_lblName->setText(AT::t("Church Name:"));
    m_lblTagline->setText(AT::t("Tagline:"));
    m_lblDenomination->setText(AT::t("Denomination:"));
    m_lblAddress->setText(AT::t("Address:"));
    m_lblPhone->setText(AT::t("Phone:"));
    m_lblEmail->setText(AT::t("Email:"));
    m_lblWebsite->setText(AT::t("Website:"));
    m_lblTaxId->setText(AT::t("Tax ID:"));
    m_txtName->setPlaceholderText(AT::t("My Church"));
    m_txtSubtitle->setPlaceholderText(AT::t("Optional tagline"));
    m_txtDenomination->setPlaceholderText(AT::t("Optional"));
    m_txtAddress->setPlaceholderText(AT::t("Street, City, State ZIP"));
    m_txtPhone->setPlaceholderText(AT::t("Optional"));
    m_txtEmail->setPlaceholderText(AT::t("Optional"));
    m_txtWebsite->setPlaceholderText(AT::t("Optional"));
    m_txtTaxId->setPlaceholderText(AT::t("Shown on giving statements (EIN)"));
    m_btnSkip->setText(AT::t("Skip for Now"));
    m_btnSave->setText(AT::t("Save && Continue"));
}

void SetupWizard::applyProfile()
{
    AppSettings s;
    const QString name = m_txtName->text().trimmed();
    s.setValue("churchName", name.isEmpty() ? "My Church" : name);
    s.setValue("churchSubtitle", m_txtSubtitle->text().trimmed());
    s.setValue("churchDenomination", m_txtDenomination->text().trimmed());
    s.setValue("churchAddress", m_txtAddress->text().trimmed());
    s.setValue("churchPhone", m_txtPhone->text().trimmed());
    s.setValue("churchEmail", m_txtEmail->text().trimmed());
    s.setValue("churchWebsite", m_txtWebsite->text().trimmed());
    s.setValue("churchTaxId", m_txtTaxId->text().trimmed());
    s.setValue("profileComplete", true);
}
