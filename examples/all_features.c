// ============================================================================
// My_LLVM_C / SafeModern C 1.0 — 全特性演示（feature tour）
// ============================================================================
// 本文件刻意在同一编译单元里铺开「当前实际实现」的每一类能力。
// 每一节先打印一行标题，随后用 check(...) 断言行为；全部通过 main 返回 0。
//
// 编译 + 运行：
//   ./bin/my_llvm_c -c examples/all_features.c -o /tmp/all_features.o
//   cc /tmp/all_features.o -o /tmp/all_features && /tmp/all_features
//   (退出码 0 = 全部特性验证通过)
//
// 备注：本语言没有预处理宏 / int / float / double 关键字；定宽类型见第 1 节。
// ============================================================================

int32 fails = 0;

// 统一断言助手：msg 为 string_view 字面量；cond 失败则计数并打印。
// 注：本语言比较表达式不能直接作 bool 实参（已知解析局限），故形参用 int32。
void check(int32 c, string_view msg) {
    if (!c) {
        fails = fails + 1;
        println("FAIL: {}", msg);
    }
}

// ---------------------------------------------------------------------------
// 1. 定宽类型与字面量
// ---------------------------------------------------------------------------
void section_types() {
    println("== 1. types & literals ==");
    int8   a8  = -128;
    uint8  u8  = 255;
    int16  a16 = -32768;
    int32  i   = 0b1010;           // 二进制字面量
    uint32 u   = 0xFF;             // 十六进制
    int32  oct = 0o755;            // 八进制
    int64  big = 4000000000;       // 十进制定宽
    uint64 ul  = 4000000000ul;     // ul 后缀
    isize  sz  = 100;
    usize  nz  = 1_000_000;        // 下划线分隔
    float32 f  = 3.5f;
    float64 d  = -1.5;
    float16 h  = 1.5f16;
    float128 q = 2.5f128;
    bool b     = true;
    char c     = '\x41';

    check(a8 == -128, "int8");
    check(u8 == 255, "uint8");
    check(a16 == -32768, "int16");
    check(i == 10, "binary literal");
    check(u == 255, "hex literal");
    check(oct == 493, "octal literal");
    check(big == 4000000000, "int64 literal");
    check(ul == 4000000000, "ul suffix");
    check(sz == 100, "isize");
    check(nz == 1000000, "underscore literal");
    check(f == 3.5f, "float32");
    check(d == -1.5, "float64");
    check(b, "bool");
    check(c == 'A', "char literal");
    println("  f16={} f128={} f32={} f64={}", h, q, f, d);
}

// ---------------------------------------------------------------------------
// 2. typedef / using / type 别名
// ---------------------------------------------------------------------------
typedef int32 Integer;
typedef struct { int32 x; int32 y; } Point;
typedef int32* IntPtr;
typedef int32 IntArr[10];
typedef int32? Opt;                  // optional 别名
using Size = int32;
type Speed = float64;                // `type` 上下文关键字（当前 = typedef）

// 全局 f()：与 namespace io::f() 隔离（第 13 节用）
int32 f() { return 1; }

void section_aliases() {
    println("== 2. typedef / using / type ==");
    Integer n = 7;
    Point p = {1, 2};
    IntPtr ip = null;
    IntArr arr = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    Opt o = {true, 42};
    Size s = 9;
    Speed sp = 3.25;
    check(n == 7, "typedef int32");
    check(p.x + p.y == 3, "anonymous struct typedef");
    check(ip == null, "typedef pointer");
    // 注：数组 typedef 目前不支持下标/slice/sizeof(按指针退化)，仅演示声明。
    check(o.valid && o.value == 42, "optional typedef");
    check(s == 9, "using alias");
    check(sp == 3.25, "type alias");
}

// ---------------------------------------------------------------------------
// 3. struct：聚合初始化 / 嵌套 / 部分初始化 + 布局注解
// ---------------------------------------------------------------------------
struct R { int32 a; int16 b; }                // 结构体定义可无尾分号
[[packed]] struct P { int32 a; int16 b; }
[[align(64)]] struct A { int32 x; }
struct F { int8 a; [[align(16)]] int32 b; }
[[repr(C)]] struct RC { int32 a; float64 b; }

void section_struct() {
    println("== 3. struct + annotations ==");
    struct R r = {10, 20};
    struct R part = {5};                       // 部分初始化，余量零填充
    struct R r2 = {7, 1};
    struct R* rp = &r2;
    rp->a = 9;                                 // 指针成员访问
    check(r.a + r.b == 30, "aggregate init");
    check(part.b == 0, "partial init zero-fill");
    check(rp->a == 9, "struct pointer member access");
    check(compile_time.size_of(P) == 6 && compile_time.offset_of(P, "b") == 4,
          "[[packed]] layout");
    check(compile_time.size_of(A) == 64 && compile_time.align_of(A) == 64,
          "[[align(64)]]");
    check(compile_time.offset_of(F, "b") == 16 && compile_time.size_of(F) == 32,
          "field [[align(16)]]");
    check(compile_time.size_of(RC) == 16 && compile_time.offset_of(RC, "b") == 8,
          "[[repr(C)]] layout");
}

// ---------------------------------------------------------------------------
// 4. union：C 风格共享布局 + 位域成员
// ---------------------------------------------------------------------------
union Data { int32 i; float32 f; }
union UB { uint32 a : 3; }                     // union 位域（RMW 截断）

void section_union() {
    println("== 4. union ==");
    union Data d;
    d.i = 42;
    check(compile_time.size_of(union Data) == 4, "union size = max member");
    union Data e;
    e.f = 1.0f;
    check(e.f == 1.0f, "union float member");
    union UB ub;
    ub.a = 9;                                  // 3 位域 → 截断为 1
    check(ub.a == 1, "union bit-field truncation");
}

// ---------------------------------------------------------------------------
// 5. enum：强类型 + 底层类型
// ---------------------------------------------------------------------------
enum Color { Red, Green, Blue };
enum SmallColor : uint8 { SC0, SC1 };
enum BigColor : uint64 { BC0, BC1 };

void section_enum() {
    println("== 5. enum ==");
    enum Color c = Green;
    check((int32)c == 1, "strong enum value");
    enum SmallColor s = SC1;
    enum BigColor b = BC1;
    check((int32)s == 1, "uint8 underlying enum");
    check(b == BC1, "uint64 underlying enum");
    // 枚举算术提升为整型（Red=0, Blue=2）
    int32 sum = (int32)Red + (int32)Blue;
    check(sum == 2, "enum arithmetic");
    switch (c) {
    case Red:   break;
    case Green: break;
    default:    break;
    }
}

// ---------------------------------------------------------------------------
// 6. 位域：struct / packed / union / class + 无名 + 零宽
// ---------------------------------------------------------------------------
struct BF { int32 a : 3; int32 b : 5; int32 c : 29; }
[[packed]] struct PB { int32 a : 3; int32 b : 3; }
struct PW { int32 a : 3; int32 : 0; int32 b : 3; }   // 无名零宽：对齐到自然边界
struct PAD { int32 a : 3; int32 : 5; int32 b : 3; }  // 无名填充位域

void section_bitfield() {
    println("== 6. bit-fields ==");
    struct BF bf;
    bf.a = 3;                                  // 3 位有符号，最大 3
    bf.b = 15;                                 // 5 位有符号，最大 15
    bf.c = -1;                                 // 29 位有符号 → 读回 -1
    check(bf.a == 3 && bf.b == 15, "bit-field store/load");
    check(bf.c == -1, "signed bit-field sign-extension");
    bf.a = 5;                                  // 3 位溢出 → 回绕为 -3（C 语义）
    check(bf.a == -3, "signed bit-field wrap");
    struct PB pb;
    pb.a = 3; pb.b = 3;                        // packed 区域流共享 [2 x i8]
    check(pb.a == 3 && pb.b == 3, "packed bit-field");
    check(compile_time.size_of(struct PB) == 1, "packed a:3+b:3 = 1 byte");
    struct PW pw;
    pw.a = 3; pw.b = 3;
    check(pw.a == 3 && pw.b == 3, "zero-width unnamed bit-field");
    struct PAD pad;
    pad.a = 3; pad.b = 3;
    check(pad.a == 3 && pad.b == 3, "unnamed padding bit-field");
    bf.a = 2;                                  // 复位再走指针 RMW
    struct BF* bfp = &bf;
    bfp->a = bfp->a + 1;                       // 复合自增走 RMW
    check(bfp->a == 3, "bit-field through pointer");
}

// ---------------------------------------------------------------------------
// 7. class：方法 / 访问控制 / 单继承 / this / 类位域
// ---------------------------------------------------------------------------
class Counter {
public:
    int32 count;
    int32 bf : 3;                              // class 位域
    void inc() { this->count = this->count + 1; }
    int32 get() { return this->count; }
};

class Animal {
public:
    int32 age;
    void setAge(int32 v) { this->age = v; }
};

class Dog : public Animal {
public:
    int32 legs;
    int32 bark() { return this->age * 10 + this->legs; }
};

void section_class() {
    println("== 7. class + inheritance ==");
    Counter ctr;
    ctr.count = 0;
    ctr.inc();
    ctr.inc();
    check(ctr.get() == 2, "method + this");
    ctr.bf = 3;                                // 3 位有符号最大 3
    check(ctr.bf == 3, "class bit-field");
    Counter* cp = &ctr;
    cp->inc();
    check(cp->get() == 3, "method via pointer");
    Dog d;
    d.setAge(4);
    d.legs = 4;
    check(d.bark() == 44, "inherited method + field");
    check(compile_time.size_of(Dog) == 8, "derived layout (base slot)");
}

// ---------------------------------------------------------------------------
// 8. 指针：多级 / 算术 / 函数指针 / libc qsort·bsearch 回调
// ---------------------------------------------------------------------------
int32 add(int32 a, int32 b) { return a + b; }
int32 mul(int32 a, int32 b) { return a * b; }
int32 apply(int32 (*cb)(int32, int32), int32 a, int32 b) { return cb(a, b); }
int32 cmp_int(void* a, void* b) {
    int32* x = (int32*)a;
    int32* y = (int32*)b;
    if (*x < *y) return -1;
    if (*x > *y) return 1;
    return 0;
}

void section_pointers() {
    println("== 8. pointers ==");
    int32 v = 7;
    int32* p = &v;
    int32** pp = &p;
    **pp = 11;                                 // 多级指针写入
    check(v == 11, "pointer to pointer");
    int32 arr[5] = {1, 2, 3, 4, 5};
    int32* q = arr;                            // 数组退化
    check(*(q + 2) == 3, "pointer arithmetic");
    check(*(q + 4) - *(q + 1) == 3, "pointer subtraction");
    check(q == arr, "array decay");
    int32 (*fp)(int32, int32) = add;           // 函数指针变量
    check(fp(3, 4) == 7, "function pointer call");
    check(apply(mul, 6, 7) == 42, "callback param");
    int32 ns[5] = {5, 3, 4, 1, 2};
    qsort(ns, 5, 4, cmp_int);                  // libc 回调
    int32 sorted = 1;
    int32 i = 0;
    while (i < 4) {
        if (ns[i] > ns[i + 1]) sorted = 0;
        i = i + 1;
    }
    check(sorted == 1, "qsort callback");
    int32 key = 4;
    int32* found = (int32*)bsearch(&key, ns, 5, 4, cmp_int);
    check(found != null && *found == 4, "bsearch callback");
}

// ---------------------------------------------------------------------------
// 9. 数组（多维 / 参数 / 退化）+ 切片 T[]
// ---------------------------------------------------------------------------
void bump(int32[] s) { s[0] = s[0] + 100; }    // 切片参数，零拷贝视图
int64 sum2d(int32 m[2][3]) {
    int64 total = 0;
    int32 i = 0;
    while (i < 2) {
        int32 j = 0;
        while (j < 3) {
            total = total + m[i][j];
            j = j + 1;
        }
        i = i + 1;
    }
    return total;
}

void section_arrays_slice() {
    println("== 9. arrays + slices ==");
    int32 m[2][3] = {{1, 2, 3}, {4, 5, 6}};
    check(m[1][2] == 6, "multidim subscript");
    check(sum2d(m) == 21, "multidim param");
    int32 a[3] = {7, 8, 9};
    int32[] s = a;                             // 数组 → 切片
    check(s.len == 3 && s[1] == 8, "slice view");
    bump(a);
    check(a[0] == 107, "slice write visible to caller");
    int32[] empty;
    check(empty.len == 0, "uninitialized slice is empty");
}

// ---------------------------------------------------------------------------
// 10. 字符串：string_view（零拷贝视图）/ string（深拷贝所有权）/ format / split
// ---------------------------------------------------------------------------
class Greeter {
public:
    int32 id;
    char* to_string() { return "Greeter!"; }   // format 自定义渲染
};

void section_strings() {
    println("== 10. strings ==");
    string_view sv = "hello";
    check(sv.len() == 5, "string_view.len");
    string_view uni = "h\xc3\xa9llo";
    check(uni.len() == 6 && uni.char_count() == 5, "UTF-8 len vs char_count");
    check(sv.find("ll") == 2, "find");
    check(sv.sub(1, 3) == "el", "sub");
    char* cp = sv;                             // string_view → char*
    check(cp[0] == 'h', "string_view -> char*");

    string s = string.new("ab");               // 所有权字符串
    s.append("cd");
    s.push('!');
    check(s.len() == 5 && s[2] == 'c', "string.append/push/subscript");
    check(s.capacity() >= 5, "string.capacity");
    string_view back = s;                      // string → string_view
    check(back == "abcd!", "string -> string_view");
    s.destroy();
    s.destroy();                               // 双销毁安全
    check(s.len() == 0, "destroy zeroes");

    string f = format("x={} y={}", 7, -3);     // format 返回新 string
    string_view fv = f;                        // string → string_view 投影
    check(fv == "x=7 y=-3", "format basic");
    string g = format("{:x} {:X} {:o} {:b}", 255, 255, 8, 5);
    string_view gv = g;
    check(gv == "ff FF 10 101", "format int specs");
    string h = format("{:>5}|{:<5}|", 42, 42);
    string_view hv = h;
    check(hv == "   42|42   |", "format width/align");
    string k = format("{:.2f} {:e}", 3.14159, 31415.926);
    string_view kv = k;
    check(kv == "3.14 3.141593e+04", "format float");
    string esc = format("{{a}}");
    string_view ev = esc;
    check(ev == "{a}", "format escape");
    f.destroy(); g.destroy(); h.destroy(); k.destroy(); esc.destroy();

    string_view[] parts = split("a,b,c", ","); // 零拷贝视图数组
    check(parts.len == 3 && parts[1] == "b", "split");
    split_destroy(parts);

    Greeter gr;
    gr.id = 1;
    string viaToString = format("{}", gr);     // 自定义 to_string()
    string_view tv = viaToString;
    check(tv == "Greeter!", "format to_string");
    viaToString.destroy();
}

// ---------------------------------------------------------------------------
// 11. Optional / Result
// ---------------------------------------------------------------------------
result<int32, int32> divide(int32 a, int32 b) {
    if (b == 0) {
        result<int32, int32> e = {false, 0, -1};
        return e;
    }
    result<int32, int32> s = {true, a / b, 0};
    return s;
}

void section_optional_result() {
    println("== 11. optional / result ==");
    int32? o = {true, 42};
    check(o.valid && o.value == 42, "optional");
    int32? none = {false, 0};
    check(!none.valid, "invalid optional");
    result<int32, int32> r = divide(10, 2);
    check(r.ok && r.value == 5, "result ok");
    result<int32, int32> er = divide(10, 0);
    check(!er.ok && er.error == -1, "result error");
    int32? g;
    check(!g.valid, "zero-init optional");
    optional<optional<int32>> nest = {true, {false, 0}};
    check(nest.valid && !nest.value.valid, "nested optional");
}

// ---------------------------------------------------------------------------
// 12. 模板 / 泛型：类模板 / 函数模板 / 别名模板 / CRTP
// ---------------------------------------------------------------------------
template<typename T> struct Box { T value; };
template<typename T, usize N> struct Arr { T data[N]; };
template<typename T> using Vec = Arr<T, 2>;
template<typename T> T tmax(T a, T b) { return a; }

template<typename D>
class Shape {
public:
    float64 twice_area() { return static_cast<D*>(this)->area() * 2.0; }
};
class Circle : Shape<Circle> {
public:
    float64 r;
    float64 area() { return 3.14159 * this->r * this->r; }
};

void section_templates() {
    println("== 12. templates ==");
    Box<int32> b;
    b.value = 42;
    check(b.value == 42, "class template");
    Box<Box<int32>> nb;
    nb.value.value = 7;
    check(nb.value.value == 7, "nested template");
    Arr<int32, 4> a;
    a.data[0] = 1; a.data[1] = 2; a.data[2] = 3; a.data[3] = 4;
    check(a.data[0] + a.data[3] == 5, "template with non-type param");
    Vec<int32> v;
    v.data[0] = 5; v.data[1] = 6;
    check(v.data[0] * 10 + v.data[1] == 56, "alias template");
    check(tmax(3, 4) == 3, "function template (deduction)");
    bool ft = tmax<float64>(2.5, 1.5) == 2.5;
    check(ft, "function template (explicit)");
    Circle c;
    c.r = 1.0;
    float64 t = c.twice_area();
    check(t > 6.0 && t < 6.3, "CRTP static dispatch");
}

// ---------------------------------------------------------------------------
// 13. namespace：限定名 / 嵌套 / 类型隔离
// ---------------------------------------------------------------------------
namespace geometry {
    struct Point { int32 x; int32 y; };
    int32 dist1(Point* p) { return p->x + p->y; }
    int32 f() { return 7; }
}
namespace A.B { int32 deep() { return 9; } }
namespace io {
    int32 f() { return 2; }
}

void section_namespace() {
    println("== 13. namespace ==");
    check(geometry::f() == 7, "qualified function");
    geometry::Point pt;
    pt.x = 3; pt.y = 4;
    check(geometry::dist1(&pt) == 7, "qualified struct type + fn");
    check(A::B::deep() == 9, "nested namespace");
    check(f() == 1 && io::f() == 2, "namespace vs global isolation");
}

// ---------------------------------------------------------------------------
// 14. 函数重载
// ---------------------------------------------------------------------------
int32 twice(int32 x) { return x * 2; }
int32 twice(int64 x) { return (int32)(x * 3); }
float64 twice(float64 x) { return x * 4.0; }

void section_overload() {
    println("== 14. overloading ==");
    check(twice(5) == 10, "overload int32");
    int64 lv = 5;
    check(twice(lv) == 15, "overload int64");
    check(twice(2.5) == 10.0, "overload float64");
}

// ---------------------------------------------------------------------------
// 15. 控制流：for / while / switch 穿透 / break / continue / defer
// ---------------------------------------------------------------------------
int32 trace = 0;
void tick(int32 v) { trace = trace * 10 + v; }

void section_controlflow() {
    println("== 15. control flow ==");
    int32 sum = 0;
    for (int32 i = 0; i <= 10; i = i + 1) {
        sum = sum + i;
    }
    check(sum == 55, "for loop");

    int32 w = 0;
    while (w < 3) {
        w = w + 1;
    }
    check(w == 3, "while loop");

    int32 sw = 0;
    int32 n = 2;
    switch (n) {
    case 1: sw = 10; break;
    case 2:             // 穿透到 case 3
    case 3: sw = 20; break;
    default: sw = 30;
    }
    check(sw == 20, "switch fallthrough");

    int32 cont = 0;
    for (int32 i = 0; i < 5; i = i + 1) {
        if (i == 2) { continue; }
        if (i == 4) { break; }
        cont = cont + 1;
    }
    check(cont == 3, "break/continue");

    trace = 0;
    {
        defer tick(1);
        defer tick(2);
        defer tick(3);
    }
    check(trace == 321, "defer reverse order");
}

// ---------------------------------------------------------------------------
// 16. 运算符：三元 / 位运算 / 移位 / 自增自减 / 逻辑短路
// ---------------------------------------------------------------------------
void section_operators() {
    println("== 16. operators ==");
    int32 x = 5;
    int32 tv = x == 5 ? 7 : 0;
    check(tv == 7, "ternary");
    check((x & 1) == 1, "bitwise and");
    check((x | 8) == 13, "bitwise or");
    check((x ^ 1) == 4, "bitwise xor");
    check(~x == -6, "bitwise not");
    check((1 << 4) == 16, "left shift");
    check((32 >> 2) == 8, "right shift");
    int32 pre = ++x;
    check(pre == 6 && x == 6, "prefix ++");
    int32 post = x--;
    check(post == 6 && x == 5, "postfix --");
    int32 calls = 0;
    int32 shorty = 0;
    check(!(shorty && (calls = calls + 1)), "short-circuit &&");
    check(calls == 0, "RHS of && not evaluated");
    check(!shorty && (shorty || 1), "short-circuit ||");
}

// ---------------------------------------------------------------------------
// 17. 显式转换：static_cast / reinterpret_cast / C 风格
// ---------------------------------------------------------------------------
struct Base { int32 b; };
struct Derived { int32 b; int32 d; };

void section_casts() {
    println("== 17. casts ==");
    int32 t = static_cast<int32>(3.7);         // 浮点截断
    check(t == 3, "static_cast float->int");
    int64 wid = static_cast<int64>(41);
    check((int32)wid == 41, "static_cast widen");
    float32 one = 1.0f;
    int32 bits = reinterpret_cast<int32>(one); // 1.0f == 0x3F800000 == 1065353216
    check(bits == 1065353216, "reinterpret_cast float bits");
    float32 back = reinterpret_cast<float32>(bits);
    check(back == 1.0f, "reinterpret_cast int->float");
    struct Derived dv;
    dv.b = 1; dv.d = 2;
    struct Base* bp = static_cast<struct Base*>(&dv);
    check(bp->b == 1, "static_cast pointer to base");
    int32 val = 7;
    void* vp = (void*)&val;
    int32* rp = (int32*)vp;
    check(*rp == 7, "C-style cast round trip");
}

// ---------------------------------------------------------------------------
// 18. constexpr 常量 / constexpr 函数 / compile_time 查询与条件编译
// ---------------------------------------------------------------------------
constexpr usize N = compile_time.size_of(float64);
constexpr int32 ct_twice(int32 x) { return x * 2; }
constexpr int32 ct42 = compile_time.size_of(int32) * 0 + ct_twice(21);

compile_time.static_assert(compile_time.size_of(int32) == 4, "int32 is 4 bytes");
compile_time.if (compile_time.target.os == "linux") {
    int32 ct_pick() { return 42; }
} else {
    int32 ct_pick() { return NoSuchSymbolAnywhere(); }
}

void section_compile_time() {
    println("== 18. constexpr + compile_time ==");
    check(N == 8, "compile_time.size_of const");
    check(ct42 == 42, "constexpr function in ct expr");
    check(compile_time.size_of(struct BF) == 8, "size_of aggregate");
    check(compile_time.align_of(float64) == 8, "align_of");
    check(compile_time.offset_of(struct R, "b") == 4, "offset_of");
    check(ct_pick() == 42, "compile_time.if selects branch");
    bool isLinux = compile_time.target.os == "linux";
    check(isLinux, "compile_time.target.os");
}

// ---------------------------------------------------------------------------
// 19. 内联汇编（x86-64）
// ---------------------------------------------------------------------------
void section_asm() {
    println("== 19. inline asm ==");
    int32 out;
    asm volatile("mov $7, %0" : "=r"(out));
    check(out == 7, "asm output register");
    int32 x = 5;
    int32 y = 3;
    asm("add %1, %0" : "+r"(x) : "r"(y));
    check(x == 8, "asm read-write operand");
    asm volatile("nop");
}

// ---------------------------------------------------------------------------
// 20. libc 绑定层（自动注入的 std.c）
// ---------------------------------------------------------------------------
void section_libc() {
    println("== 20. libc binding ==");
    void* p = malloc(64);
    check(p != null, "malloc");
    char* buf = (char*)p;
    char* src = "hello";                       // string_view → char*
    memcpy(buf, src, 5);
    buf[5] = 0;
    check(strlen(buf) == 5, "memcpy + strlen");
    free(p);
    check(abs(-42) == 42, "abs");
    int32* dyn = (int32*)calloc(3, 4);
    check(dyn[0] == 0 && dyn[1] == 0, "calloc zeroes");
    free(dyn);
    puts("  [libc] puts OK");
    printf("  [libc] printf: %d %s\n", 42, "str");
}

// 全局 f() 已在文件前部定义（第 13 节 namespace 隔离用）

int32 main() {
    section_types();
    section_aliases();
    section_struct();
    section_union();
    section_enum();
    section_bitfield();
    section_class();
    section_pointers();
    section_arrays_slice();
    section_strings();
    section_optional_result();
    section_templates();
    section_namespace();
    section_overload();
    section_controlflow();
    section_operators();
    section_casts();
    section_compile_time();
    section_asm();
    section_libc();
    if (fails == 0) {
        println("ALL FEATURES OK");
    }
    return fails;
}
