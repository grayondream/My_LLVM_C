// #include <stdio.h>
// #include <stdlib.h>

// // ===== 宏 / 预处理 =====
// #define MAX(a,b) ((a) > (b) ? (a) : (b))
// #define SQUARE(x) ((x) * (x))

typedef unsigned long ulong;

// ===== 枚举 =====
enum Color {
    RED,
    GREEN = 5,
    BLUE
};

// ===== 结构体 / 联合体 =====
struct Point {
    int x;
    int y;
};

union Data {
    int i;
    float f;
    char str[20];
};

// ===== 全局变量 =====
static int global_var = 10;
extern int external_var;

// ===== 函数声明 =====
int add(int a, int b);
void pointer_demo(int* p);

// ===== 函数定义 =====
int add(int a, int b) {
    return a + b;
}

// ===== 指针 / 地址 / 解引用 =====
void pointer_demo(int* p) {
    int local = 42;
    int* ptr = &local;   // 取地址 &
    *ptr = *ptr + 1;     // 解引用 *
    p = ptr;
}

// ===== 控制流 =====
void control_flow(int n) {
    if (n > 0) {
        printf("positive\n");
    } else if (n == 0) {
        printf("zero\n");
    } else {
        printf("negative\n");
    }

    switch (n) {
        case 1:
            break;
        case 2:
        case 3:
            break;
        default:
            break;
    }

    for (int i = 0; i < n; i++) {
        if (i == 5) continue;
        if (i == 8) break;
    }

    int i = 0;
    while (i < n) {
        i++;
    }

    do {
        i--;
    } while (i > 0);
}

// ===== 数组 / 指针算术 =====
void array_demo() {
    int arr[5] = {1,2,3,4,5};
    int* p = arr;

    for (int i = 0; i < 5; i++) {
        printf("%d\n", *(p + i));
    }
}

// ===== 函数指针 =====
int mul(int a, int b) {
    return a * b;
}

void function_pointer_demo() {
    int (*fp)(int, int) = mul;
    int result = fp(2, 3);
}

// ===== 递归 =====
int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

// ===== 运算符覆盖（Pratt Parser重点）=====
void operator_demo() {
    int a = 5, b = 3;

    int c = a + b * 2;
    int d = (a + b) * 2;

    int e = a & b;
    int f = a | b;
    int g = a ^ b;

    int h = a << 1;
    int i = a >> 1;

    int j = (a > b) ? a : b;

    int k = ++a;
    int l = b--;

    int m = (a += b);

    int n = !a;
    int o = ~b;
}

// ===== sizeof / 类型 =====
void sizeof_demo() {
    int x = 10;
    printf("%zu\n", sizeof(x));
    printf("%zu\n", sizeof(int));
}

// ===== 主函数 =====
int main() {
    struct Point p = {10, 20};
    union Data d;

    d.i = 10;
    d.f = 3.14f;

    int result = add(3, 4);

    pointer_demo(&result);
    control_flow(result);
    array_demo();
    function_pointer_demo();

    int fact = factorial(5);

    operator_demo();
    sizeof_demo();

    printf("Done: %d %d\n", result, fact);

    return 0;
}