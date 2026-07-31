
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
#include "app/gui/dialogs/BlockInspector.h"
#include "app/gui/panels/PropertyPanel.h"
#include "app/gui/dialogs/ScopeWindow.h"
#include "app/gui/canvas/WireItem.h"
#include "app/gui/canvas/WireRouter.h"
#include "io/ModelSerializer.h"
#include "scripting/PythonEngine.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QGroupBox>
#include <QPainter>
#include <QPixmap>
#include <QLabel>
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

    scene.clearSelection();
    if (BlockItem* item = scene.itemForBlock(
            QString::fromStdString(source->block()->id())))
        item->setSelected(true);
    scene.deleteSelection();

    check(model.blocks().size() == 1, "the block is gone");
    check(model.connections().empty(), "and so is the wire that fed it");
}

void testCustomBlockLoop() {
    beginTest("A saved subsystem comes back as a palette block");

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

    const std::string sourceId = source->block()->id();
    model.connect(sourceId, 0, firstSink->block()->id(), 0);
    model.connect(sourceId, 0, secondSink->block()->id(), 0);

    // rebuild() destroys every item, so look blocks up again after each one.
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

    // A word boundary inside the name, not a match at character zero.
    check(first(QStringLiteral("transfer")) == QStringLiteral("TransferFcn"),
          "an inner word matches");
    check(contains(QStringLiteral("transfer"),
                   QStringLiteral("DiscreteTransferFcn")),
          "and finds the discrete one too");

    check(contains(QStringLiteral("tffcn"), QStringLiteral("TransferFcn")),
          "a subsequence still matches");

    check(QuickAddPopup::rankBlockTypes(QStringLiteral("zzzqqq")).isEmpty(),
          "nonsense matches nothing");
    check(QuickAddPopup::rankBlockTypes(QString()).size() > 20,
          "an empty query offers everything");

    check(QuickAddPopup::rankBlockTypes(QString(), 5).size() == 5,
          "and the result is capped");
}

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

    const QPointF wanted = view.mapToScene(where);
    const BlockGeometry& geometry =
        model.geometry(model.blocks().front()->id());
    check(std::abs(geometry.x - wanted.x()) <= 60.0 &&
              std::abs(geometry.y - wanted.y()) <= 60.0,
          "and it lands where the user was pointing");

    QTest::keyClick(&view, Qt::Key_G);
    QApplication::processEvents();
    if (QLineEdit* reopened = view.findChild<QLineEdit*>())
        QTest::keyClick(reopened, Qt::Key_Escape);
    QApplication::processEvents();
    check(model.blocks().size() == 1, "Escape adds nothing");

    // The shortcuts have to survive: Ctrl+A, Ctrl+S and the rest.
    const std::size_t before = model.blocks().size();
    QTest::keyClick(&view, Qt::Key_A, Qt::ControlModifier);
    QApplication::processEvents();
    check(model.blocks().size() == before,
          "a modified keystroke stays a shortcut");
}

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

    // Plain text too, so a selection survives the trip to another copy.
    check(ModelSerializer::isPastable(
              QGuiApplication::clipboard()->text().toStdString()),
          "and is readable as plain text too");

    scene->pasteAt(QPointF(600, 600));
    QApplication::processEvents();

    check(scene->model().blocks().size() == before + 1,
          "pasting adds exactly one block");
    check(scene->selectedBlocks().size() == 1,
          "and leaves the new block selected");

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

void testCanvasShortcutsStayOnTheCanvas() {
    beginTest("Ctrl+C in a text field still copies text");

    MainWindow window;
    window.show();
    QApplication::processEvents();

    window.scene()->addBlock(QStringLiteral("Constant"), QPointF(0, 0));
    window.scene()->selectAll();

    QGuiApplication::clipboard()->clear();

    // Same window, same shortcut, focus somewhere other than the canvas.
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

    scene->selectAll();
    QKeyEvent realDelete(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier,
                         QString(QChar(0x7F)));
    QApplication::sendEvent(view, &realDelete);
    QApplication::processEvents();
    check(scene->model().blocks().empty(),
          "a Delete carrying its control character still deletes");
    check(view->findChild<QLineEdit*>() == nullptr,
          "and does not open the block search instead");

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

    scene->selectAll();
    QTest::keyClick(view, Qt::Key_Backspace);
    QApplication::processEvents();
    check(scene->model().blocks().empty(), "Backspace deletes too");

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

    auto* interactive = dynamic_cast<InteractiveBlock*>(
        model.block(sliderId.toStdString()));
    check(interactive != nullptr, "the block is interactive");
    if (!interactive) return;
    checkClose(interactive->liveValue(), 10.0,
               "the block picked the new value up");

    const SignalLogPtr log = window.lastLog();
    check(log != nullptr && log->sampleCount() > 2, "the run logged samples");
    if (!log || log->sampleCount() <= 2) return;

    const LogChannel& channel = log->channels().front();
    checkClose(channel.at(0, 0), 2.0, "the trace starts where the slider was");
    checkClose(channel.at(log->sampleCount() - 1, 0), 10.0,
               "and ends where it was dragged to");
}

void testControlsOnTheCanvas() {
    beginTest("Blocks can be driven on the canvas");

    Model model;
    DiagramScene scene(model);
    DiagramView view;
    view.setScene(&scene);
    view.resize(700, 700);
    view.show();
    QApplication::processEvents();

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

    auto at = [&view](BlockItem* item, const QPointF& local) {
        return view.mapFromScene(item->mapToScene(local));
    };

    auto interactiveFor = [](BlockItem* item) {
        return dynamic_cast<InteractiveBlock*>(item->block());
    };

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

    BlockItem* slider = scene.itemForBlock(sliderId);
    if (!slider) return;
    InteractiveBlock* sliderBlock = interactiveFor(slider);
    if (!sliderBlock) return;

    const QRectF track = slider->controlZone();
    const QPointF startedAt = slider->pos();

    const QPoint left = at(slider, QPointF(track.left() + 1, track.center().y()));
    const QPoint right = at(slider, QPointF(track.right() - 1, track.center().y()));

    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, left);

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

    const std::size_t blocks = model.blocks().size();
    scene.selectAll();
    scene.deleteSelection();
    check(model.blocks().size() == blocks, "delete is refused");

    scene.duplicateSelection();
    check(model.blocks().size() == blocks, "duplicate is refused");

    scene.copySelection();
    scene.pasteAt(QPointF(400, 400));
    check(model.blocks().size() == blocks, "paste is refused");

    scene.setInteractionLocked(false);
    scene.selectAll();
    scene.deleteSelection();
    check(model.blocks().empty(), "unlocking restores editing");
}

/// A parameter of a kind the block cannot read must not take the window down:
/// drawing it is a report, not a crash.
void testAboutNamesTheLicence() {
    beginTest("The About box carries the licence and the project links");

    const QString about = MainWindow::aboutHtml();

    check(about.contains(QStringLiteral("GNU")) &&
              about.contains(QStringLiteral("General Public License")),
          "it names the licence");
    check(about.contains(QStringLiteral("version 3")) &&
              about.contains(QStringLiteral("any later version")),
          "including which version, and that later ones are allowed");
    check(about.contains(QStringLiteral("no warranty")),
          "and the absence of warranty the licence asks to be shown");
    check(about.contains(QStringLiteral("Copyright")),
          "with a copyright line");
    check(about.contains(QStringLiteral("github.com/ppgg88/simupy\"")) &&
              about.contains(QStringLiteral("/issues")),
          "and links to the project and its bug tracker");
    check(about.contains(QApplication::applicationVersion()) ||
              QApplication::applicationVersion().isEmpty(),
          "next to the version it is built from");
}

void testUnreadableParameterDoesNotCrash() {
    beginTest("A block with an unreadable parameter still draws");

    Model model;
    DiagramScene scene(model);
    BlockItem* gain = scene.addBlock(QStringLiteral("Gain"), QPointF(0, 0));
    BlockItem* mux = scene.addBlock(QStringLiteral("Mux"), QPointF(200, 0));
    if (!gain || !mux) return;

    // What a hand-edited file, or a mask resolving to text, leaves behind.
    gain->block()->params().set("gain", std::string("2"));
    mux->block()->params().set("inputs", std::string("three"));
    scene.refreshBlock(gain->blockId());
    scene.refreshBlock(mux->blockId());

    QPixmap canvas(400, 200);
    QPainter painter(&canvas);
    scene.render(&painter);
    painter.end();
    check(true, "the canvas renders both of them");

    check(!gain->problem().isEmpty() || !mux->problem().isEmpty(),
          "and the trouble is reported on the block rather than swallowed");

    BlockInspector inspector(model, gain->block(), {}, true);
    check(inspector.findChild<PropertyPanel*>() != nullptr,
          "the window opens on it, so the value can be corrected");

    gain->block()->params().set("gain", std::vector<double>{2.0});
    scene.refreshBlock(gain->blockId());
    QPainter again(&canvas);
    scene.render(&again);
    again.end();
    check(true, "and it draws normally once the value is put right");
}

void testBlockInspector() {
    beginTest("The block window lists its ports and edits its parameters");

    Model model;
    Block* source = model.addBlock("Constant", 0, 0);
    Block* sum = model.addBlock("Sum", 200, 0);
    sum->params().set("signs", std::string("++"));
    Block* scope = model.addBlock("Scope", 400, 0);
    model.connect(source->id(), 0, sum->id(), 0);
    model.connect(sum->id(), 0, scope->id(), 0);
    source->setSignalName(0, "setpoint");

    BlockInspector inspector(model, sum, {}, true);
    inspector.show();
    QApplication::processEvents();

    check(inspector.windowTitle().contains(QString::fromStdString(sum->name())),
          "the window is titled after the block");

    QStringList groups;
    for (QGroupBox* group : inspector.findChildren<QGroupBox*>())
        groups << group->title();
    check(groups.contains(QStringLiteral("Inputs")) &&
              groups.contains(QStringLiteral("Outputs")),
          "it has a section for each side of the block");

    QStringList texts;
    for (QLabel* label : inspector.findChildren<QLabel*>())
        texts << label->text();
    const QString all = texts.join(QStringLiteral(" | "));

    check(all.contains(QStringLiteral("from Constant")),
          "the wired input says where it comes from (got: " +
              all.toStdString() + ")");
    check(all.contains(QStringLiteral("setpoint")),
          "including the name the signal carries");
    check(all.contains(QStringLiteral("to Scope")),
          "and the output says where it goes");
    check(all.contains(QStringLiteral("not connected")),
          "while the free input says so");

    auto* panel = inspector.findChild<PropertyPanel*>();
    check(panel != nullptr && panel->currentBlock() == sum,
          "the parameters of that block are editable in the window");

    auto* buttons = inspector.findChild<QDialogButtonBox*>();
    check(buttons != nullptr && buttons->button(QDialogButtonBox::Ok) != nullptr,
          "and there is an OK button");
    if (!buttons) return;

    QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
    QApplication::processEvents();
    check(!inspector.isVisible(), "which closes it");

    BlockInspector readOnly(model, sum, {}, false);
    auto* locked = readOnly.findChild<PropertyPanel*>();
    check(locked != nullptr && !locked->isEnabled(),
          "during a run the parameters are read-only");
}

void testWireDropZone() {
    beginTest("A dropped wire lands on the nearest port band");

    Model model;
    DiagramScene scene(model);
    DiagramView view;
    view.setScene(&scene);
    view.resize(900, 600);
    view.show();
    QApplication::processEvents();

    BlockItem* source =
        scene.addBlock(QStringLiteral("Constant"), QPointF(0, 200));
    BlockItem* sum = scene.addBlock(QStringLiteral("Sum"), QPointF(400, 200));
    if (!source || !sum) return;
    sum->block()->params().set("signs", std::string("++"));
    scene.refreshBlock(sum->blockId());
    check(sum->inputCount() == 2, "the Sum has two inputs to choose between");
    if (sum->inputCount() != 2) return;

    const std::string sumId = sum->block()->id();
    view.resetZoom();
    view.centerOn(QPointF(250, 200));
    QApplication::processEvents();

    auto dragTo = [&](const QPointF& scenePos) {
        const QPoint from = view.mapFromScene(source->outputScenePos(0));
        const QPoint to = view.mapFromScene(scenePos);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(view.viewport(), to);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QApplication::processEvents();
    };

    const QPointF first = sum->inputScenePos(0);
    const QPointF second = sum->inputScenePos(1);
    const qreal middle = (first.y() + second.y()) / 2.0;

    // 14 px out is past the 11 px grab radius a press uses.
    dragTo(QPointF(first.x() - 14.0, first.y()));
    check(model.connections().size() == 1,
          "a release short of the port still lands");
    check(model.incoming(sumId, 0) != nullptr, "on the port it was aimed at");

    dragTo(QPointF(first.x() - 6.0, middle - 4.0));
    check(model.incoming(sumId, 0) != nullptr,
          "above the halfway line is still the first input");

    dragTo(QPointF(second.x() - 6.0, middle + 4.0));
    check(model.incoming(sumId, 1) != nullptr, "below it is the second");
    check(model.connections().size() == 2, "so both inputs end up wired");

    const std::size_t before = model.connections().size();
    dragTo(QPointF(first.x() - 90.0, first.y()));
    check(model.connections().size() == before,
          "a release in open space connects nothing");
}

void testGroupSelectionOnCanvas() {
    beginTest("Selected blocks are grouped into a subsystem");

    MainWindow window;
    window.show();
    QApplication::processEvents();

    DiagramScene* scene = window.scene();
    Model& model = scene->model();

    const QString source =
        scene->addBlock(QStringLiteral("Constant"), QPointF(0, 0))->blockId();
    const QString gain =
        scene->addBlock(QStringLiteral("Gain"), QPointF(200, 0))->blockId();
    const QString scope =
        scene->addBlock(QStringLiteral("Scope"), QPointF(400, 0))->blockId();
    model.connect(source.toStdString(), 0, gain.toStdString(), 0);
    model.connect(gain.toStdString(), 0, scope.toStdString(), 0);
    scene->rebuild();

    scene->clearSelection();
    if (BlockItem* item = scene->itemForBlock(gain)) item->setSelected(true);
    scene->groupSelection();
    QApplication::processEvents();

    check(model.blocks().size() == 3,
          "the grouped block is replaced by a subsystem");
    check(model.block(gain.toStdString()) == nullptr,
          "and is no longer on this diagram");

    const QList<BlockItem*> selected = scene->selectedBlocks();
    check(selected.size() == 1 && isSubsystem(selected.first()->block()),
          "the new subsystem is left selected, ready to be opened");
    if (selected.size() != 1) return;

    const QString subsystemId = selected.first()->blockId();

    check(model.incoming(subsystemId.toStdString(), 0) != nullptr,
          "the source feeds its input port");
    check(model.incoming(scope.toStdString(), 0) != nullptr,
          "and the scope is fed from its output port");

    window.openSelectedSubsystem();
    QApplication::processEvents();

    check(window.currentPath().endsWith(QStringLiteral("Subsystem")),
          "opening it goes a level down (got \"" +
              window.currentPath().toStdString() + "\")");
    check(scene->model().blocks().size() == 3,
          "the gain is inside, between an Inport and an Outport");

    window.leaveSubsystem();
    QApplication::processEvents();
    check(window.currentPath() == QStringLiteral("model"), "and back out");

    scene->clearSelection();
    if (BlockItem* item = scene->itemForBlock(subsystemId))
        item->setSelected(true);
    scene->ungroupSelection();
    QApplication::processEvents();

    check(model.blocks().size() == 3, "the contents come back up");
    check(model.connections().size() == 2, "joined straight through");

    const QList<BlockItem*> back = scene->selectedBlocks();
    check(back.size() == 1 && back.first()->block()->typeName() == "Gain",
          "the block that came up is selected, and it is the gain");

    const Connection* intoScope = model.incoming(scope.toStdString(), 0);
    check(intoScope != nullptr && back.size() == 1 &&
              intoScope->sourceBlock == back.first()->block()->id(),
          "the gain feeds the scope directly again");
}

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

    QMetaObject::invokeMethod(&window, "autosave");
    QApplication::processEvents();

    check(!window.windowTitle().contains(QLatin1Char('*')),
          "autosaving clears the unsaved marker");

    Model reloaded;
    ModelSerializer::load(reloaded, path.toStdString());
    check(reloaded.blocks().size() == before + 1,
          "and the file on disk has the new block");

    QMetaObject::invokeMethod(&window, "autosave");
    QApplication::processEvents();
    check(!window.windowTitle().contains(QLatin1Char('*')),
          "a tick with nothing to write does nothing");

    window.setAutosaveEnabled(false);
    check(!window.isAutosaveEnabled(), "and it can be turned back off");

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

void testScopePlacement(const QString& modelPath) {
    beginTest("A plot can be pinned on top or docked with the console");

    MainWindow window;
    if (!window.openFile(modelPath)) {
        check(false, "the example model opened");
        return;
    }
    window.show();
    QApplication::processEvents();

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

    QPushButton* dockButton = nullptr;
    QCheckBox* onTopBox = nullptr;
    for (QPushButton* button : scope->findChildren<QPushButton*>())
        if (button->isCheckable()) dockButton = button;
    for (QCheckBox* box : scope->findChildren<QCheckBox*>())
        if (box->text() == QStringLiteral("On top")) onTopBox = box;

    check(dockButton != nullptr && onTopBox != nullptr,
          "the plot offers both placement controls");
    if (!dockButton || !onTopBox) return;

    onTopBox->setChecked(true);
    QApplication::processEvents();
    check(dock->windowFlags().testFlag(Qt::WindowStaysOnTopHint),
          "ticking 'On top' sets the hint on the window");
    check(dock->isVisible(),
          "and the window is still shown after the flag change");

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

    dockButton->click();
    QApplication::processEvents();
    check(dock->isFloating(), "undocking returns it to its own window");
    check(onTopBox->isEnabled(), "with the pin available again");
}

void testWireFieldPenalties() {
    beginTest("Wire lane and crossing penalties");

    QPainterPath placed;
    placed.moveTo(0.0, 100.0);
    placed.lineTo(200.0, 100.0);

    WireField field;
    field.add(placed, 1);

    checkClose(field.penalty(true, 100.0, 50.0, 150.0, 2), 100.0 * 2.0,
               "sharing a lane costs the shared length");

    checkClose(field.penalty(true, 100.0, 50.0, 150.0, 1), 0.0,
               "a branch of the same signal is exempt");

    checkClose(field.penalty(true, 400.0, 50.0, 150.0, 2), 0.0,
               "a lane well away from it costs nothing");

    checkClose(field.penalty(true, 100.0 + 8.0, 50.0, 150.0, 2), 0.0,
               "a lane beyond the tolerance costs nothing");

    checkClose(field.penalty(false, 100.0, 50.0, 150.0, 2), 30.0,
               "crossing it costs one crossing");

    checkClose(field.penalty(false, 100.0, 50.0, 90.0, 2), 0.0,
               "stopping short of it is not a crossing");

    checkClose(field.penalty(false, 300.0, 50.0, 150.0, 2), 0.0,
               "passing beyond its end is not a crossing");
}

void testWiresTakeSeparateLanes() {
    beginTest("Parallel wires are pushed onto separate lanes");

    Model model;
    DiagramScene scene(model);

    Block* a = model.addBlock("Constant", 0, 0);
    Block* b = model.addBlock("Constant", 0, 400);
    Block* mux = model.addBlock("Mux", 600, 180);
    mux->params().set("inputs", 2.0);
    Block* scope = model.addBlock("Scope", 800, 180);

    model.connect(a->id(), 0, mux->id(), 0);
    model.connect(b->id(), 0, mux->id(), 1);
    model.connect(mux->id(), 0, scope->id(), 0);
    scene.rebuild();

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

}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

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
    testAboutNamesTheLicence();
    testUnreadableParameterDoesNotCrash();
    testBlockInspector();
    testWireDropZone();
    testGroupSelectionOnCanvas();
    testScopePlacement(modelPath);
    testWireFieldPenalties();
    testWiresTakeSeparateLanes();

    std::cout << '\n'
              << (g_failures == 0 ? "PASSED" : "FAILED") << "  " << g_checks
              << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
