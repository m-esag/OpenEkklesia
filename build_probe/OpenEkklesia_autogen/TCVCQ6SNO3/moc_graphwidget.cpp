/****************************************************************************
** Meta object code from reading C++ file 'graphwidget.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/ui/ministry/graphwidget.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'graphwidget.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN15GraphCanvasViewE_t {};
} // unnamed namespace

template <> constexpr inline auto GraphCanvasView::qt_create_metaobjectdata<qt_meta_tag_ZN15GraphCanvasViewE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "GraphCanvasView"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<GraphCanvasView, qt_meta_tag_ZN15GraphCanvasViewE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject GraphCanvasView::staticMetaObject = { {
    QMetaObject::SuperData::link<QGraphicsView::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15GraphCanvasViewE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15GraphCanvasViewE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN15GraphCanvasViewE_t>.metaTypes,
    nullptr
} };

void GraphCanvasView::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<GraphCanvasView *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *GraphCanvasView::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *GraphCanvasView::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15GraphCanvasViewE_t>.strings))
        return static_cast<void*>(this);
    return QGraphicsView::qt_metacast(_clname);
}

int GraphCanvasView::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QGraphicsView::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN11GraphWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto GraphWidget::qt_create_metaobjectdata<qt_meta_tag_ZN11GraphWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "GraphWidget",
        "navigateToProfile",
        "",
        "entityType",
        "entityId",
        "onZoomIn",
        "onZoomOut",
        "onZoomReset",
        "updatePhysics",
        "onFilterToggled",
        "onPathfindToggle",
        "onClearPathfind",
        "onSaveNotes",
        "onCareFlagChanged",
        "index",
        "onExportPNG",
        "onRunAudit",
        "onGraphSearchChanged",
        "text",
        "onExportJSON",
        "onToggleLegend",
        "onToggleLayoutMode",
        "onSpotlightIsolatedClicked",
        "onToggleDataHygiene",
        "onToggleCollapseDock",
        "onSeverSelectedEdge",
        "onEdgeRoleChanged",
        "onRosterItemClicked",
        "QListWidgetItem*",
        "item",
        "onCopyContactInfo",
        "NodeItem*",
        "node",
        "type",
        "onQuickAssignGroup",
        "onToggleSelectMode",
        "onMinistryBranchChanged",
        "onSceneSelectionChanged",
        "onBatchCopyEmails",
        "onBatchCopyPhones",
        "onBatchExportCSV",
        "onBatchAssignGroup",
        "onConnectSmartBridge",
        "personId",
        "groupId",
        "role",
        "onGroupSuggest",
        "onLogContact",
        "onUndoSever",
        "onToggleStatusCluster"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'navigateToProfile'
        QtMocHelpers::SignalData<void(const QString &, int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 }, { QMetaType::Int, 4 },
        }}),
        // Slot 'onZoomIn'
        QtMocHelpers::SlotData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onZoomOut'
        QtMocHelpers::SlotData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onZoomReset'
        QtMocHelpers::SlotData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'updatePhysics'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFilterToggled'
        QtMocHelpers::SlotData<void()>(9, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPathfindToggle'
        QtMocHelpers::SlotData<void()>(10, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onClearPathfind'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSaveNotes'
        QtMocHelpers::SlotData<void()>(12, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onCareFlagChanged'
        QtMocHelpers::SlotData<void(int)>(13, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 14 },
        }}),
        // Slot 'onExportPNG'
        QtMocHelpers::SlotData<void()>(15, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRunAudit'
        QtMocHelpers::SlotData<void()>(16, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onGraphSearchChanged'
        QtMocHelpers::SlotData<void(const QString &)>(17, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 18 },
        }}),
        // Slot 'onExportJSON'
        QtMocHelpers::SlotData<void()>(19, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onToggleLegend'
        QtMocHelpers::SlotData<void()>(20, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onToggleLayoutMode'
        QtMocHelpers::SlotData<void()>(21, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSpotlightIsolatedClicked'
        QtMocHelpers::SlotData<void()>(22, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onToggleDataHygiene'
        QtMocHelpers::SlotData<void()>(23, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onToggleCollapseDock'
        QtMocHelpers::SlotData<void()>(24, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSeverSelectedEdge'
        QtMocHelpers::SlotData<void()>(25, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onEdgeRoleChanged'
        QtMocHelpers::SlotData<void(int)>(26, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 14 },
        }}),
        // Slot 'onRosterItemClicked'
        QtMocHelpers::SlotData<void(QListWidgetItem *)>(27, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 28, 29 },
        }}),
        // Slot 'onCopyContactInfo'
        QtMocHelpers::SlotData<void(NodeItem *, const QString &)>(30, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 31, 32 }, { QMetaType::QString, 33 },
        }}),
        // Slot 'onQuickAssignGroup'
        QtMocHelpers::SlotData<void(NodeItem *)>(34, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 31, 32 },
        }}),
        // Slot 'onToggleSelectMode'
        QtMocHelpers::SlotData<void()>(35, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onMinistryBranchChanged'
        QtMocHelpers::SlotData<void(int)>(36, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 14 },
        }}),
        // Slot 'onSceneSelectionChanged'
        QtMocHelpers::SlotData<void()>(37, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onBatchCopyEmails'
        QtMocHelpers::SlotData<void()>(38, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onBatchCopyPhones'
        QtMocHelpers::SlotData<void()>(39, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onBatchExportCSV'
        QtMocHelpers::SlotData<void()>(40, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onBatchAssignGroup'
        QtMocHelpers::SlotData<void()>(41, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onConnectSmartBridge'
        QtMocHelpers::SlotData<void(int, int, const QString &)>(42, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 43 }, { QMetaType::Int, 44 }, { QMetaType::QString, 45 },
        }}),
        // Slot 'onGroupSuggest'
        QtMocHelpers::SlotData<void()>(46, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onLogContact'
        QtMocHelpers::SlotData<void()>(47, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onUndoSever'
        QtMocHelpers::SlotData<void()>(48, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onToggleStatusCluster'
        QtMocHelpers::SlotData<void()>(49, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<GraphWidget, qt_meta_tag_ZN11GraphWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject GraphWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11GraphWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11GraphWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN11GraphWidgetE_t>.metaTypes,
    nullptr
} };

void GraphWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<GraphWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->navigateToProfile((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 1: _t->onZoomIn(); break;
        case 2: _t->onZoomOut(); break;
        case 3: _t->onZoomReset(); break;
        case 4: _t->updatePhysics(); break;
        case 5: _t->onFilterToggled(); break;
        case 6: _t->onPathfindToggle(); break;
        case 7: _t->onClearPathfind(); break;
        case 8: _t->onSaveNotes(); break;
        case 9: _t->onCareFlagChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 10: _t->onExportPNG(); break;
        case 11: _t->onRunAudit(); break;
        case 12: _t->onGraphSearchChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 13: _t->onExportJSON(); break;
        case 14: _t->onToggleLegend(); break;
        case 15: _t->onToggleLayoutMode(); break;
        case 16: _t->onSpotlightIsolatedClicked(); break;
        case 17: _t->onToggleDataHygiene(); break;
        case 18: _t->onToggleCollapseDock(); break;
        case 19: _t->onSeverSelectedEdge(); break;
        case 20: _t->onEdgeRoleChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 21: _t->onRosterItemClicked((*reinterpret_cast<std::add_pointer_t<QListWidgetItem*>>(_a[1]))); break;
        case 22: _t->onCopyContactInfo((*reinterpret_cast<std::add_pointer_t<NodeItem*>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 23: _t->onQuickAssignGroup((*reinterpret_cast<std::add_pointer_t<NodeItem*>>(_a[1]))); break;
        case 24: _t->onToggleSelectMode(); break;
        case 25: _t->onMinistryBranchChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 26: _t->onSceneSelectionChanged(); break;
        case 27: _t->onBatchCopyEmails(); break;
        case 28: _t->onBatchCopyPhones(); break;
        case 29: _t->onBatchExportCSV(); break;
        case 30: _t->onBatchAssignGroup(); break;
        case 31: _t->onConnectSmartBridge((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 32: _t->onGroupSuggest(); break;
        case 33: _t->onLogContact(); break;
        case 34: _t->onUndoSever(); break;
        case 35: _t->onToggleStatusCluster(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (GraphWidget::*)(const QString & , int )>(_a, &GraphWidget::navigateToProfile, 0))
            return;
    }
}

const QMetaObject *GraphWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *GraphWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11GraphWidgetE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int GraphWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 36)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 36;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 36)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 36;
    }
    return _id;
}

// SIGNAL 0
void GraphWidget::navigateToProfile(const QString & _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}
QT_WARNING_POP
