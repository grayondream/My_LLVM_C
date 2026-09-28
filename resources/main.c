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
    int32 x;
    int32 y;
};

union Data {
    int32 i;
    float32 f;
    char str[20];
};

// ===== 全局变量 =====
static int32 global_var = 10;
extern int32 external_var;

// ===== 函数声明 =====
int32 add(int32 a, int32 b);
void pointer_demo(int32* p);

// ===== 函数定义 =====
int32 add(int32 a, int32 b) {
    return a + b;
}

// ===== 指针 / 地址 / 解引用 =====
void pointer_demo(int32* p) {
    int32 local = 42;
    int32* ptr = &local;   // 取地址 &
    *ptr = *ptr + 1;     // 解引用 *
    p = ptr;
}

// ===== 控制流 =====
void control_flow(int32 n) {
    if (n > 0) {
        println("positive");
    } else if (n == 0) {
        println("zero");
    } else {
        println("negative");
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

    for (int32 i = 0; i < n; i++) {
        if (i == 5) continue;
        if (i == 8) break;
    }

    int32 i = 0;
    while (i < n) {
        i++;
    }

    do {
        i--;
    } while (i > 0);
}

// ===== 数组 / 指针算术 =====
void array_demo() {
    int32 arr[5] = {1,2,3,4,5};
    int32* p = arr;

    for (int32 i = 0; i < 5; i++) {
        println("{}", *(p + i));
    }
}

// ===== 函数指针 =====
int32 mul(int32 a, int32 b) {
    return a * b;
}

void function_pointer_demo() {
    int32 (*fp)(int32, int32) = mul;
    int32 result = fp(2, 3);
}

// ===== 递归 =====
int32 factorial(int32 n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

// ===== 运算符覆盖（Pratt Parser重点）=====
void operator_demo() {
    int32 a = 5, b = 3;

    int32 c = a + b * 2;
    int32 d = (a + b) * 2;

    int32 e = a & b;
    int32 f = a | b;
    int32 g = a ^ b;

    int32 h = a << 1;
    int32 i = a >> 1;

    int32 j = (a > b) ? a : b;

    int32 k = ++a;
    int32 l = b--;

    int32 m = (a += b);

    int32 n = !a;
    int32 o = ~b;
}

// ===== sizeof / 类型 =====
void sizeof_demo() {
    int32 x = 10;
    println("{}", sizeof(x));
    println("{}", sizeof(int32));
}

// ===== 主函数 =====
int32 main() {
    struct Point p = {10, 20};
    union Data d;

    d.i = 10;
    d.f = 3.14f;

    int32 result = add(3, 4);

    pointer_demo(&result);
    control_flow(result);
    array_demo();
    function_pointer_demo();

    int32 fact = factorial(5);

    operator_demo();
    sizeof_demo();

    println("Done: {} {}", result, fact);

    return 0;
}