void doit1(int *a, int *b, int *c, int *d, int n) {
    for (int i = 0; i < n; i++) {
        a[i] = c[i] + 10;
    }
    for (int i = 0; i < n; i++) {
        b[i] = c[i] * d[i] + a[i+1];
    }
}
