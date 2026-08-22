/****************************************************************************
** Meta object code from reading C++ file 'contributionswidget.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/ui/finance/contributionswidget.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'contributionswidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN19ContributionsWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto ContributionsWidget::qt_create_metaobjectdata<qt_meta_tag_ZN19ContributionsWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ContributionsWidget",
        "onSaveContribution",
        "",
        "onResetDonationForm",
        "onAddSplitRow",
        "initialAmount",
        "fundId",
        "onRemoveSplitRow",
        "QWidget*",
        "container",
        "onUpdateSplitTotals",
        "onHistoryContextMenu",
        "QPoint",
        "pos",
        "onManageFunds",
        "onExportGnuCashCsv",
        "onNewBatchClicked",
        "onDeleteBatchClicked",
        "onToggleBatchCommit",
        "onBatchDefaultsClicked",
        "onBatchFilterChanged",
        "onBatchSelected",
        "QListWidgetItem*",
        "item",
        "onSaveBatchHeaderDetails"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onSaveContribution'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onResetDonationForm'
        QtMocHelpers::SlotData<void()>(3, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAddSplitRow'
        QtMocHelpers::SlotData<void(double, int)>(4, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Double, 5 }, { QMetaType::Int, 6 },
        }}),
        // Slot 'onAddSplitRow'
        QtMocHelpers::SlotData<void(double)>(4, 2, QMC::AccessPrivate | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::Double, 5 },
        }}),
        // Slot 'onAddSplitRow'
        QtMocHelpers::SlotData<void()>(4, 2, QMC::AccessPrivate | QMC::MethodCloned, QMetaType::Void),
        // Slot 'onRemoveSplitRow'
        QtMocHelpers::SlotData<void(QWidget *)>(7, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 8, 9 },
        }}),
        // Slot 'onUpdateSplitTotals'
        QtMocHelpers::SlotData<void()>(10, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onHistoryContextMenu'
        QtMocHelpers::SlotData<void(const QPoint &)>(11, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 12, 13 },
        }}),
        // Slot 'onManageFunds'
        QtMocHelpers::SlotData<void()>(14, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onExportGnuCashCsv'
        QtMocHelpers::SlotData<void()>(15, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onNewBatchClicked'
        QtMocHelpers::SlotData<void()>(16, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDeleteBatchClicked'
        QtMocHelpers::SlotData<void()>(17, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onToggleBatchCommit'
        QtMocHelpers::SlotData<void()>(18, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onBatchDefaultsClicked'
        QtMocHelpers::SlotData<void()>(19, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onBatchFilterChanged'
        QtMocHelpers::SlotData<void()>(20, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onBatchSelected'
        QtMocHelpers::SlotData<void(QListWidgetItem *)>(21, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 22, 23 },
        }}),
        // Slot 'onSaveBatchHeaderDetails'
        QtMocHelpers::SlotData<void()>(24, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<ContributionsWidget, qt_meta_tag_ZN19ContributionsWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ContributionsWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19ContributionsWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19ContributionsWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN19ContributionsWidgetE_t>.metaTypes,
    nullptr
} };

void ContributionsWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ContributionsWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onSaveContribution(); break;
        case 1: _t->onResetDonationForm(); break;
        case 2: _t->onAddSplitRow((*reinterpret_cast<std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 3: _t->onAddSplitRow((*reinterpret_cast<std::add_pointer_t<double>>(_a[1]))); break;
        case 4: _t->onAddSplitRow(); break;
        case 5: _t->onRemoveSplitRow((*reinterpret_cast<std::add_pointer_t<QWidget*>>(_a[1]))); break;
        case 6: _t->onUpdateSplitTotals(); break;
        case 7: _t->onHistoryContextMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 8: _t->onManageFunds(); break;
        case 9: _t->onExportGnuCashCsv(); break;
        case 10: _t->onNewBatchClicked(); break;
        case 11: _t->onDeleteBatchClicked(); break;
        case 12: _t->onToggleBatchCommit(); break;
        case 13: _t->onBatchDefaultsClicked(); break;
        case 14: _t->onBatchFilterChanged(); break;
        case 15: _t->onBatchSelected((*reinterpret_cast<std::add_pointer_t<QListWidgetItem*>>(_a[1]))); break;
        case 16: _t->onSaveBatchHeaderDetails(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 5:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QWidget* >(); break;
            }
            break;
        }
    }
}

const QMetaObject *ContributionsWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ContributionsWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19ContributionsWidgetE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int ContributionsWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 17)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 17;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 17)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 17;
    }
    return _id;
}
QT_WARNING_POP
