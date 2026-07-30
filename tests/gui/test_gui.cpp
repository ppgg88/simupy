// Smoke tests for the interface, driven offscreen.
//
// These exercise the parts of the GUI that carry real logic — hierarchy
// navigation, block insertion, wiring and routing — by driving the same
// window the application builds. Rendering itself is not checked; the point
// is that the editing operations do what they claim to the model underneath.

#include "model/BlockRegistry.h"
#include "io/CustomBlock.h"
#include "blocks/ControlBlocks.h"
#include "blocks/SubsystemBlock.h"
#include "app/gui/style/AppIcons.h"
#include "app/gui/canvas/BlockItem.h"
#include "app/gui/panels/ControlPanel.h"
#include "app/gui/canvas/DiagramScene.h"
#include "app/gui/canvas/DiagramView.h"
#include "app/gui/MainWindow.h"
#include "app/gui/canvas/QuickAddPopup.h"
#include "app/gui/dialogs/ScopeWindow.h"
#include "app/gui/canvas/WireItem.h"
#include "app/gui/canvas/WireRouter.h"
#include "io/ModelSerializer.h"
#include "scripting/PythonEngine.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QPushButton>
#include <QKeyEvent>
#include <QLineEdit>
#include <QElapsedTimer>
#include <QListWidget>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <cmath>

#include <iostream>
#include <string>

using namespace simupy;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (condition) return;
    ++g_failures;
    std::cout << "  FAIL  " << what << '\n';
}

void checkClose(double actual, double expected, const std::string& what) {
    ++g_checks;
    if (std::abs(actual - expected) <= 1e-6) return;
    ++g_failures;
    std::cout << "  FAIL  " << what << "\n        expected " << expected
              << ", got " << actual << '\n';
}

void beginTest(const std::string& name) {
    std::cout << "- " << name << std::endl;
}

BlockItem* itemNamed(DiagramScene* scene, const QString& name) {
    for (QGraphicsItem* item : scene->items()) {
        if (item->type() != BlockItem::Type) continue;
        auto* block = static_cast<BlockItem*>(item);
        if (QString::fromStdString(block->block()->name()) == name)
            return block;
    }
    return nullptr;
}

/// Opening a subsystem swaps the canvas to its contents, and leaving brings
/// the parent diagram back.
void testSubsystemNavigation(const QString& modelPath) {
    beginTest("Navigating into and out of a subsystem");

    MainWindow window;
    if (!window.openFile(modelPath)) {
        check(false, "the example model opened");
        return;
    }

    check(window.currentPath() == QStringLiteral("model"),
          "a freshly opened model starts at the root");

    BlockItem* subsystem = itemNamed(window.scene(), QStringLiteral("PI Controller"));
    check(subsystem != nullptr, "the subsystem block is on the canvas");
    if (!subsystem) return;

    const int topLevelBlocks = window.scene()->items().size();

    window.scene()->clearSelection();
    subsystem->setSelected(true);
    window.openSelectedSubsystem();

    check(window.currentPath() ==
              QStringLiteral("model / PI Controller"),
          "the breadcrumb follows us in (got \"" +
              window.currentPath().toStdString() + "\")");
    check(itemNamed(window.scene(), QStringLiteral("Integral")) != nullptr,
          "the canvas now shows the subsystem's own blocks");
    check(itemNamed(window.scene(), QStringLiteral("Setpoint")) == nullptr,
          "and no longer shows the parent's");

    window.leaveSubsystem();

    check(window.currentPath() == QStringLiteral("model"),
          "leaving returns to the root");
    check(window.scene()->items().size() == topLevelBlocks,
          "with the same items as before");
    check(itemNamed(window.scene(), QStringLiteral("Setpoint")) != nullptr,
          "and the parent's blocks are back");
}

/// Asking to open something that is not a subsystem must be refused quietly.
void testOpenNonSubsystem(const QString& modelPath) {
    beginTest("Only subsystems can be opened");

    MainWindow window;
    if (!window.openFile(modelPath)) return;

    BlockItem* plant = itemNamed(window.scene(), QStringLiteral("Plant"));
    check(plant != nullptr, "the plant block is on the canvas");
    if (!plant) return;

    window.scene()->clearSelection();
    plant->setSelected(true);
    window.openSelectedSubsystem();

    check(window.currentPath() == QStringLiteral("model"),
          "the canvas stays where it was");
}

/// Dropping a block and wiring it must change the model, not just the view.
void testAddBlockAndWire() {
    beginTest("Adding and wiring blocks edits the model");

    Model model;
    DiagramScene scene(model);

    BlockItem* source = scene.addBlock(QStringLiteral("Constant"), QPointF(0, 0));
    BlockItem* sink = scene.addBlock(QStringLiteral("Scope"), QPointF(300, 0));

    check(source != nullptr && sink != nullptr, "both blocks were created");
    check(model.blocks().size() == 2, "the model holds them");
    if (!source || !sink) return;

    model.connect(source->block()->id(), 0, sink->block()->id(), 0);
    scene.rebuild();

    check(model.connections().size() == 1, "the wire reached the model");
    check(scene.items().size() == 3, "the scene shows two blocks and a wire");

    // Deleting the source must take its wire with it.
    scene.clearSelection();
    if (BlockItem* item = scene.itemForBlock(
            QString::fromStdString(source->block()->id())))
        item->setSelected(true);
    scene.deleteSelection();

    check(model.blocks().size() == 1, "the block is gone");
    check(model.connections().empty(), "and so is the wire that fed it");
}

/// The loop the feature exists for: build a subsystem, save it as a library
/// block, then drop that block back on a canvas from the palette.
void testCustomBlockLoop() {
    beginTest("A saved subsystem comes back as a palette block");

    // Build the thing worth saving: a subsystem whose gain is bound to a mask
    // parameter rather than typed in.
    Model workshop;
    DiagramScene scene(workshop);
    BlockItem* holder = scene.addBlock(QStringLiteral("Subsystem"), QPointF(0, 0));
    check(holder != nullptr, "the subsystem was created");
    if (!holder) return;

    Model& inner = dynamic_cast<SubsystemBlock*>(holder->block())->contents();
    Block* in = inner.addBlock("Inport", 0, 0);
    in->params().set("portNumber", 1.0);
    Block* gain = inner.addBlock("Gain", 150, 0);
    gain->setParamExpression("gain", "[k]");
    Block* out = inner.addBlock("Outport", 300, 0);
    out->params().set("portNumber", 1.0);
    inner.connect(in->id(), 0, gain->id(), 0);
    inner.connect(gain->id(), 0, out->id(), 0);

    CustomBlockDef def;
    def.name = "GuiTestGain";
    def.displayName = "Test gain";
    def.category = "GUI Test";
    ParamSpec spec;
    spec.name = "k";
    spec.label = "Gain";
    spec.kind = ParamSpec::Kind::Real;
    spec.defaultValue = 2.0;
    def.params.push_back(spec);
    captureBlockDefinition(*holder->block(), def);

    registerCustomBlock(def);

    const BlockType* registered = BlockRegistry::instance().find("GuiTestGain");
    check(registered != nullptr, "the type reaches the registry");
    if (!registered) return;
    check(registered->category == "GUI Test", "under its own category");
    check(registered->params.size() == 1, "carrying its mask");

    // Now use it the way the palette does.
    Model reuse;
    DiagramScene canvas(reuse);
    BlockItem* dropped =
        canvas.addBlock(QStringLiteral("GuiTestGain"), QPointF(0, 0));
    check(dropped != nullptr, "the palette can drop it on a canvas");
    if (!dropped) return;

    checkClose(dropped->block()->params().real("k"), 2.0,
               "the mask default arrives with it");

    const auto* copy = dynamic_cast<const SubsystemBlock*>(dropped->block());
    check(copy != nullptr, "it is a subsystem underneath");
    check(copy && copy->contents().blocks().size() == 3,
          "with the saved diagram inside");
    check(dropped->block()->ports().inputs.size() == 1 &&
              dropped->block()->ports().outputs.size() == 1,
          "and the ports the Inport/Outport blocks define");

    BlockRegistry::instance().unregisterType("GuiTestGain");
}

/// Naming a signal must label every branch of it, because they all carry the
/// same thing.
void testSignalNameFollowsEveryBranch() {
    beginTest("Naming a signal labels all its branches");

    Model model;
    DiagramScene scene(model);

    BlockItem* source = scene.addBlock(QStringLiteral("Constant"), QPointF(0, 0));
    BlockItem* firstSink = scene.addBlock(QStringLiteral("Scope"), QPointF(300, 0));
    BlockItem* secondSink =
        scene.addBlock(QStringLiteral("Gain"), QPointF(300, 200));
    if (!source || !firstSink || !secondSink) {
        check(false, "the blocks were created");
        return;
    }

    // One output driving two inputs: a fan-out, not two signals.
    const std::string sourceId = source->block()->id();
    model.connect(sourceId, 0, firstSink->block()->id(), 0);
    model.connect(sourceId, 0, secondSink->block()->id(), 0);

    // rebuild() destroys every item, so nothing captured above survives it.
    // The model is what persists; look items up again after each rebuild.
    scene.rebuild();

    auto wiresOnCanvas = [&scene] {
        QList<WireItem*> found;
        for (QGraphicsItem* item : scene.items())
            if (item->type() == WireItem::Type)
                found.append(static_cast<WireItem*>(item));
        return found;
    };

    QList<WireItem*> wires = wiresOnCanvas();
    check(wires.size() == 2, "both branches are on the canvas");
    if (wires.size() != 2) return;

    scene.renameSignal(wires.first(), QStringLiteral("reference"));

    check(wires[0]->signalName() == QStringLiteral("reference") &&
              wires[1]->signalName() == QStringLiteral("reference"),
          "naming one branch labels the other too");
    check(model.block(sourceId)->signalName(0) == "reference",
          "and the name is stored on the producing port");

    // Rebuilding the scene must not lose it: the name lives in the model.
    scene.rebuild();
    wires = wiresOnCanvas();
    for (WireItem* wire : wires)
        check(wire->signalName() == QStringLiteral("reference"),
              "and survives a rebuild");

    if (wires.isEmpty()) return;
    scene.renameSignal(wires.first(), QString());
    check(model.block(sourceId)->signalName(0).empty(),
          "an empty name clears the label");
}

/// Typing on the canvas has to put the block you meant at the top, or the
/// feature is slower than reaching for the palette.
void testQuickAddRanking() {
    beginTest("Quick-add ranks the obvious match first");

    auto first = [](const QString& query) {
        const QStringList found = QuickAddPopup::rankBlockTypes(query);
        return found.isEmpty() ? QString() : found.first();
    };
    auto contains = [](const QString& query, const QString& typeName) {
        return QuickAddPopup::rankBlockTypes(query).contains(typeName);
    };

    check(first(QStringLiteral("gain")) == QStringLiteral("Gain"),
          "an exact name wins");
    check(first(QStringLiteral("gai")) == QStringLiteral("Gain"),
          "a prefix wins over anything mentioning 'gain' in prose");
    check(first(QStringLiteral("integ")) == QStringLiteral("Integrator"),
          "a prefix beats the discrete variant");
    check(first(QStringLiteral("SUM")) == QStringLiteral("Sum"),
          "matching ignores case");

    // A word boundary inside the name: the point of this tier is that the
    // useful match is rarely the one starting at character zero.
    check(first(QStringLiteral("transfer")) == QStringLiteral("TransferFcn"),
          "an inner word matches");
    check(contains(QStringLiteral("transfer"),
                   QStringLiteral("DiscreteTransferFcn")),
          "and finds the discrete one too");

    // Subsequence, so nobody has to type the whole name.
    check(contains(QStringLiteral("tffcn"), QStringLiteral("TransferFcn")),
          "a subsequence still matches");

    check(QuickAddPopup::rankBlockTypes(QStringLiteral("zzzqqq")).isEmpty(),
          "nonsense matches nothing");
    check(QuickAddPopup::rankBlockTypes(QString()).size() > 20,
          "an empty query offers everything");

    // The limit has to bite, or a long list would make the popup useless.
    check(QuickAddPopup::rankBlockTypes(QString(), 5).size() == 5,
          "and the result is capped");
}

/// The gesture itself, driven through real key and mouse events: point at the
/// canvas, type, press Enter, get a block where you were pointing.
void testQuickAddGesture() {
    beginTest("Typing on the canvas inserts a block");

    Model model;
    DiagramScene scene(model);
    DiagramView view;
    view.setScene(&scene);
    view.resize(800, 600);
    view.show();
    view.setFocus();
    QApplication::processEvents();

    const QPoint where(220, 160);
    QTest::mouseMove(view.viewport(), where);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, where);
    QApplication::processEvents();

    QTest::keyClick(&view, Qt::Key_S);
    QApplication::processEvents();

    auto* edit = view.findChild<QLineEdit*>();
    auto* list = view.findChild<QListWidget*>();
    check(edit != nullptr && list != nullptr,
          "a plain keystroke over the canvas opens the search");
    if (!edit || !list) return;
    check(edit->text() == QStringLiteral("s"),
          "the keystroke that opened it seeds the query");

    QTest::keyClicks(edit, QStringLiteral("tep"));
    QApplication::processEvents();
    check(edit->text() == QStringLiteral("step"), "typing narrows it");
    check(list->count() > 0 &&
              list->item(0)->text().startsWith(QStringLiteral("Step")),
          "and Step is the top hit");

    QTest::keyClick(edit, Qt::Key_Return);
    QApplication::processEvents();

    check(model.blocks().size() == 1, "Enter inserts exactly one block");
    if (model.blocks().empty()) return;
    check(model.blocks().front()->typeName() == "Step",
          "the one that was highlighted");

    // Landing at the origin instead of at the cursor would make the feature
    // slower than the palette it replaces.
    const QPointF wanted = view.mapToScene(where);
    const BlockGeometry& geometry =
        model.geometry(model.blocks().front()->id());
    check(std::abs(geometry.x - wanted.x()) <= 60.0 &&
              std::abs(geometry.y - wanted.y()) <= 60.0,
          "and it lands where the user was pointing");

    // Abandoning must cost nothing.
    QTest::keyClick(&view, Qt::Key_G);
    QApplication::processEvents();
    if (QLineEdit* reopened = view.findChild<QLineEdit*>())
        QTest::keyClick(reopened, Qt::Key_Escape);
    QApplication::processEvents();
    check(model.blocks().size() == 1, "Escape adds nothing");

    // And the shortcuts have to survive: swallowing every keystroke would
    // break Ctrl+A, Ctrl+S and the rest.
    const std::size_t before = model.blocks().size();
    QTest::keyClick(&view, Qt::Key_A, Qt::ControlModifier);
    QApplication::processEvents();
    check(model.blocks().size() == before,
          "a modified keystroke stays a shortcut");
}

/// Copy and paste through the real system clipboard, driven by the real
/// shortcuts — including the part where Ctrl+C must still copy text when the
/// focus is in a text field rather than on the canvas.
void testCopyPasteThroughClipboard(const QString& modelPath) {
    beginTest("Copy and paste go through the clipboard");

    MainWindow window;
    if (!window.openFile(modelPath)) {
        check(false, "the example model opened");
        return;
    }
    window.show();
    QApplication::processEvents();

    DiagramScene* scene = window.scene();
    const std::size_t before = scene->model().blocks().size();

    BlockItem* plant = itemNamed(scene, QStringLiteral("Plant"));
    check(plant != nullptr, "the plant block is on the canvas");
    if (!plant) return;

    scene->clearSelection();
    plant->setSelected(true);
    scene->copySelection();

    check(DiagramScene::clipboardHasBlocks(),
          "the selection reached the clipboard");

    // Plain text as well as the private type, so a selection survives the trip
    // to another running copy.
    check(ModelSerializer::isPastable(
              QGuiApplication::clipboard()->text().toStdString()),
          "and is readable as plain text too");

    scene->pasteAt(QPointF(600, 600));
    QApplication::processEvents();

    check(scene->model().blocks().size() == before + 1,
          "pasting adds exactly one block");
    check(scene->selectedBlocks().size() == 1,
          "and leaves the new block selected");

    // Placed where it was asked for, so a paste does not land off-screen.
    if (scene->selectedBlocks().size() == 1) {
        const BlockGeometry& g =
            scene->model().geometry(scene->selectedBlocks().first()->block()->id());
        check(std::abs(g.x - 600.0) <= 10.0 && std::abs(g.y - 600.0) <= 10.0,
              "at the position it was given");
    }

    // Duplicate must not disturb what is on the clipboard.
    const QString clipboardBefore = QGuiApplication::clipboard()->text();
    scene->duplicateSelection();
    QApplication::processEvents();
    check(scene->model().blocks().size() == before + 2,
          "duplicating adds another");
    check(QGuiApplication::clipboard()->text() == clipboardBefore,
          "without touching the clipboard");
}

/// The canvas shortcuts must not follow the focus into a text field, or
/// editing a parameter would copy blocks instead of characters.
void testCanvasShortcutsStayOnTheCanvas() {
    beginTest("Ctrl+C in a text field still copies text");

    MainWindow window;
    window.show();
    QApplication::processEvents();

    window.scene()->addBlock(QStringLiteral("Constant"), QPointF(0, 0));
    window.scene()->selectAll();

    QGuiApplication::clipboard()->clear();

    // A field standing in for the property panel's editors: same window, same
    // shortcut, focus somewhere other than the canvas.
    QLineEdit field(&window);
    field.setText(QStringLiteral("some text"));
    field.show();
    field.setFocus();
    field.selectAll();
    QApplication::processEvents();

    QTest::keyClick(&field, Qt::Key_C, Qt::ControlModifier);
    QApplication::processEvents();

    check(QGuiApplication::clipboard()->text() == QStringLiteral("some text"),
          "the text field kept its Ctrl+C");
    check(!DiagramScene::clipboardHasBlocks(),
          "and no blocks were copied behind its back");

    // The other half: narrowing the shortcut must not have disabled it. With
    // focus back on the canvas the same keystroke has to copy blocks.
    field.hide();
    window.scene()->selectAll();
    QApplication::processEvents();

    QTest::keyClick(window.findChild<DiagramView*>(), Qt::Key_C,
                    Qt::ControlModifier);
    QApplication::processEvents();

    check(DiagramScene::clipboardHasBlocks(),
          "and Ctrl+C on the canvas still copies blocks");

    const std::size_t before = window.scene()->model().blocks().size();
    QTest::keyClick(window.findChild<DiagramView*>(), Qt::Key_V,
                    Qt::ControlModifier);
    QApplication::processEvents();
    check(window.scene()->model().blocks().size() > before,
          "and Ctrl+V pastes them back");
}

/// Delete has to remove whatever is selected, however it got selected.
///
/// Worth pinning down rather than assuming: the canvas now intercepts plain
/// keystrokes for the block search, and a Delete key arrives from a real
/// keyboard carrying U+007F as its text — close enough to a printable
/// character to be swallowed by accident.
void testDeleteRemovesSelection() {
    beginTest("Delete removes the selection");

    MainWindow window;
    window.resize(1000, 700);
    window.show();
    QApplication::processEvents();

    DiagramScene* scene = window.scene();
    auto* view = window.findChild<DiagramView*>();
    check(view != nullptr, "the canvas view is there");
    if (!view) return;

    auto addChain = [scene] {
        const QString a =
            scene->addBlock(QStringLiteral("Constant"), QPointF(0, 0))->blockId();
        const QString b =
            scene->addBlock(QStringLiteral("Scope"), QPointF(300, 0))->blockId();
        scene->model().connect(a.toStdString(), 0, b.toStdString(), 0);
        scene->rebuild();
        return std::make_pair(a, b);
    };

    // 1. A block selected with the mouse.
    auto ids = addChain();
    view->resetZoom();
    view->centerOn(QPointF(150, 0));
    QApplication::processEvents();

    BlockItem* item = scene->itemForBlock(ids.first);
    if (!item) return;
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                      view->mapFromScene(item->sceneBoundingRect().center()));
    QApplication::processEvents();
    check(scene->selectedBlocks().size() == 1, "clicking a block selects it");

    QTest::keyClick(view, Qt::Key_Delete);
    QApplication::processEvents();
    check(scene->model().blocks().size() == 1, "Delete removes it");
    check(scene->model().connections().empty(),
          "and takes the wire that fed it");

    // 2. The event a real keyboard sends: Delete carries U+007F as its text,
    // which the quick-add filter has to reject.
    scene->selectAll();
    QKeyEvent realDelete(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier,
                         QString(QChar(0x7F)));
    QApplication::sendEvent(view, &realDelete);
    QApplication::processEvents();
    check(scene->model().blocks().empty(),
          "a Delete carrying its control character still deletes");
    check(view->findChild<QLineEdit*>() == nullptr,
          "and does not open the block search instead");

    // 3. A wire on its own.
    ids = addChain();
    scene->clearSelection();
    for (QGraphicsItem* graphicsItem : scene->items())
        if (graphicsItem->type() == WireItem::Type) {
            graphicsItem->setSelected(true);
            break;
        }
    QTest::keyClick(view, Qt::Key_Delete);
    QApplication::processEvents();
    check(scene->model().connections().empty(), "a selected wire is deleted");
    check(scene->model().blocks().size() == 2,
          "without taking its blocks with it");

    // 4. A block and a wire that touches it, together. Removing the block
    // already removes the wire, so this is the path where deleting the same
    // thing twice would show up.
    scene->model().connect(ids.first.toStdString(), 0, ids.second.toStdString(), 0);
    scene->rebuild();
    scene->clearSelection();
    if (BlockItem* block = scene->itemForBlock(ids.first)) block->setSelected(true);
    for (QGraphicsItem* graphicsItem : scene->items())
        if (graphicsItem->type() == WireItem::Type)
            graphicsItem->setSelected(true);

    QTest::keyClick(view, Qt::Key_Delete);
    QApplication::processEvents();
    check(scene->model().blocks().size() == 1, "a mixed selection deletes cleanly");
    check(scene->model().connections().empty(), "leaving no stale wire");

    // 5. Backspace is the same command, for keyboards without a Delete key.
    scene->selectAll();
    QTest::keyClick(view, Qt::Key_Backspace);
    QApplication::processEvents();
    check(scene->model().blocks().empty(), "Backspace deletes too");

    // 6. But not while a text field has the focus, or editing a parameter
    // would delete blocks instead of characters.
    const QString kept =
        scene->addBlock(QStringLiteral("Gain"), QPointF(0, 0))->blockId();
    scene->rebuild();
    scene->selectAll();

    QLineEdit field(&window);
    field.setText(QStringLiteral("abc"));
    field.show();
    field.setFocus();
    field.setCursorPosition(0);
    QApplication::processEvents();

    QTest::keyClick(&field, Qt::Key_Delete);
    QApplication::processEvents();
    check(field.text() == QStringLiteral("bc"),
          "the text field keeps its own Delete");
    check(scene->model().blocks().size() == 1,
          "and the selected block is untouched");
}

/// Every toolbar icon has to actually draw something.
///
/// A QIcon that renders nothing is not an error anywhere in Qt — it just
/// leaves a blank button — so a typo in a path, or artwork drawn outside the
/// 24-unit grid, would ship silently.
void testToolbarIconsDrawSomething() {
    beginTest("Every icon draws visible artwork");

    struct Entry {
        const char* name;
        QIcon icon;
    };
    const QList<Entry> icons = {
        {"run", appicons::run()},
        {"stop", appicons::stop()},
        {"new", appicons::newModel()},
        {"open", appicons::open()},
        {"save", appicons::save()},
        {"copy", appicons::copy()},
        {"cut", appicons::cut()},
        {"paste", appicons::paste()},
        {"duplicate", appicons::duplicate()},
        {"delete", appicons::remove()},
        {"mirror", appicons::mirror()},
        {"addBlock", appicons::addBlock()},
        {"nameSignal", appicons::nameSignal()},
        {"enterSubsystem", appicons::enterSubsystem()},
        {"leaveSubsystem", appicons::leaveSubsystem()},
        {"zoomIn", appicons::zoomIn()},
        {"zoomOut", appicons::zoomOut()},
        {"zoomFit", appicons::zoomFit()},
        {"library", appicons::library()},
        {"saveAsBlock", appicons::saveAsBlock()},
        {"settings", appicons::settings()},
    };

    for (const Entry& entry : icons) {
        const QImage image = entry.icon.pixmap(24, 24).toImage();
        if (image.isNull()) {
            check(false, std::string(entry.name) + " produced no pixmap");
            continue;
        }

        // Enough opaque pixels to be a shape rather than a stray dot, and not
        // so many that the icon is a solid block covering its whole cell.
        int painted = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (qAlpha(image.pixel(x, y)) > 40) ++painted;

        const int total = image.width() * image.height();
        check(painted > total / 20,
              std::string(entry.name) + " draws something at 24 px");
        check(painted < total * 9 / 10,
              std::string(entry.name) + " is a shape, not a filled square");
    }

    // The disabled state has to look disabled, or Stop appears clickable
    // while nothing is running.
    const QImage on = appicons::run().pixmap(24, 24, QIcon::Normal).toImage();
    const QImage off = appicons::run().pixmap(24, 24, QIcon::Disabled).toImage();
    auto meanAlpha = [](const QImage& image) {
        long long sum = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                sum += qAlpha(image.pixel(x, y));
        return sum / double(image.width() * image.height());
    };
    check(meanAlpha(off) < meanAlpha(on) * 0.6,
          "a disabled icon is visibly faded");
}

/// The scenario the feature exists for, end to end and across threads: a
/// real-time run in flight, a slider dragged from the interface thread, and
/// the change showing up in the logged signal.
///
/// This is the one test where the two threads genuinely meet, so it is worth
/// paying the wall-clock second it costs.
void testControlDrivesALiveRun() {
    beginTest("Dragging a slider steers a running model");

    MainWindow window;
    window.show();
    QApplication::processEvents();

    DiagramScene* scene = window.scene();
    Model& model = scene->model();

    const QString sliderId =
        scene->addBlock(QStringLiteral("Slider"), QPointF(0, 0))->blockId();
    const QString scopeId =
        scene->addBlock(QStringLiteral("Scope"), QPointF(300, 0))->blockId();

    Block* sliderBlock = model.block(sliderId.toStdString());
    sliderBlock->params().set("minimum", 0.0);
    sliderBlock->params().set("maximum", 10.0);
    sliderBlock->params().set("value", 2.0);
    model.connect(sliderId.toStdString(), 0, scopeId.toStdString(), 0);

    // Paced, so the run lasts long enough to interact with — which is the
    // only situation where any of this means anything.
    model.solver().stopTime = 0.5;
    model.solver().realTime = true;
    model.solver().realTimeFactor = 1.0;

    scene->rebuild();
    window.refreshControlPanel();
    QApplication::processEvents();

    auto* panel = window.findChild<ControlPanel*>();
    check(panel != nullptr, "the Controls dock exists");
    if (!panel) return;
    check(!panel->isEmpty(), "and found the slider");

    QSlider* widget = panel->findChild<QSlider*>();
    check(widget != nullptr, "the slider has a widget");
    if (!widget) return;

    window.startSimulation();

    // Let the run get going, then drag the slider to the top of its range,
    // and wait for the run to actually finish.
    QElapsedTimer clock;
    clock.start();
    bool moved = false;
    bool finished = false;

    while (clock.elapsed() < 5000) {
        QApplication::processEvents();
        if (!moved && clock.elapsed() > 150) {
            widget->setValue(widget->maximum());
            moved = true;
        }
        if (moved && !window.isRunning()) {
            finished = true;
            break;
        }
        QThread::msleep(5);
    }

    check(moved, "the slider was moved while the run was in flight");
    check(finished, "and the run finished");

    // The run is paced at 1x for 0.5 s, so it must still have been going.
    auto* interactive = dynamic_cast<InteractiveBlock*>(
        model.block(sliderId.toStdString()));
    check(interactive != nullptr, "the block is interactive");
    if (!interactive) return;
    checkClose(interactive->liveValue(), 10.0,
               "the block picked the new value up");

    // And the change reached the numbers, not just the widget: the logged
    // signal has to start near 2 and end near 10.
    const SignalLogPtr log = window.lastLog();
    check(log != nullptr && log->sampleCount() > 2, "the run logged samples");
    if (!log || log->sampleCount() <= 2) return;

    const LogChannel& channel = log->channels().front();
    checkClose(channel.at(0, 0), 2.0, "the trace starts where the slider was");
    checkClose(channel.at(log->sampleCount() - 1, 0), 10.0,
               "and ends where it was dragged to");
}

/// Controls have to work on the block itself, not only in the dock — and
/// operating one must not drag the block out from under the cursor.
void testControlsOnTheCanvas() {
    beginTest("Blocks can be driven on the canvas");

    Model model;
    DiagramScene scene(model);
    // Events go through a real view: a QGraphicsScene has no widget to send
    // them to, and routing them by hand would test the harness, not the code.
    DiagramView view;
    view.setScene(&scene);
    view.resize(700, 700);
    view.show();
    QApplication::processEvents();

    // Ids, not item pointers: rebuild() below destroys every item, and a
    // BlockItem* captured now would dangle.
    const QString toggleId =
        scene.addBlock(QStringLiteral("Toggle"), QPointF(0, 0))->blockId();
    const QString buttonId =
        scene.addBlock(QStringLiteral("PushButton"), QPointF(0, 200))->blockId();
    const QString sliderId =
        scene.addBlock(QStringLiteral("Slider"), QPointF(0, 400))->blockId();

    Block* sliderModelBlock = model.block(sliderId.toStdString());
    sliderModelBlock->params().set("minimum", 0.0);
    sliderModelBlock->params().set("maximum", 10.0);
    sliderModelBlock->params().set("value", 0.0);
    scene.rebuild();
    view.zoomToFit();
    QApplication::processEvents();

    // Scene point -> a pixel inside the viewport, which is what QTest needs.
    auto at = [&view](BlockItem* item, const QPointF& local) {
        return view.mapFromScene(item->mapToScene(local));
    };

    auto interactiveFor = [](BlockItem* item) {
        return dynamic_cast<InteractiveBlock*>(item->block());
    };

    // --- a toggle flips on click ---
    BlockItem* toggle = scene.itemForBlock(toggleId);
    check(toggle != nullptr, "the toggle item is on the canvas");
    if (!toggle) return;
    InteractiveBlock* toggleBlock = interactiveFor(toggle);
    check(toggleBlock != nullptr, "the toggle is interactive");
    if (!toggleBlock) return;

    const QRectF zone = toggle->controlZone();
    check(!zone.isEmpty(), "and has a control zone on the block");
    check(!toggle->bodyRect().contains(zone) ||
              zone.width() < toggle->bodyRect().width(),
          "smaller than the block, so dragging elsewhere still moves it");

    const double before = toggleBlock->liveValue();
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      at(toggle, zone.center()));
    QApplication::processEvents();
    check(toggleBlock->liveValue() != before, "clicking the switch flips it");

    // --- a push button is momentary ---
    BlockItem* button = scene.itemForBlock(buttonId);
    if (!button) return;
    InteractiveBlock* buttonBlock = interactiveFor(button);
    if (!buttonBlock) return;
    const ControlDescriptor buttonSpec = buttonBlock->descriptor();
    const QPoint buttonAt = at(button, button->controlZone().center());

    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, buttonAt);
    QApplication::processEvents();
    checkClose(buttonBlock->liveValue(), buttonSpec.onValue,
               "holding the button outputs its on value");

    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                        buttonAt);
    QApplication::processEvents();
    checkClose(buttonBlock->liveValue(), buttonSpec.offValue,
               "and releasing it springs back");

    // --- a slider drags, and the block stays put ---
    BlockItem* slider = scene.itemForBlock(sliderId);
    if (!slider) return;
    InteractiveBlock* sliderBlock = interactiveFor(slider);
    if (!sliderBlock) return;

    const QRectF track = slider->controlZone();
    const QPointF startedAt = slider->pos();

    const QPoint left = at(slider, QPointF(track.left() + 1, track.center().y()));
    const QPoint right = at(slider, QPointF(track.right() - 1, track.center().y()));

    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, left);

    // Built by hand rather than QTest::mouseMove, which reports no button
    // held — and a move with no button is not a drag, so the view would
    // never route it to the item.
    QMouseEvent drag(QEvent::MouseMove, QPointF(right),
                     QPointF(view.viewport()->mapToGlobal(right)),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &drag);
    QApplication::processEvents();

    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, right);
    QApplication::processEvents();

    check(sliderBlock->liveValue() > 8.0,
          "dragging the track raises the value");
    check(slider->pos() == startedAt,
          "and the block itself did not move with the drag");
}

/// A locked diagram has to stay usable without being editable — that is the
/// difference between locking it and switching it off.
void testLockedSceneStillDrivesControls() {
    beginTest("A locked diagram still takes control input");

    Model model;
    DiagramScene scene(model);
    DiagramView view;
    view.setScene(&scene);
    view.resize(700, 500);
    view.show();

    BlockItem* item = scene.addBlock(QStringLiteral("Toggle"), QPointF(0, 0));
    scene.addBlock(QStringLiteral("Constant"), QPointF(200, 0));
    scene.rebuild();
    view.zoomToFit();
    QApplication::processEvents();

    const QString toggleId = item->blockId();
    scene.setInteractionLocked(true);
    check(scene.isInteractionLocked(), "the scene reports itself locked");

    BlockItem* toggle = scene.itemForBlock(toggleId);
    auto* block = dynamic_cast<InteractiveBlock*>(toggle->block());
    if (!block) return;

    const double before = block->liveValue();
    QTest::mouseClick(
        view.viewport(), Qt::LeftButton, Qt::NoModifier,
        view.mapFromScene(toggle->mapToScene(toggle->controlZone().center())));
    QApplication::processEvents();
    check(block->liveValue() != before,
          "a control still responds while the diagram is locked");

    // Everything that edits the diagram must refuse.
    const std::size_t blocks = model.blocks().size();
    scene.selectAll();
    scene.deleteSelection();
    check(model.blocks().size() == blocks, "delete is refused");

    scene.duplicateSelection();
    check(model.blocks().size() == blocks, "duplicate is refused");

    scene.copySelection();
    scene.pasteAt(QPointF(400, 400));
    check(model.blocks().size() == blocks, "paste is refused");

    // And unlocking gives it all back.
    scene.setInteractionLocked(false);
    scene.selectAll();
    scene.deleteSelection();
    check(model.blocks().empty(), "unlocking restores editing");
}

/// Autosave writes the model back to its own file on its own, and — the part
/// that would be unusable if it were wrong — never asks a question to do it.
void testAutosaveWritesTheFile(const QString& modelPath) {
    beginTest("Autosave writes the open file by itself");

    QTemporaryDir dir;
    check(dir.isValid(), "a scratch directory for the copy");
    if (!dir.isValid()) return;

    const QString path = dir.filePath(QStringLiteral("autosave.spy"));
    check(QFile::copy(modelPath, path), "the example is copied into it");

    MainWindow window;
    if (!window.openFile(path)) {
        check(false, "the copy opened");
        return;
    }

    check(!window.isAutosaveEnabled(), "autosave starts off");
    window.setAutosaveEnabled(true);
    check(window.isAutosaveEnabled(), "and turns on");

    const std::size_t before = window.scene()->model().blocks().size();
    window.scene()->addBlock(QStringLiteral("Gain"), QPointF(600, 400));
    QApplication::processEvents();
    check(window.windowTitle().contains(QLatin1Char('*')),
          "an edit marks the model unsaved");

    // The timer's 30 seconds are not worth waiting for; fire what it fires.
    QMetaObject::invokeMethod(&window, "autosave");
    QApplication::processEvents();

    check(!window.windowTitle().contains(QLatin1Char('*')),
          "autosaving clears the unsaved marker");

    Model reloaded;
    ModelSerializer::load(reloaded, path.toStdString());
    check(reloaded.blocks().size() == before + 1,
          "and the file on disk has the new block");

    // Nothing to save must stay a no-op: a second tick cannot fail or spin.
    QMetaObject::invokeMethod(&window, "autosave");
    QApplication::processEvents();
    check(!window.windowTitle().contains(QLatin1Char('*')),
          "a tick with nothing to write does nothing");

    window.setAutosaveEnabled(false);
    check(!window.isAutosaveEnabled(), "and it can be turned back off");

    // An untitled model has no file to write to. Autosave must leave it
    // alone rather than open Save As — a dialog nobody asked for, every 30
    // seconds, would be worse than no autosave at all.
    MainWindow untitled;
    untitled.setAutosaveEnabled(true);
    untitled.scene()->addBlock(QStringLiteral("Gain"), QPointF(0, 0));
    QApplication::processEvents();
    QMetaObject::invokeMethod(&untitled, "autosave");
    QApplication::processEvents();
    check(untitled.windowTitle().contains(QLatin1Char('*')),
          "an untitled model stays unsaved, with no dialog");
    untitled.setAutosaveEnabled(false);
}

/// The ∞ toggle has to reach the solver settings and the stop-time field.
void testUnboundedToggle() {
    beginTest("The infinity toggle drops the stop time");

    MainWindow window;
    window.show();
    QApplication::processEvents();

    QAction* infinity = nullptr;
    for (QAction* action : window.findChildren<QAction*>())
        if (action->text() == QStringLiteral("∞")) infinity = action;

    check(infinity != nullptr, "the toolbar has an infinity toggle");
    if (!infinity) return;
    check(infinity->isCheckable(), "and it is a toggle");
    check(!infinity->isChecked(), "off by default");

    infinity->trigger();
    QApplication::processEvents();
    check(window.scene()->model().solver().unbounded,
          "turning it on makes the run unbounded");

    // The stop-time field has to stop claiming an end that will never come.
    QLineEdit* stopTime = nullptr;
    for (QLineEdit* edit : window.findChildren<QLineEdit*>())
        if (edit->text() == QStringLiteral("∞")) stopTime = edit;
    check(stopTime != nullptr, "the stop-time field shows ∞");
    check(stopTime && !stopTime->isEnabled(),
          "and is disabled, since there is nothing to type");

    infinity->trigger();
    QApplication::processEvents();
    check(!window.scene()->model().solver().unbounded,
          "turning it off restores the stop time");
    check(window.scene()->model().solver().stopTime > 0.0,
          "which was kept rather than cleared");
}

/// A plot has to be placeable: pinned above the window, or folded into a tab
/// beside the Console.
void testScopePlacement(const QString& modelPath) {
    beginTest("A plot can be pinned on top or docked with the console");

    MainWindow window;
    if (!window.openFile(modelPath)) {
        check(false, "the example model opened");
        return;
    }
    window.show();
    QApplication::processEvents();

    // Running is what opens the plots.
    window.startSimulation();
    QElapsedTimer clock;
    clock.start();
    while (window.isRunning() && clock.elapsed() < 5000) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
    QApplication::processEvents();

    auto* scope = window.findChild<ScopeWindow*>();
    check(scope != nullptr, "the run opened a plot");
    if (!scope) return;

    auto* dock = qobject_cast<QDockWidget*>(scope->parentWidget());
    check(dock != nullptr, "which lives in a dock");
    if (!dock) return;
    check(dock->isFloating(), "floating by default, as a plot always was");

    auto* console = window.findChild<QDockWidget*>(QStringLiteral("consoleDock"));
    check(console != nullptr, "the console dock is there to tab with");
    if (!console) return;

    // The buttons the user would press, not the slots behind them.
    QPushButton* dockButton = nullptr;
    QCheckBox* onTopBox = nullptr;
    for (QPushButton* button : scope->findChildren<QPushButton*>())
        if (button->isCheckable()) dockButton = button;
    for (QCheckBox* box : scope->findChildren<QCheckBox*>())
        if (box->text() == QStringLiteral("On top")) onTopBox = box;

    check(dockButton != nullptr && onTopBox != nullptr,
          "the plot offers both placement controls");
    if (!dockButton || !onTopBox) return;

    // --- pin it above the window ---
    onTopBox->setChecked(true);
    QApplication::processEvents();
    check(dock->windowFlags().testFlag(Qt::WindowStaysOnTopHint),
          "ticking 'On top' sets the hint on the window");
    check(dock->isVisible(),
          "and the window is still shown after the flag change");

    // --- fold it into a tab ---
    dockButton->click();
    QApplication::processEvents();

    check(!dock->isFloating(), "docking attaches it to the main window");
    check(window.tabifiedDockWidgets(console).contains(dock),
          "as a tab beside the Console");
    check(!dock->windowFlags().testFlag(Qt::WindowStaysOnTopHint),
          "and the always-on-top hint is dropped, having nothing to top");
    check(!onTopBox->isEnabled(),
          "so the control for it is disabled while docked");
    check(dockButton->text() == QStringLiteral("Undock"),
          "and the button now offers the way back");

    // --- and back out again ---
    dockButton->click();
    QApplication::processEvents();
    check(dock->isFloating(), "undocking returns it to its own window");
    check(onTopBox->isEnabled(), "with the pin available again");
}

/// The two costs a placed wire imposes on the next one.
void testWireFieldPenalties() {
    beginTest("Wire lane and crossing penalties");

    // One horizontal run at y = 100, from x = 0 to x = 200, carrying signal 1.
    QPainterPath placed;
    placed.moveTo(0.0, 100.0);
    placed.lineTo(200.0, 100.0);

    WireField field;
    field.add(placed, 1);

    // Running along it for 100 units costs that length, weighted.
    checkClose(field.penalty(true, 100.0, 50.0, 150.0, 2), 100.0 * 2.0,
               "sharing a lane costs the shared length");

    // The same wire's own branch may share it freely.
    checkClose(field.penalty(true, 100.0, 50.0, 150.0, 1), 0.0,
               "a branch of the same signal is exempt");

    // A clear lane is free.
    checkClose(field.penalty(true, 400.0, 50.0, 150.0, 2), 0.0,
               "a lane well away from it costs nothing");

    // Just outside the shared band, also free.
    checkClose(field.penalty(true, 100.0 + 8.0, 50.0, 150.0, 2), 0.0,
               "a lane beyond the tolerance costs nothing");

    // Crossing it costs one flat crossing.
    checkClose(field.penalty(false, 100.0, 50.0, 150.0, 2), 30.0,
               "crossing it costs one crossing");

    // A perpendicular run that stops short of it does not cross.
    checkClose(field.penalty(false, 100.0, 50.0, 90.0, 2), 0.0,
               "stopping short of it is not a crossing");

    // Nor does one that passes beyond its far end.
    checkClose(field.penalty(false, 300.0, 50.0, 150.0, 2), 0.0,
               "passing beyond its end is not a crossing");
}

/// Two unrelated wires with the same natural route must not end up drawn on
/// top of each other.
void testWiresTakeSeparateLanes() {
    beginTest("Parallel wires are pushed onto separate lanes");

    Model model;
    DiagramScene scene(model);

    // Two sources at the same height as their sinks, so the obvious route for
    // both is a straight line down the very same lane.
    Block* a = model.addBlock("Constant", 0, 0);
    Block* b = model.addBlock("Constant", 0, 400);
    Block* mux = model.addBlock("Mux", 600, 180);
    mux->params().set("inputs", 2.0);
    Block* scope = model.addBlock("Scope", 800, 180);

    model.connect(a->id(), 0, mux->id(), 0);
    model.connect(b->id(), 0, mux->id(), 1);
    model.connect(mux->id(), 0, scope->id(), 0);
    scene.rebuild();

    // Collect every horizontal run of every wire, with its lane.
    struct Run {
        qreal lane, from, to;
    };
    QList<QList<Run>> perWire;
    for (QGraphicsItem* item : scene.items()) {
        if (item->type() != WireItem::Type) continue;
        const QPainterPath path = static_cast<WireItem*>(item)->path();
        QList<Run> runs;
        for (int i = 1; i < path.elementCount(); ++i) {
            const auto p = path.elementAt(i - 1);
            const auto q = path.elementAt(i);
            if (std::abs(p.y - q.y) < 0.5 && std::abs(p.x - q.x) > 1.0)
                runs.append({p.y, std::min(p.x, q.x), std::max(p.x, q.x)});
        }
        perWire.append(runs);
    }

    check(perWire.size() == 3, "all three wires were routed");

    qreal worstShared = 0.0;
    for (int i = 0; i < perWire.size(); ++i)
        for (int j = i + 1; j < perWire.size(); ++j)
            for (const Run& x : perWire[i])
                for (const Run& y : perWire[j]) {
                    if (std::abs(x.lane - y.lane) > 7.0) continue;
                    worstShared = std::max(
                        worstShared, std::min(x.to, y.to) - std::max(x.from, y.from));
                }

    check(worstShared <= 1.0,
          "no two wires share a horizontal lane (worst overlap " +
              std::to_string(worstShared) + " units)");
}

}  // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // Settings the window remembers (autosave) belong to the run, not to
    // whoever is running it: keep them in a directory we throw away.
    QTemporaryDir settingsDir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settingsDir.path());

    std::cout << "SimuPy interface tests\n\n";
    registerBuiltinBlocks();

    try {
        PythonEngine::instance().initialize();
    } catch (const ModelError& error) {
        std::cerr << "could not start Python: " << error.what() << '\n';
        return 1;
    }

    // The hierarchical example lives beside the sources; the test is run from
    // the build directory, so look upward for it.
    QString modelPath = QStringLiteral("examples/subsystem_controller.spy");
    if (!QFileInfo::exists(modelPath))
        modelPath = QStringLiteral("examples/subsystem_controller.spy");
    if (!QFileInfo::exists(modelPath)) {
        std::cerr << "cannot find the example model; run from the project or "
                     "build directory\n";
        return 1;
    }

    testSubsystemNavigation(modelPath);
    testOpenNonSubsystem(modelPath);
    testAddBlockAndWire();
    testCustomBlockLoop();
    testSignalNameFollowsEveryBranch();
    testQuickAddRanking();
    testQuickAddGesture();
    testCopyPasteThroughClipboard(modelPath);
    testCanvasShortcutsStayOnTheCanvas();
    testDeleteRemovesSelection();
    testToolbarIconsDrawSomething();
    testControlDrivesALiveRun();
    testControlsOnTheCanvas();
    testLockedSceneStillDrivesControls();
    testUnboundedToggle();
    testAutosaveWritesTheFile(modelPath);
    testScopePlacement(modelPath);
    testWireFieldPenalties();
    testWiresTakeSeparateLanes();

    std::cout << '\n'
              << (g_failures == 0 ? "PASSED" : "FAILED") << "  " << g_checks
              << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
