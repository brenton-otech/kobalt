#include "reader_p.h"

namespace kobalt::reader {
void Reader::showBiblePicker() {
    if (!model->bibleInfo().valid() || model->busy() || closing || services.suspended()) return;
    model->cancelPendingTurn();
    dismissLookup();
    dismissControls();
    page->cancelHold();
    biblePicker->setGeometry(rect());
    biblePicker->open(model->bibleInfo(), model->bibleReference());
}
}
