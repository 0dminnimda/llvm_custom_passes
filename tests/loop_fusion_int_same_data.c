void doit1(int *a, int n) {
    for (int i = 0; i < n; i++) {
        a[i] = a[i] + 3;
    }
    for (int i = 0; i < n; i++) {
        a[i] = a[i] * 5;
    }
    for (int i = 0; i < n; i++) {
        a[i] = a[i] >> 10;
    }
    for (int i = 0; i < n; i++) {
        a[i] = a[i] & 0xFF;
    }
}

void doit2(int *a, int n) {
    for (int i = 0; i < n; i++) {
        a[i] = a[i] + 3;
        a[i] = a[i] * 5;
        a[i] = a[i] >> 10;
        a[i] = a[i] & 0xFF;
    }
}
