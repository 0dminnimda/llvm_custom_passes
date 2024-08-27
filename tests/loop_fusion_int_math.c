int doit1(int n) {
    int x = 0;
    int y = 0;

    for (int i = 0; i < n; ++i) {
        x *= 2;
    }

    for (int i = 0; i < n; ++i) {
        y += 3 + i;
    }

    return x + y;
}

int doit2(int n) {
    int x = 0;
    int y = 0;

    for (int i = 0; i < n; ++i) {
        x *= 2;
        y += 3 + i;
    }

    return x + y;
}
