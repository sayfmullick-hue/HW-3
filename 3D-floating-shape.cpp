// build: g++ -std=c++17 -o 3D-floating-shape main.cpp -lX11 -lm
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <cmath>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <algorithm>

constexpr int W = 800, H = 600;
constexpr double FOCAL = 500.0;
constexpr uint32_t BG_COLOR    = 0xFFFFFF;  // white
constexpr uint32_t AXIS_COLOR  = 0x000000;  // black
constexpr uint32_t EDGE_COLORS[3] = {
    0xFF0000,   // red
    0x00AA00,   // green (darker than pure 0x00FF00 so it's readable on white)
    0x0000FF    // blue
};

struct Vec3 { double x, y, z; };
struct Edge { int a, b; };
struct Shape { std::vector<Vec3> v; std::vector<Edge> e; };
struct Camera { double x = 5, y = 7, z = 70, yaw = 0; };

std::vector<uint32_t> fb(W * H);

void clearFB(uint32_t c) { std::fill(fb.begin(), fb.end(), c); }

void putPixel(int x, int y, uint32_t c) {
    if (x >= 0 && x < W && y >= 0 && y < H) fb[y * W + x] = c;
}

// Bresenham line
void drawLine(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    while (true) {
        putPixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

Vec3 rotateY(Vec3 p, double a) {
    return { p.x * cos(a) + p.z * sin(a), p.y, -p.x * sin(a) + p.z * cos(a) };
}

Vec3 rotateX(Vec3 p, double a) {
    return { p.x,
             p.y * cos(a) - p.z * sin(a),
             p.y * sin(a) + p.z * cos(a) };
}

// World point -> camera space (translate by camera, then rotate by head yaw)
Vec3 toCamera(Vec3 p, const Camera& cam) {
    return rotateY({p.x - cam.x, p.y - cam.y, p.z - cam.z}, cam.yaw);
}

// Camera-space point -> screen pixel
void toScreen(Vec3 r, int& sx, int& sy) {
    double depth = -r.z;                       // camera looks along -z
    sx = (int)(W / 2 + r.x * FOCAL / depth);
    sy = (int)(H / 2 - r.y * FOCAL / depth);
}

// Draw a 3D line, clipping it against the near plane.
// ox/oy are optional pixel offsets (used to thicken the axes).
void drawLine3D(Vec3 a, Vec3 b, const Camera& cam, uint32_t color, 
				int ox = 0, int oy = 0) {
    const double NEAR = 1.0;
    Vec3 p = toCamera(a, cam), q = toCamera(b, cam);
    double dp = -p.z, dq = -q.z;               // depths

    if (dp < NEAR && dq < NEAR) return;        // entirely behind the viewer

    if (dp < NEAR) {                           // clip p up to the near plane
        double t = (NEAR - dp) / (dq - dp);
        p = { p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, p.z + (q.z - p.z) * t };
    } else if (dq < NEAR) {                    // clip q up to the near plane
        double t = (NEAR - dq) / (dp - dq);
        q = { q.x + (p.x - q.x) * t, q.y + (p.y - q.y) * t, q.z + (p.z - q.z) * t };
    }

    int x0, y0, x1, y1;
    toScreen(p, x0, y0);
    toScreen(q, x1, y1);
    drawLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy, color);
}

Shape makePyramid() {
    Shape s;
    s.v = {{-5,-5,-5},{5,-5,-5},{5,-5,5},{-5,-5,5},{0,5,0}};
    s.e = {{0,1},{1,2},{2,3},{3,0},{0,4},{1,4},{2,4},{3,4}};
    return s;
}

Shape makePrism() {
    Shape s;
    s.v = {{-5,-4, 5},{5,-4, 5},{0,5, 5},
           {-5,-4,-5},{5,-4,-5},{0,5,-5}};
    s.e = {{0,1},{1,2},{2,0},{3,4},{4,5},{5,3},{0,3},{1,4},{2,5}};
    return s;
}

Shape makeFish() {
    Shape s;
    constexpr double PI = 3.14159265358979;
    constexpr int SEG = 8;       // points around each ring
    constexpr int RINGS = 4;

    // Body rings along the x axis (nose points toward +x)
    const double ringX[RINGS]  = { -4,  0,   4,   7   };
    const double ringRy[RINGS] = {  1.5, 4,  4.5,  2.5 };  // height (oval is taller...
    const double ringRz[RINGS] = {  1,   2.5, 3,   1.5 };  // ...than it is wide)

    // Ring vertices: index = ring * SEG + k
    for (int r = 0; r < RINGS; ++r) {
        for (int k = 0; k < SEG; ++k) {
            double ang = 2 * PI * k / SEG;
            s.v.push_back({ ringX[r], ringRy[r] * cos(ang), ringRz[r] * sin(ang) });
        }
    }

    // Nose, tail joint, and the two tail-fin tips
    int nose = (int)s.v.size();      s.v.push_back({ 10, 0, 0 });
    int joint = (int)s.v.size();     s.v.push_back({ -4, 0, 0 });
    int tailTop = (int)s.v.size();   s.v.push_back({ -10,  5, 0 });
    int tailBot = (int)s.v.size();   s.v.push_back({ -10, -5, 0 });

    // Edges around each ring
    for (int r = 0; r < RINGS; ++r)
        for (int k = 0; k < SEG; ++k)
            s.e.push_back({ r * SEG + k, r * SEG + (k + 1) % SEG });

    // Edges lengthwise between rings
    for (int r = 0; r < RINGS - 1; ++r)
        for (int k = 0; k < SEG; ++k)
            s.e.push_back({ r * SEG + k, (r + 1) * SEG + k });

    // Nose: last ring to the nose point
    for (int k = 0; k < SEG; ++k)
        s.e.push_back({ (RINGS - 1) * SEG + k, nose });

    // Tail: a flat triangle
    s.e.push_back({ joint, tailTop });
    s.e.push_back({ joint, tailBot });
    s.e.push_back({ tailTop, tailBot });

    return s;
}

// Adds a box (8 vertices, 12 edges) given its center and half-sizes
void addBox(Shape& s, double cx, double cy, double cz,
            double hx, double hy, double hz) {
    int b = (int)s.v.size();
    for (int i = 0; i < 8; ++i)
        s.v.push_back({ cx + ((i & 1) ? hx : -hx),
                        cy + ((i & 2) ? hy : -hy),
                        cz + ((i & 4) ? hz : -hz) });
    // connect every pair of corners that differ in exactly one axis
    for (int i = 0; i < 8; ++i)
        for (int bit = 1; bit <= 4; bit <<= 1)
            if (!(i & bit)) s.e.push_back({ b + i, b + (i | bit) });
}

void addLine(Shape& s, Vec3 a, Vec3 b) {
    int i = (int)s.v.size();
    s.v.push_back(a);
    s.v.push_back(b);
    s.e.push_back({ i, i + 1 });
}

// Shifts the shape so the middle of its bounding box sits at the origin
void recenter(Shape& s) {
    Vec3 lo = s.v[0], hi = s.v[0];
    for (const Vec3& p : s.v) {
        lo = { std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z) };
        hi = { std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z) };
    }
    Vec3 c = { (lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2 };
    for (Vec3& p : s.v) { p.x -= c.x; p.y -= c.y; p.z -= c.z; }
}
Shape makeCow() {
    Shape s;

    addBox(s, 0, 0, 0, 6, 3, 3);          // body
    addBox(s, 8.5, 1.5, 0, 2.5, 2, 2);    // head, raised and in front (+x)

    // four legs: lines hanging down from the underside of the body
    for (double x : { -4.5, 4.5 })
        for (double z : { -2.5, 2.5 })
            addLine(s, { x, -3, z }, { x, -9, z });

    // horns: angled up and outward from the top of the head
    addLine(s, { 8, 3.5, -1.5 }, { 8, 6, -2.5 });
    addLine(s, { 8, 3.5,  1.5 }, { 8, 6,  2.5 });

    // tail: hangs down from the rear
    addLine(s, { -6, 2, 0 }, { -7.5, -4, 0 });

    recenter(s);
    return s;
}

Shape makeCube() {
    Shape s;
    s.v = {{-5,-5,-5},{5,-5,-5},{5,5,-5},{-5,5,-5},
           {-5,-5, 5},{5,-5, 5},{5,5, 5},{-5,5, 5}};
    s.e = {{0,1},{1,2},{2,3},{3,0},
           {4,5},{5,6},{6,7},{7,4},
           {0,4},{1,5},{2,6},{3,7}};
    return s;
}

void drawAxes(const Camera& cam, uint32_t color) {
    const double e = 50;
    drawLine3D({-e, 0, 0}, {e, 0, 0}, cam, color, 0, 0);   // X
    drawLine3D({-e, 0, 0}, {e, 0, 0}, cam, color, 0, 1);   // thickness
    drawLine3D({0, -e, 0}, {0, e, 0}, cam, color, 0, 0);   // Y
    drawLine3D({0, -e, 0}, {0, e, 0}, cam, color, 1, 0);
    drawLine3D({0, 0, -e}, {0, 0, e}, cam, color, 0, 0);   // Z
    drawLine3D({0, 0, -e}, {0, 0, e}, cam, color, 1, 0);
}

int main() {
    Display* d = XOpenDisplay(nullptr);
    if (!d) return 1;

    int scr = DefaultScreen(d);
    Window win = XCreateSimpleWindow(d, RootWindow(d, scr), 0, 0, W, H, 0, 0, 0);
    XSelectInput(d, win, KeyPressMask | ExposureMask);
    XMapWindow(d, win);

    GC gc = XCreateGC(d, win, 0, nullptr);
    XImage* img = XCreateImage(d, DefaultVisual(d, scr), DefaultDepth(d, scr),
                               ZPixmap, 0, (char*)fb.data(), W, H, 32, 0);

    Camera cam;
    std::vector<Shape> shapes = { makePyramid(), makePrism(), makeCube(), makeFish(), makeCow() };
    size_t current = 0;
    bool showAxes = true, running = true;
    double spinX = 0, spinY = 0;
    using clock = std::chrono::steady_clock;

    while (running) {
        auto start = clock::now();

        // ---- input ----
        while (XPending(d)) {
            XEvent ev;
            XNextEvent(d, &ev);
            if (ev.type == KeyPress) {
                KeySym k = XLookupKeysym(&ev.xkey, 0);
                if (k == XK_Left)  cam.yaw += 0.05;   // look right
                if (k == XK_Right) cam.yaw -= 0.05;   // look left
                if (k == XK_s)     current = (current + 1) % shapes.size();
                if (k == XK_x)     showAxes = !showAxes;
                if (k == XK_Escape || k == XK_q) running = false;
            }
        }

        // ---- update ----
        spinX += 0.015;
		spinY += 0.02;

        // ---- render ----
        clearFB(BG_COLOR);
        if (showAxes) drawAxes(cam, AXIS_COLOR);

        const Shape& s = shapes[current];
	for (size_t i = 0; i < s.e.size(); ++i) {
		const Edge& e = s.e[i];
		Vec3 a = rotateY(rotateX(s.v[e.a], spinX), spinY);
		Vec3 b = rotateY(rotateX(s.v[e.b], spinX), spinY);
		drawLine3D(a, b, cam, EDGE_COLORS[i % 3]);
	}




        XPutImage(d, win, gc, img, 0, 0, 0, 0, W, H);
        XFlush(d);

        // ---- frame pacing (~60 fps) ----
        std::this_thread::sleep_until(start + std::chrono::milliseconds(16));
    }

    img->data = nullptr;   // fb is owned by the vector, not by XImage
    XDestroyImage(img);
    XFreeGC(d, gc);
    XCloseDisplay(d);
    return 0;
}

