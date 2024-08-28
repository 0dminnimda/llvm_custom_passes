int doit1(int n) {
    int x = 0;
    int y = 0;

    for (int k = 0; k < 20; ++k) {
        x -= 2;

        for (int i = 0; i < n; ++i) {
            x *= 2;
        }
    }

    for (int k = 0; k < 20; ++k) {
        for (int i = 0; i < n; ++i) {
            y += 3 + i;
        }

        y *= 3;
    }

    return x + y;
}

int doit2(int n) {
    int x = 0;
    int y = 0;

    for (int k = 0; k < 20; ++k) {
        x -= 2;

        for (int i = 0; i < n; ++i) {
            x *= 2;
        }

        for (int i = 0; i < n; ++i) {
            y += 3 + i;
        }

        y *= 3;
    }

    return x + y;
}

int doit3(int n) {
    int x = 0;
    int y = 0;

    for (int k = 0; k < 20; ++k) {
        x -= 2;

        for (int i = 0; i < n; ++i) {
            x *= 2;
            y += 3 + i;
        }

        y *= 3;
    }


    return x + y;
}
