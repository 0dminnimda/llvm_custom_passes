int doit1(int n) {
    int x = 1;
    int y = 1;

    for (int i = 1; i < 0xFFFF; i *= 2) {
        x = x && (i % 5) > 2;
    }

    for (int i = 1; i < 0xFFFF; i *= 2) {
        y = y && (i % 7) > 2;
    }

    return x + y;
}

int doit2(int n) {
    int x = 1;
    int y = 1;

    for (int i = 1; i < 0xFFFF; i *= 2) {
        x = x && (i % 5) > 2;
        y = y && (i % 7) > 2;
    }

    return x + y;
}

