#include "BLEKeyCast.hpp"

static BLEKeyCast app;

void setup() {
    app.begin();
}

void loop() {
    app.update();
}
