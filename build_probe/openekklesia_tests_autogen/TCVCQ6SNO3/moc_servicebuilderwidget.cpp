/****************************************************************************
** Meta object code from reading C++ file 'servicebuilderwidget.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/ui/ministry/servicebuilderwidget.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'servicebuilderwidget.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN20ServiceBuilderWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto ServiceBuilderWidget::qt_create_metaobjectdata<qt_meta_tag_ZN20ServiceBuilderWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ServiceBuilderWidget",
        "onSaveService",
        "",
        "onLoadService",
        "onClearPlaylist",
        "onRemoveItem",
        "onScheduleVolunteer",
        "onUnscheduleVolunteer",
        "onServiceDateChanged",
        "date",
        "onExportRunSheet",
        "updateDurationCalculator",
        "onSaveServiceDetails",
        "onAddElement",
        "onEditElement",
        "QListWidgetItem*",
        "item",
        "onRemoveElement",
        "onSaveTemplate",
        "onApplyTemplate"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onSaveService'
        QtMocHelpers::SlotData<void()>(1, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onLoadService'
        QtMocHelpers::SlotData<void()>(3, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onClearPlaylist'
        QtMocHelpers::SlotData<void()>(4, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRemoveItem'
        QtMocHelpers::SlotData<void()>(5, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onScheduleVolunteer'
        QtMocHelpers::SlotData<void()>(6, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onUnscheduleVolunteer'
        QtMocHelpers::SlotData<void()>(7, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onServiceDateChanged'
        QtMocHelpers::SlotData<void(const QDate &)>(8, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QDate, 9 },
        }}),
        // Slot 'onExportRunSheet'
        QtMocHelpers::SlotData<void()>(10, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'updateDurationCalculator'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSaveServiceDetails'
        QtMocHelpers::SlotData<void()>(12, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAddElement'
        QtMocHelpers::SlotData<void()>(13, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onEditElement'
        QtMocHelpers::SlotData<void(QListWidgetItem *)>(14, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 15, 16 },
        }}),
        // Slot 'onRemoveElement'
        QtMocHelpers::SlotData<void()>(17, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSaveTemplate'
        QtMocHelpers::SlotData<void()>(18, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onApplyTemplate'
        QtMocHelpers::SlotData<void()>(19, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<ServiceBuilderWidget, qt_meta_tag_ZN20ServiceBuilderWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ServiceBuilderWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN20ServiceBuilderWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN20ServiceBuilderWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN20ServiceBuilderWidgetE_t>.metaTypes,
    nullptr
} };

void ServiceBuilderWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ServiceBuilderWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onSaveService(); break;
        case 1: _t->onLoadService(); break;
        case 2: _t->onClearPlaylist(); break;
        case 3: _t->onRemoveItem(); break;
        case 4: _t->onScheduleVolunteer(); break;
        case 5: _t->onUnscheduleVolunteer(); break;
        case 6: _t->onServiceDateChanged((*reinterpret_cast<std::add_pointer_t<QDate>>(_a[1]))); break;
        case 7: _t->onExportRunSheet(); break;
        case 8: _t->updateDurationCalculator(); break;
        case 9: _t->onSaveServiceDetails(); break;
        case 10: _t->onAddElement(); break;
        case 11: _t->onEditElement((*reinterpret_cast<std::add_pointer_t<QListWidgetItem*>>(_a[1]))); break;
        case 12: _t->onRemoveElement(); break;
        case 13: _t->onSaveTemplate(); break;
        case 14: _t->onApplyTemplate(); break;
        default: ;
        }
    }
}

const QMetaObject *ServiceBuilderWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ServiceBuilderWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN20ServiceBuilderWidgetE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int ServiceBuilderWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 15)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 15;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 15)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 15;
    }
    return _id;
}
QT_WARNING_POP
