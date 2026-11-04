#include "bible_picker.h"
#include "interfaces/reader/ui/controls/buttons.h"
#include "interfaces/reader/ui/controls/rows.h"
#include "interfaces/reader/ui/controls/paging.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "interfaces/reader/ui/style/styles.h"
#include "support/strings.h"
#include <QPainter>

namespace kobalt {
namespace c = ui::metrics::chrome;
BiblePicker::BiblePicker(QWidget *parent) : ui::Panel(parent) {
    setObjectName("rrBiblePicker");
    back = new ui::CellButton(ui::ControlIcon::Back, this, c::backIcon);
    back->setObjectName("rrBibleBack"); back->setRule(Qt::RightEdge);
    back->setAccessibleName(strings::get("common.back"));
    close = new ui::CellButton(ui::ControlIcon::Close, this);
    close->setObjectName("rrBibleClose"); close->setRule(Qt::LeftEdge);
    close->setAccessibleName(strings::get("bible.close"));
    connect(close, &QPushButton::clicked, this, [this] { hide(); if (dismissed) dismissed(); });
    connect(back, &QPushButton::clicked, this, [this] {
        if (stage) { --stage; page = 0; redraw(); }
        else { hide(); if (dismissed) dismissed(); }
    });
    title = new ui::InterfaceLabel({}, this);
    title->setTextFormat(Qt::PlainText);
    ui::readerStyle(title, ui::styles::brand(c::titleText) + "background:transparent;");
    heading = new ui::ListHeader(this);
    const char *codes[] = {"OT", "NT", "AP"};
    const char *keys[] = {"bible.old_testament", "bible.new_testament", "bible.apocrypha"};
    for (int i = 0; i < 3; ++i) {
        auto *tab = new ui::Button(strings::get(keys[i]), this);
        tab->setObjectName(QString("rrBibleTestament%1").arg(codes[i]));
        tab->setCheckable(true);
        ui::readerStyle(tab, ui::styles::outlined() + "QPushButton{" + ui::styles::font(c::tabText) + "}");
        const auto code = QString::fromLatin1(codes[i]);
        connect(tab, &QPushButton::clicked, this, [this, code] { testament = code; page = 0; redraw(); });
        tabs.append(tab);
    }
    pager = new ui::PagerBar(this, "rrBible");
    connect(pager->previous, &QPushButton::clicked, this, [this] { page = pager->turnTarget(-1); arrange(); });
    connect(pager->next, &QPushButton::clicked, this, [this] { page = pager->turnTarget(1); arrange(); });
    hide();
}
void BiblePicker::open(const BibleInfo &value, BibleReference ref) {
    info = value; current = ref;
    if (!info.valid()) return;
    book = info.contains(ref) ? ref.book : 0;
    chapter = info.contains(ref) ? ref.chapter : 0;
    testament = info.books[book].testament;
    stage = page = 0;
    redraw(); show(); raise();
}
void BiblePicker::redraw() {
    items.clear();
    setProperty("stage", stage);
    title->setText(!stage ? strings::get("bible.books") : stage == 1 ? info.books[book].name :
                   info.books[book].name + ' ' + info.books[book].chapters[chapter].label);
    heading->setCaptions(info.id, strings::get(stage == 0 ? "bible.choose_book" : stage == 1 ? "bible.choose_chapter" : "bible.choose_verse"));
    for (int i = 0; i < tabs.size(); ++i) {
        const auto code = QStringList{"OT", "NT", "AP"}[i];
        bool present = false;
        for (const auto &entry : info.books) present = present || entry.testament == code;
        tabs[i]->setVisible(!stage && present);
        tabs[i]->setChecked(testament == code);
    }
    if (!stage) {
        for (int b = 0; b < info.books.size(); ++b) if (info.books[b].testament == testament)
            items.append({info.books[b].name, b, current.book == b});
    } else if (stage == 1) {
        for (int ch = 0; ch < info.books[book].chapters.size(); ++ch)
            items.append({info.books[book].chapters[ch].label, ch, current.book == book && current.chapter == ch});
    } else {
        for (int v = 1; v <= info.books[book].chapters[chapter].verses; ++v)
            items.append({QString::number(v), v, current.book == book && current.chapter == chapter && current.verse == v});
    }
    arrange();
}
void BiblePicker::pick(int value) {
    page = 0;
    if (!stage) { book = value; stage = 1; redraw(); }
    else if (stage == 1) { chapter = value; stage = 2; redraw(); }
    else {
        const auto target = QStringLiteral("bible:") + info.key({book, chapter, value});
        hide();
        if (selected) selected(target);
    }
}
void BiblePicker::arrange() {
    const int bar = c::actionHeight - c::heavyRule;
    back->setGeometry(0, 0, c::cellWidth, bar);
    close->setGeometry(width() - c::cellWidth, 0, c::cellWidth, bar);
    title->setGeometry(c::cellWidth + c::inset, 0, qMax(1, width() - 2 * (c::cellWidth + c::inset)), bar);
    heading->setGeometry(0, c::actionHeight, width(), ui::metrics::list::headerHeight);
    int top = c::actionHeight + ui::metrics::list::headerHeight;
    if (!stage) {
        int visible = 0;
        for (auto *tab : tabs) visible += !tab->isHidden();
        int column = 0;
        for (auto *tab : tabs) if (!tab->isHidden()) {
            tab->setGeometry(column * width() / qMax(1, visible), top, width() / qMax(1, visible), ui::px(88)); ++column;
        }
        top += ui::px(88);
    }
    int columns = 3;
    if (stage) {
        int wanted = ui::px(112);
        for (const auto &item : items)
            wanted = qMax(wanted, ui::interfaceTextWidth(item.label, ui::interfaceFont(c::labelText)) + 2 * int(ui::metrics::spacing::row));
        columns = qMax(1, (width() - 2 * c::inset) / wanted);
    }
    const int gap = ui::metrics::spacing::row;
    const int cellHeight = ui::metrics::list::twoLines;
    const int rowCount = qMax(1, (height() - top - c::actionHeight - 2 * gap) / (cellHeight + gap));
    const int capacity = columns * rowCount;
    const int count = qMax(1, (int(items.size()) + capacity - 1) / capacity);
    page = qBound(0, page, count - 1);
    pager->setPage(page, count); pager->setVisible(count > 1);
    pager->setGeometry(0, height() - c::actionHeight, width(), c::actionHeight);
    const int shown = qMin(capacity, int(items.size()) - page * capacity);
    while (cells.size() < shown) {
        auto *cell = new ui::Button({}, this);
        cell->setCheckable(true);
        auto *label = new ui::InterfaceLabel({}, cell);
        label->setObjectName("bibleCellLabel"); label->setWordWrap(true); label->setTextFormat(Qt::PlainText);
        label->setAlignment(Qt::AlignCenter); label->setAttribute(Qt::WA_TransparentForMouseEvents);
        ui::readerStyle(cell, ui::styles::outlined());
        connect(cell, &QPushButton::clicked, this, [this, cell] { pick(cell->property("value").toInt()); });
        cells.append(cell);
    }
    const int cellWidth = qMax(1, (width() - 2 * c::inset - (columns - 1) * gap) / columns);
    for (int i = 0; i < cells.size(); ++i) {
        auto *cell = cells[i]; cell->setVisible(i < shown);
        if (i >= shown) continue;
        const auto &item = items[page * capacity + i];
        cell->setObjectName(QString("rrBible%1%2").arg(stage == 0 ? "Book" : stage == 1 ? "Chapter" : "Verse").arg(item.value));
        cell->setProperty("value", item.value); cell->setChecked(item.checked);
        cell->setAccessibleName(item.label);
        cell->setGeometry(c::inset + (i % columns) * (cellWidth + gap), top + gap + (i / columns) * (cellHeight + gap), cellWidth, cellHeight);
        auto *label = cell->findChild<QLabel *>("bibleCellLabel"); label->setText(item.label);
        label->setGeometry(gap, gap, qMax(1, cellWidth - 2 * gap), cellHeight - 2 * gap);
        ui::readerStyle(label, ui::styles::font(c::labelText) + (item.checked ? "color:palette(window);" : "color:palette(window-text);") + "background:transparent;");
    }
    pager->setBlankArea({0, top + gap + rowCount * (cellHeight + gap), width(), 0});
}
void BiblePicker::resizeEvent(QResizeEvent *) { if (info.valid()) arrange(); }
void BiblePicker::paintEvent(QPaintEvent *event) {
    ui::Panel::paintEvent(event);
    QPainter p(this);
    p.fillRect(0, c::actionHeight - c::heavyRule, width(), c::heavyRule, palette().color(QPalette::WindowText));
}
}
