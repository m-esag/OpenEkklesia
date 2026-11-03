#include "setupwizard.h"
#include "appsettings.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>

SetupWizard::SetupWizard(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Welcome to OpenEkklesia");
    setModal(true);
    setMinimumWidth(460);

    QVBoxLayout* lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 24, 24, 20);
    lay->setSpacing(14);

    QLabel* header = new QLabel("Welcome to OpenEkklesia", this);
    header->setObjectName("pageHeader");
    header->setStyleSheet("font-size: 20px; font-weight: bold;");
    lay->addWidget(header);

    QLabel* intro = new QLabel(
        "Tell us about your church. These details appear on giving statements "
        "and reports, and can be changed any time in Settings → General.", this);
    intro->setWordWrap(true);
    intro->setStyleSheet("color: palette(placeholderText); font-size: 13px;");
    lay->addWidget(intro);

    QFormLayout* form = new QFormLayout();
    form->setSpacing(10);

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

    form->addRow("Church Name:", m_txtName);
    form->addRow("Tagline:", m_txtSubtitle);
    form->addRow("Denomination:", m_txtDenomination);
    form->addRow("Address:", m_txtAddress);
    form->addRow("Phone:", m_txtPhone);
    form->addRow("Email:", m_txtEmail);
    form->addRow("Website:", m_txtWebsite);
    form->addRow("Tax ID:", m_txtTaxId);
    lay->addLayout(form);

    lay->addStretch();

    QHBoxLayout* btnRow = new QHBoxLayout();
    QPushButton* btnSkip = new QPushButton("Skip for Now", this);
    btnSkip->setObjectName("secondaryBtn");
    QPushButton* btnSave = new QPushButton("Save && Continue", this);
    btnSave->setObjectName("saveBtn");
    btnSave->setDefault(true);
    btnRow->addWidget(btnSkip);
    btnRow->addStretch();
    btnRow->addWidget(btnSave);
    lay->addLayout(btnRow);

    // Skip marks the profile complete so the wizard never nags again; the
    // giving page still nudges while the actual fields are empty.
    connect(btnSkip, &QPushButton::clicked, this, [this]() {
        AppSettings s;
        s.setValue("profileComplete", true);
        reject();
    });
    connect(btnSave, &QPushButton::clicked, this, [this]() {
        applyProfile();
        accept();
    });
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
