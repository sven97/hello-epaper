#include "layout.h"
#include "display.h"

LayoutMetrics currentLayout() {
    return computeLayout(epaper.width(), epaper.height());
}
