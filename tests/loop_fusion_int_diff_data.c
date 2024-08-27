void doit1(int *a, int n) {
    int b[n];
    for (int i = 0; i < n; i++) {
        b[i] = a[i] + 3;
    }
    int c[n];
    for (int i = 0; i < n; i++) {
        c[i] = b[i] * 5;
    }
    int d[n];
    for (int i = 0; i < n; i++) {
        d[i] = c[i] >> 10;
    }
    for (int i = 0; i < n; i++) {
        a[i] = d[i] & 0xFF;
    }
}

void doit2(int *a, int n) {
    int b[n];
    int c[n];
    int d[n];
    for (int i = 0; i < n; i++) {
        b[i] = a[i] + 3;
        c[i] = b[i] * 5;
        d[i] = c[i] >> 10;
        a[i] = d[i] & 0xFF;
    }
}
