int doit1(int n) {
    int x = 0;
    int y = 0;

    // Should not fuse
    for (int i = 0; i < n; ++i) {
        x *= 2;
    }

    for (int i = 0; i < n; ++i) {
        y += x + i;
    }

    return x + y;
}
