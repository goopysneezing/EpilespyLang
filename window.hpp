#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <GL/gl.h>
#include <thread>
#include <mutex>
#include <string>
#include <unordered_map>
#include <chrono>

// Helper to convert hex string or named color to COLORREF
inline COLORREF parseColor(const std::string& colorName) {
    static const std::unordered_map<std::string, COLORREF> colorMap = {
        {"black", RGB(0, 0, 0)},
        {"white", RGB(255, 255, 255)},
        {"red", RGB(255, 0, 0)},
        {"green", RGB(0, 255, 0)},
        {"blue", RGB(0, 0, 255)},
        {"yellow", RGB(255, 255, 0)},
        {"purple", RGB(128, 0, 128)},
        {"orange", RGB(255, 165, 0)},
        {"cyan", RGB(0, 255, 255)},
        {"magenta", RGB(255, 0, 255)},
        {"gray", RGB(128, 128, 128)},
        {"lightgray", RGB(211, 211, 211)},
        {"darkgray", RGB(169, 169, 169)},
        {"brown", RGB(139, 69, 19)},
        {"saddlebrown", RGB(139, 69, 19)},
        {"forestgreen", RGB(34, 139, 34)}
    };

    auto it = colorMap.find(colorName);
    if (it != colorMap.end()) return it->second;

    // Check if it is a hex color e.g. #ff0000
    if (colorName.size() == 7 && colorName[0] == '#') {
        try {
            int r = std::stoi(colorName.substr(1, 2), nullptr, 16);
            int g = std::stoi(colorName.substr(3, 2), nullptr, 16);
            int b = std::stoi(colorName.substr(5, 2), nullptr, 16);
            return RGB(r, g, b);
        } catch (...) {}
    }

    return RGB(255, 255, 255); // default to white
}

#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>

struct Vertex3D {
    double x, y, z;
};

struct Face3D {
    int indices[4];
    int numVertices;
    std::string color;
    double depth;
};

struct Object3D {
    std::string type; // "cube" or "grid"
    std::vector<Vertex3D> vertices;
    std::vector<Face3D> faces;
    double px, py, pz;
    double rx, ry, rz;
    double scaleX, scaleY, scaleZ;
};

struct Object2D {
    std::string type; // "rect" or "circle"
    double x, y, w, h, r;
    std::string color;
};

class WindowInstance {
public:
    HWND hwnd = nullptr;
    std::thread winThread;
    std::recursive_mutex mtx;
    bool isWindowOpen = false;
    int width = 0;
    int height = 0;
    std::string title;
    
    // Offscreen rendering resources
    HDC hdcMem = nullptr;
    HBITMAP hbmMem = nullptr;
    HBITMAP hbmOld = nullptr;

    HDC hdcFront = nullptr;
    HBITMAP hbmFront = nullptr;
    HBITMAP hbmOldFront = nullptr;

    // OpenGL resources
    HDC hdcGL = nullptr;
    HGLRC hglrcGL = nullptr;

    // Input state
    double mouseX = 0;
    double mouseY = 0;
    bool mouseLeft = false;
    bool keys[256] = {false};

    // 3D/2D properties
    bool is3D = false;
    bool is2D = false;
    double camX = 0, camY = 0, camZ = 0;
    double camPitch = 0, camYaw = 0;
    double cam2DX = 0, cam2DY = 0, cam2DZoom = 1.0;
    std::vector<Object3D> objects3D;
    std::vector<Object2D> objects2D;

    // Player and physics properties
    double playerX = 0, playerY = 0, playerZ = 0;
    double eyeHeight = 1.8;
    double followDistance = 5.0;
    double staticCameraX = 0, staticCameraY = 5, staticCameraZ = -10;
    bool gravityEnabled = false;
    double gravity = -9.81; // m/s^2
    double velocityY = 0.0;
    bool isGrounded = false;
    std::string cameraMode = "first"; // "first", "second", "third"

    void setGravity(double g) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        gravity = g;
    }

    void enableGravity(bool enable) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        gravityEnabled = enable;
    }

    void setCameraMode(const std::string& mode) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (mode == "first" || mode == "second" || mode == "third") {
            cameraMode = mode;
        }
    }

    void setSecondPersonCamera(double x, double y, double z) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        staticCameraX = x;
        staticCameraY = y;
        staticCameraZ = z;
    }

    void jump(double force) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (isGrounded || !gravityEnabled) {
            velocityY = force;
            isGrounded = false;
        }
    }

    double getPlayerX() { std::lock_guard<std::recursive_mutex> lock(mtx); return playerX; }
    double getPlayerY() { std::lock_guard<std::recursive_mutex> lock(mtx); return playerY; }
    double getPlayerZ() { std::lock_guard<std::recursive_mutex> lock(mtx); return playerZ; }
    bool getIsGrounded() { std::lock_guard<std::recursive_mutex> lock(mtx); return isGrounded; }

    void updatePhysicsInternal(double dt) {
        if (gravityEnabled) {
            velocityY += gravity * dt;
            playerY += velocityY * dt;
            if (playerY <= 0.0) {
                playerY = 0.0;
                velocityY = 0.0;
                isGrounded = true;
            } else {
                isGrounded = false;
            }
        }
    }

    void updateCameraPosInternal() {
        if (cameraMode == "first") {
            camX = playerX;
            camY = playerY + eyeHeight;
            camZ = playerZ;
        } 
        else if (cameraMode == "second") {
            camX = staticCameraX;
            camY = staticCameraY;
            camZ = staticCameraZ;
            double dx = playerX - camX;
            double dy = (playerY + eyeHeight/2.0) - camY;
            double dz = playerZ - camZ;
            double dist = std::hypot(dx, dz);
            camYaw = std::atan2(dx, dz);
            camPitch = std::atan2(dy, dist);
        }
        else if (cameraMode == "third") {
            double dx = -std::sin(camYaw) * std::cos(camPitch) * followDistance;
            double dy = -std::sin(camPitch) * followDistance;
            double dz = -std::cos(camYaw) * std::cos(camPitch) * followDistance;
            camX = playerX + dx;
            camY = playerY + eyeHeight + dy;
            camZ = playerZ + dz;
        }
    }

    WindowInstance(const std::string& title, int w, int h)
        : title(title), width(w), height(h) {
        isWindowOpen = true;
        winThread = std::thread(&WindowInstance::run, this);

        // Wait until HWND is created
        while (hwnd == nullptr && isWindowOpen) {
            std::this_thread::yield();
        }
    }

    ~WindowInstance() {
        close();
        if (winThread.joinable()) {
            winThread.join();
        }
    }

    void close() {
        if (hwnd != nullptr) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        }
    }

    bool isOpen() {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        return isWindowOpen;
    }

    void present() {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (is3D) {
            if (hdcGL) SwapBuffers(hdcGL);
            return;
        }
        if (hdcMem != nullptr && hdcFront != nullptr) {
            BitBlt(hdcFront, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);
            invalidate();
        }
    }

    // Drawing methods
    void clear(const std::string& colorName) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (is3D) {
            if (hglrcGL && hdcGL) {
                wglMakeCurrent(hdcGL, hglrcGL);
                COLORREF color = parseColor(colorName);
                float r = GetRValue(color) / 255.0f;
                float g = GetGValue(color) / 255.0f;
                float b = GetBValue(color) / 255.0f;
                glClearColor(r, g, b, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            }
            return;
        }
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        HBRUSH brush = CreateSolidBrush(color);
        RECT r = {0, 0, width, height};
        FillRect(hdcMem, &r, brush);
        DeleteObject(brush);
        if (!is3D && !is2D) present();
    }

    void rect(int x, int y, int w, int h, const std::string& colorName) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (is3D) {
            if (hglrcGL && hdcGL) {
                wglMakeCurrent(hdcGL, hglrcGL);
                glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, width, height, 0, -1, 1);
                glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
                glDisable(GL_DEPTH_TEST);
                COLORREF color = parseColor(colorName);
                glColor3ub(GetRValue(color), GetGValue(color), GetBValue(color));
                glBegin(GL_QUADS);
                glVertex2i(x, y);
                glVertex2i(x + w, y);
                glVertex2i(x + w, y + h);
                glVertex2i(x, y + h);
                glEnd();
                glEnable(GL_DEPTH_TEST);
                glMatrixMode(GL_PROJECTION); glPopMatrix();
                glMatrixMode(GL_MODELVIEW); glPopMatrix();
            }
            return;
        }
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        HBRUSH brush = CreateSolidBrush(color);
        RECT r = {x, y, x + w, y + h};
        FillRect(hdcMem, &r, brush);
        DeleteObject(brush);
        if (!is3D && !is2D) present();
    }

    void rectEmpty(int x, int y, int w, int h, const std::string& colorName, int thickness) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (is3D) {
            if (hglrcGL && hdcGL) {
                wglMakeCurrent(hdcGL, hglrcGL);
                glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, width, height, 0, -1, 1);
                glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
                glDisable(GL_DEPTH_TEST);
                COLORREF color = parseColor(colorName);
                glColor3ub(GetRValue(color), GetGValue(color), GetBValue(color));
                glLineWidth(static_cast<GLfloat>(thickness));
                glBegin(GL_LINE_LOOP);
                glVertex2i(x, y);
                glVertex2i(x + w, y);
                glVertex2i(x + w, y + h);
                glVertex2i(x, y + h);
                glEnd();
                glLineWidth(1.0f);
                glEnable(GL_DEPTH_TEST);
                glMatrixMode(GL_PROJECTION); glPopMatrix();
                glMatrixMode(GL_MODELVIEW); glPopMatrix();
            }
            return;
        }
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        HPEN pen = CreatePen(PS_SOLID, thickness, color);
        HPEN oldPen = (HPEN)SelectObject(hdcMem, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdcMem, GetStockObject(NULL_BRUSH));

        Rectangle(hdcMem, x, y, x + w, y + h);

        SelectObject(hdcMem, oldPen);
        SelectObject(hdcMem, oldBrush);
        DeleteObject(pen);
        if (!is3D && !is2D) present();
    }

    void circle(int x, int y, int r, const std::string& colorName) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (is3D) {
            if (hglrcGL && hdcGL) {
                wglMakeCurrent(hdcGL, hglrcGL);
                glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, width, height, 0, -1, 1);
                glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
                glDisable(GL_DEPTH_TEST);
                COLORREF color = parseColor(colorName);
                glColor3ub(GetRValue(color), GetGValue(color), GetBValue(color));
                glBegin(GL_TRIANGLE_FAN);
                glVertex2i(x, y);
                for (int i = 0; i <= 36; ++i) {
                    double angle = i * 2 * 3.1415926535 / 36.0;
                    glVertex2d(x + r * std::cos(angle), y + r * std::sin(angle));
                }
                glEnd();
                glEnable(GL_DEPTH_TEST);
                glMatrixMode(GL_PROJECTION); glPopMatrix();
                glMatrixMode(GL_MODELVIEW); glPopMatrix();
            }
            return;
        }
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        HBRUSH brush = CreateSolidBrush(color);
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HPEN oldPen = (HPEN)SelectObject(hdcMem, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdcMem, brush);

        Ellipse(hdcMem, x - r, y - r, x + r, y + r);

        SelectObject(hdcMem, oldPen);
        SelectObject(hdcMem, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
        if (!is3D && !is2D) present();
    }

    void circleEmpty(int x, int y, int r, const std::string& colorName, int thickness) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (is3D) {
            if (hglrcGL && hdcGL) {
                wglMakeCurrent(hdcGL, hglrcGL);
                glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, width, height, 0, -1, 1);
                glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
                glDisable(GL_DEPTH_TEST);
                COLORREF color = parseColor(colorName);
                glColor3ub(GetRValue(color), GetGValue(color), GetBValue(color));
                glLineWidth(static_cast<GLfloat>(thickness));
                glBegin(GL_LINE_LOOP);
                for (int i = 0; i < 36; ++i) {
                    double angle = i * 2 * 3.1415926535 / 36.0;
                    glVertex2d(x + r * std::cos(angle), y + r * std::sin(angle));
                }
                glEnd();
                glLineWidth(1.0f);
                glEnable(GL_DEPTH_TEST);
                glMatrixMode(GL_PROJECTION); glPopMatrix();
                glMatrixMode(GL_MODELVIEW); glPopMatrix();
            }
            return;
        }
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        HPEN pen = CreatePen(PS_SOLID, thickness, color);
        HPEN oldPen = (HPEN)SelectObject(hdcMem, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdcMem, GetStockObject(NULL_BRUSH));

        Ellipse(hdcMem, x - r, y - r, x + r, y + r);

        SelectObject(hdcMem, oldPen);
        SelectObject(hdcMem, oldBrush);
        DeleteObject(pen);
        if (!is3D && !is2D) present();
    }

    void ellipse(int x, int y, int rx, int ry, const std::string& colorName) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (is3D) {
            if (hglrcGL && hdcGL) {
                wglMakeCurrent(hdcGL, hglrcGL);
                glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, width, height, 0, -1, 1);
                glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
                glDisable(GL_DEPTH_TEST);
                COLORREF color = parseColor(colorName);
                glColor3ub(GetRValue(color), GetGValue(color), GetBValue(color));
                glBegin(GL_TRIANGLE_FAN);
                glVertex2i(x, y);
                for (int i = 0; i <= 36; ++i) {
                    double angle = i * 2 * 3.1415926535 / 36.0;
                    glVertex2d(x + rx * std::cos(angle), y + ry * std::sin(angle));
                }
                glEnd();
                glEnable(GL_DEPTH_TEST);
                glMatrixMode(GL_PROJECTION); glPopMatrix();
                glMatrixMode(GL_MODELVIEW); glPopMatrix();
            }
            return;
        }
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        HBRUSH brush = CreateSolidBrush(color);
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HPEN oldPen = (HPEN)SelectObject(hdcMem, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdcMem, brush);

        Ellipse(hdcMem, x - rx, y - ry, x + rx, y + ry);

        SelectObject(hdcMem, oldPen);
        SelectObject(hdcMem, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
        if (!is3D && !is2D) present();
    }

    void line(int x1, int y1, int x2, int y2, const std::string& colorName, int thickness) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (is3D) {
            if (hglrcGL && hdcGL) {
                wglMakeCurrent(hdcGL, hglrcGL);
                glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, width, height, 0, -1, 1);
                glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
                glDisable(GL_DEPTH_TEST);
                COLORREF color = parseColor(colorName);
                glColor3ub(GetRValue(color), GetGValue(color), GetBValue(color));
                glLineWidth(static_cast<GLfloat>(thickness));
                glBegin(GL_LINES);
                glVertex2i(x1, y1);
                glVertex2i(x2, y2);
                glEnd();
                glLineWidth(1.0f);
                glEnable(GL_DEPTH_TEST);
                glMatrixMode(GL_PROJECTION); glPopMatrix();
                glMatrixMode(GL_MODELVIEW); glPopMatrix();
            }
            return;
        }
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        HPEN pen = CreatePen(PS_SOLID, thickness, color);
        HPEN oldPen = (HPEN)SelectObject(hdcMem, pen);

        MoveToEx(hdcMem, x1, y1, NULL);
        LineTo(hdcMem, x2, y2);

        SelectObject(hdcMem, oldPen);
        DeleteObject(pen);
        if (!is3D && !is2D) present();
    }

    void text(int x, int y, const std::string& content, const std::string& colorName, int size) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (is3D) {
            if (hglrcGL && hdcGL) {
                wglMakeCurrent(hdcGL, hglrcGL);
                glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, width, height, 0, -1, 1);
                glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
                glDisable(GL_DEPTH_TEST);
                COLORREF color = parseColor(colorName);
                glColor3ub(GetRValue(color), GetGValue(color), GetBValue(color));
                glRasterPos2i(x, y + size - 2);

                HFONT font = CreateFontA(
                    size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial"
                );
                HFONT oldFont = (HFONT)SelectObject(hdcGL, font);

                GLuint listBase = glGenLists(96);
                wglUseFontBitmapsA(hdcGL, 32, 96, listBase);
                glListBase(listBase - 32);
                glCallLists(static_cast<GLsizei>(content.length()), GL_UNSIGNED_BYTE, content.c_str());

                glDeleteLists(listBase, 96);
                SelectObject(hdcGL, oldFont);
                DeleteObject(font);

                glEnable(GL_DEPTH_TEST);
                glMatrixMode(GL_PROJECTION); glPopMatrix();
                glMatrixMode(GL_MODELVIEW); glPopMatrix();
            }
            return;
        }
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        
        SetTextColor(hdcMem, color);
        SetBkMode(hdcMem, TRANSPARENT);

        HFONT font = CreateFontA(
            size, 0, 0, 0, FW_NORMAL, false, false, false,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial"
        );
        HFONT oldFont = (HFONT)SelectObject(hdcMem, font);

        TextOutA(hdcMem, x, y, content.c_str(), static_cast<int>(content.length()));

        SelectObject(hdcMem, oldFont);
        DeleteObject(font);
        if (!is3D && !is2D) present();
    }

    void pixel(int x, int y, const std::string& colorName) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (is3D) {
            if (hglrcGL && hdcGL) {
                wglMakeCurrent(hdcGL, hglrcGL);
                glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, width, height, 0, -1, 1);
                glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
                glDisable(GL_DEPTH_TEST);
                COLORREF color = parseColor(colorName);
                glColor3ub(GetRValue(color), GetGValue(color), GetBValue(color));
                glBegin(GL_POINTS);
                glVertex2i(x, y);
                glEnd();
                glEnable(GL_DEPTH_TEST);
                glMatrixMode(GL_PROJECTION); glPopMatrix();
                glMatrixMode(GL_MODELVIEW); glPopMatrix();
            }
            return;
        }
        if (hdcMem == nullptr) return;
        COLORREF color = parseColor(colorName);
        SetPixel(hdcMem, x, y, color);
        if (!is3D && !is2D) present();
    }

    // 3D Engine Methods
    void addCube(double x, double y, double z, double size, const std::string& colorName) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        double s = size / 2.0;
        Object3D obj;
        obj.type = "cube";
        obj.px = x; obj.py = y; obj.pz = z;
        obj.rx = 0; obj.ry = 0; obj.rz = 0;
        obj.scaleX = 1; obj.scaleY = 1; obj.scaleZ = 1;

        // 8 vertices
        obj.vertices = {
            {-s, -s, -s}, {s, -s, -s}, {s, s, -s}, {-s, s, -s},
            {-s, -s, s},  {s, -s, s},  {s, s, s},  {-s, s, s}
        };

        // 6 faces
        obj.faces = {
            {{0, 3, 2, 1}, 4, colorName, 0.0}, // Front
            {{5, 6, 7, 4}, 4, colorName, 0.0}, // Back
            {{4, 7, 3, 0}, 4, colorName, 0.0}, // Left
            {{1, 2, 6, 5}, 4, colorName, 0.0}, // Right
            {{3, 7, 6, 2}, 4, colorName, 0.0}, // Top
            {{0, 1, 5, 4}, 4, colorName, 0.0}  // Bottom
        };

        objects3D.push_back(obj);
    }

    void addBox(double x, double y, double z, double sx, double sy, double sz, const std::string& colorName) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        Object3D obj;
        obj.type = "cube";
        obj.px = x; obj.py = y; obj.pz = z;
        obj.rx = 0; obj.ry = 0; obj.rz = 0;
        obj.scaleX = sx; obj.scaleY = sy; obj.scaleZ = sz;

        // 8 vertices of unit cube centered at origin
        obj.vertices = {
            {-0.5, -0.5, -0.5}, {0.5, -0.5, -0.5}, {0.5, 0.5, -0.5}, {-0.5, 0.5, -0.5},
            {-0.5, -0.5, 0.5},  {0.5, -0.5, 0.5},  {0.5, 0.5, 0.5},  {-0.5, 0.5, 0.5}
        };

        // 6 faces
        obj.faces = {
            {{0, 3, 2, 1}, 4, colorName, 0.0}, // Front
            {{5, 6, 7, 4}, 4, colorName, 0.0}, // Back
            {{4, 7, 3, 0}, 4, colorName, 0.0}, // Left
            {{1, 2, 6, 5}, 4, colorName, 0.0}, // Right
            {{3, 7, 6, 2}, 4, colorName, 0.0}, // Top
            {{0, 1, 5, 4}, 4, colorName, 0.0}  // Bottom
        };

        objects3D.push_back(obj);
    }

    void addTree(double x, double y, double z, double trunkHeight, double foliageSize) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        Object3D obj;
        obj.type = "tree";
        obj.px = x; obj.py = y; obj.pz = z;
        obj.rx = 0; obj.ry = 0; obj.rz = 0;
        obj.scaleX = 1.0; obj.scaleY = 1.0; obj.scaleZ = 1.0;

        double tw = 0.3;
        double th = trunkHeight;
        double fw = foliageSize;
        double fh = foliageSize * 1.5;

        // 8 trunk vertices + 8 foliage vertices
        obj.vertices = {
            {-tw/2.0, 0.0, -tw/2.0}, {tw/2.0, 0.0, -tw/2.0}, {tw/2.0, th, -tw/2.0}, {-tw/2.0, th, -tw/2.0},
            {-tw/2.0, 0.0, tw/2.0},  {tw/2.0, 0.0, tw/2.0},  {tw/2.0, th, tw/2.0},  {-tw/2.0, th, tw/2.0},

            {-fw/2.0, th, -fw/2.0}, {fw/2.0, th, -fw/2.0}, {fw/2.0, th + fh, -fw/2.0}, {-fw/2.0, th + fh, -fw/2.0},
            {-fw/2.0, th, fw/2.0},  {fw/2.0, th, fw/2.0},  {fw/2.0, th + fh, fw/2.0},  {-fw/2.0, th + fh, fw/2.0}
        };

        obj.faces = {
            // Trunk (SaddleBrown)
            {{0, 3, 2, 1}, 4, "saddlebrown", 0.0},
            {{5, 6, 7, 4}, 4, "saddlebrown", 0.0},
            {{4, 7, 3, 0}, 4, "saddlebrown", 0.0},
            {{1, 2, 6, 5}, 4, "saddlebrown", 0.0},
            {{3, 7, 6, 2}, 4, "saddlebrown", 0.0},
            {{0, 1, 5, 4}, 4, "saddlebrown", 0.0},

            // Foliage (ForestGreen)
            {{8, 11, 10, 9}, 4, "forestgreen", 0.0},
            {{13, 14, 15, 12}, 4, "forestgreen", 0.0},
            {{12, 15, 11, 8}, 4, "forestgreen", 0.0},
            {{9, 10, 14, 13}, 4, "forestgreen", 0.0},
            {{11, 15, 14, 10}, 4, "forestgreen", 0.0},
            {{8, 9, 13, 12}, 4, "forestgreen", 0.0}
        };

        objects3D.push_back(obj);
    }

    void addModel(double x, double y, double z, double sx, double sy, double sz, double rx, double ry, double rz, const std::string& objPath, const std::string& colorName) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        
        Object3D obj;
        obj.type = "model";
        obj.px = x; obj.py = y; obj.pz = z;
        obj.rx = rx; obj.ry = ry; obj.rz = rz;
        obj.scaleX = sx; obj.scaleY = sy; obj.scaleZ = sz;

        std::ifstream file(objPath);
        if (!file.is_open()) {
            // Fallback placeholder box
            addBox(x, y, z, sx, sy, sz, colorName);
            return;
        }

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            std::string prefix;
            ss >> prefix;
            if (prefix == "v") {
                double vx, vy, vz;
                if (ss >> vx >> vy >> vz) {
                    obj.vertices.push_back({vx, vy, vz});
                }
            } else if (prefix == "f") {
                std::string vStr;
                Face3D face;
                face.numVertices = 0;
                face.color = colorName;
                face.depth = 0.0;
                while (ss >> vStr && face.numVertices < 4) {
                    size_t slashPos = vStr.find('/');
                    std::string idxStr = (slashPos == std::string::npos) ? vStr : vStr.substr(0, slashPos);
                    if (idxStr.empty()) continue;
                    int idx = std::stoi(idxStr);
                    if (idx > 0) {
                        face.indices[face.numVertices++] = idx - 1;
                    } else if (idx < 0) {
                        face.indices[face.numVertices++] = static_cast<int>(obj.vertices.size()) + idx;
                    }
                }
                if (face.numVertices >= 3) {
                    obj.faces.push_back(face);
                }
            }
        }
        objects3D.push_back(obj);
    }


    void addGrid(double x, double z, double size, double spacing, const std::string& colorName) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        double halfSize = size / 2.0;
        Object3D obj;
        obj.type = "grid";
        obj.px = x; obj.py = 0; obj.pz = z;
        obj.rx = 0; obj.ry = 0; obj.rz = 0;
        obj.scaleX = 1; obj.scaleY = 1; obj.scaleZ = 1;

        int idx = 0;
        for (double i = -halfSize; i <= halfSize; i += spacing) {
            // Line along Z axis
            obj.vertices.push_back({i, 0, -halfSize});
            obj.vertices.push_back({i, 0, halfSize});
            Face3D f1;
            f1.indices[0] = idx++;
            f1.indices[1] = idx++;
            f1.numVertices = 2;
            f1.color = colorName;
            f1.depth = 0.0;
            obj.faces.push_back(f1);

            // Line along X axis
            obj.vertices.push_back({-halfSize, 0, i});
            obj.vertices.push_back({halfSize, 0, i});
            Face3D f2;
            f2.indices[0] = idx++;
            f2.indices[1] = idx++;
            f2.numVertices = 2;
            f2.color = colorName;
            f2.depth = 0.0;
            obj.faces.push_back(f2);
        }

        objects3D.push_back(obj);
    }

    void setCamera(double x, double y, double z, double pitch, double yaw) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        playerX = x;
        playerY = y;
        playerZ = z;
        camPitch = pitch;
        camYaw = yaw;
    }

    void moveCamera(double forward, double right, double up) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        double fx = -std::sin(camYaw);
        double fz = std::cos(camYaw);

        double rx = std::cos(camYaw);
        double rz = std::sin(camYaw);

        playerX += forward * fx + right * rx;
        playerZ += forward * fz + right * rz;
        playerY += up;
    }

    void rotateCamera(double dpitch, double dyaw) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        camPitch += dpitch;
        camYaw += dyaw;

        if (camPitch > 1.5) camPitch = 1.5;
        if (camPitch < -1.5) camPitch = -1.5;
    }

    void clear3D() {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        objects3D.clear();
    }

    void render3D() {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        
        // Update physics and camera positioning
        updatePhysicsInternal(0.016);
        updateCameraPosInternal();

        if (is3D) {
            if (!hglrcGL || !hdcGL) return;

            wglMakeCurrent(hdcGL, hglrcGL);

            // Set viewport
            glViewport(0, 0, width, height);

            // Set projection matrix
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            double aspect = (double)width / (double)height;
            double fovY = 45.0; // 45 degrees
            double zNear = 0.1;
            double zFar = 1000.0;
            double f = 1.0 / std::tan(fovY * 3.1415926535 / 360.0);
            double m[16] = {
                f / aspect, 0, 0, 0,
                0, f, 0, 0,
                0, 0, (zFar + zNear) / (zNear - zFar), -1,
                0, 0, (2.0 * zFar * zNear) / (zNear - zFar), 0
            };
            glLoadMatrixd(m);

            // Set modelview matrix
            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glScaled(1.0, 1.0, -1.0); // Make +Z go forward

            glRotated(-camPitch * 180.0 / 3.1415926535, 1, 0, 0);
            glRotated(camYaw * 180.0 / 3.1415926535, 0, 1, 0);
            glTranslated(-camX, -camY, -camZ);

            // Enable depth test
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);

            // Draw objects
            for (const auto& obj : objects3D) {
                glPushMatrix();
                glTranslated(obj.px, obj.py, obj.pz);
                glRotated(obj.rx * 180.0 / 3.1415926535, 1, 0, 0);
                glRotated(obj.ry * 180.0 / 3.1415926535, 0, 1, 0);
                glRotated(obj.rz * 180.0 / 3.1415926535, 0, 0, 1);
                glScaled(obj.scaleX, obj.scaleY, obj.scaleZ);

                if (obj.type == "cube" || obj.type == "tree" || obj.type == "model") {
                    for (const auto& face : obj.faces) {
                        COLORREF color = parseColor(face.color);
                        float r = GetRValue(color) / 255.0f;
                        float g = GetGValue(color) / 255.0f;
                        float b = GetBValue(color) / 255.0f;

                        if (face.numVertices == 3) glBegin(GL_TRIANGLES);
                        else if (face.numVertices == 4) glBegin(GL_QUADS);
                        else if (face.numVertices == 2) glBegin(GL_LINES);
                        else glBegin(GL_POLYGON);

                        glColor3f(r, g, b);
                        for (int i = 0; i < face.numVertices; ++i) {
                            const auto& v = obj.vertices[face.indices[i]];
                            glVertex3d(v.x, v.y, v.z);
                        }
                        glEnd();
                    }
                } else if (obj.type == "grid") {
                    for (const auto& face : obj.faces) {
                        COLORREF color = parseColor(face.color);
                        float r = GetRValue(color) / 255.0f;
                        float g = GetGValue(color) / 255.0f;
                        float b = GetBValue(color) / 255.0f;

                        glBegin(GL_LINES);
                        glColor3f(r, g, b);
                        const auto& v1 = obj.vertices[face.indices[0]];
                        const auto& v2 = obj.vertices[face.indices[1]];
                        glVertex3d(v1.x, v1.y, v1.z);
                        glVertex3d(v2.x, v2.y, v2.z);
                        glEnd();
                    }
                }

                glPopMatrix();
            }

            // Draw player box in OpenGL if not in first-person mode
            if (cameraMode != "first") {
                glPushMatrix();
                glTranslated(playerX, playerY + 0.9, playerZ);
                glRotated(camYaw * 180.0 / 3.1415926535, 0, 1, 0); // Rotate player with camera view
                
                double s = 0.3; // radius/width
                double h = 0.9; // half height
                
                glBegin(GL_QUADS);
                // Front - distinct orange face
                glColor3f(1.0f, 0.4f, 0.0f);
                glVertex3d(-s, -h, -s); glVertex3d(s, -h, -s); glVertex3d(s, h, -s); glVertex3d(-s, h, -s);
                // Back - cyan
                glColor3f(0.0f, 0.7f, 1.0f);
                glVertex3d(-s, -h, s); glVertex3d(-s, h, s); glVertex3d(s, h, s); glVertex3d(s, -h, s);
                // Left - cyan
                glColor3f(0.0f, 0.7f, 1.0f);
                glVertex3d(-s, -h, s); glVertex3d(-s, -h, -s); glVertex3d(-s, h, -s); glVertex3d(-s, h, s);
                // Right - cyan
                glColor3f(0.0f, 0.7f, 1.0f);
                glVertex3d(s, -h, -s); glVertex3d(s, -h, s); glVertex3d(s, h, s); glVertex3d(s, h, -s);
                // Top - cyan
                glColor3f(0.0f, 0.7f, 1.0f);
                glVertex3d(-s, h, -s); glVertex3d(s, h, -s); glVertex3d(s, h, s); glVertex3d(-s, h, s);
                // Bottom - cyan
                glColor3f(0.0f, 0.7f, 1.0f);
                glVertex3d(-s, -h, -s); glVertex3d(-s, -h, s); glVertex3d(s, -h, s); glVertex3d(s, -h, -s);
                glEnd();
                glPopMatrix();
            }

            present();
            return;
        }
        if (hdcMem == nullptr) return;

        struct RenderTask {
            std::vector<POINT> screenPoints;
            int numVertices;
            COLORREF colorRef;
            double depth;
        };
        std::vector<RenderTask> renderTasks;

        double fov = 400.0;
        double halfW = width / 2.0;
        double halfH = height / 2.0;

        double cosYaw = std::cos(camYaw);
        double sinYaw = std::sin(camYaw);
        double cosPitch = std::cos(camPitch);
        double sinPitch = std::sin(camPitch);

        // Copy objects and inject player object if in 2nd or 3rd person camera mode
        std::vector<Object3D> renderObjects = objects3D;
        if (cameraMode != "first") {
            Object3D pObj;
            pObj.type = "cube";
            pObj.px = playerX;
            pObj.py = playerY + 0.9;
            pObj.pz = playerZ;
            pObj.rx = 0; pObj.ry = camYaw; pObj.rz = 0; // Rotate player by camYaw
            pObj.scaleX = 1; pObj.scaleY = 1; pObj.scaleZ = 1;

            double s = 0.3;
            double h = 0.9;
            pObj.vertices = {
                {-s, -h, -s}, {s, -h, -s}, {s, h, -s}, {-s, h, -s},
                {-s, -h, s},  {s, -h, s},  {s, h, s},  {-s, h, s}
            };
            pObj.faces = {
                {{0, 3, 2, 1}, 4, "orange", 0.0}, // Front face is orange
                {{5, 6, 7, 4}, 4, "cyan", 0.0},
                {{4, 7, 3, 0}, 4, "cyan", 0.0},
                {{1, 2, 6, 5}, 4, "cyan", 0.0},
                {{3, 7, 6, 2}, 4, "cyan", 0.0},
                {{0, 1, 5, 4}, 4, "cyan", 0.0}
            };
            renderObjects.push_back(pObj);
        }

        for (const auto& obj : renderObjects) {
            double cosRX = std::cos(obj.rx), sinRX = std::sin(obj.rx);
            double cosRY = std::cos(obj.ry), sinRY = std::sin(obj.ry);
            double cosRZ = std::cos(obj.rz), sinRZ = std::sin(obj.rz);

            std::vector<Vertex3D> camVertices;
            camVertices.reserve(obj.vertices.size());

            for (const auto& v : obj.vertices) {
                double sx = v.x * obj.scaleX;
                double sy = v.y * obj.scaleY;
                double sz = v.z * obj.scaleZ;

                double x1 = sx * cosRZ - sy * sinRZ;
                double y1 = sx * sinRZ + sy * cosRZ;
                double z1 = sz;

                double x2 = x1 * cosRY + z1 * sinRY;
                double y2 = y1;
                double z2 = -x1 * sinRY + z1 * cosRY;

                double x3 = x2;
                double y3 = y2 * cosRX - z2 * sinRX;
                double z3 = y2 * sinRX + z2 * cosRX;

                double wx = x3 + obj.px;
                double wy = y3 + obj.py;
                double wz = z3 + obj.pz;

                double cx_rel = wx - camX;
                double cy_rel = wy - camY;
                double cz_rel = wz - camZ;

                double cx_rot = cx_rel * cosYaw - cz_rel * sinYaw;
                double cz_rot = cx_rel * sinYaw + cz_rel * cosYaw;

                double cy_rot = cy_rel * cosPitch - cz_rot * sinPitch;
                double cz_final = cy_rel * sinPitch + cz_rot * cosPitch;

                camVertices.push_back({cx_rot, cy_rot, cz_final});
            }

            for (const auto& face : obj.faces) {
                std::vector<Vertex3D> faceVertices;
                faceVertices.reserve(face.numVertices);
                for (int i = 0; i < face.numVertices; ++i) {
                    faceVertices.push_back(camVertices[face.indices[i]]);
                }

                std::vector<Vertex3D> clipped;
                if (face.numVertices == 2) {
                    clipped = clipLineNearPlane(faceVertices[0], faceVertices[1]);
                } else {
                    clipped = clipPolygonNearPlane(faceVertices);
                }

                if (clipped.empty()) continue;

                std::vector<POINT> projPoints;
                projPoints.reserve(clipped.size());
                double sumDepth = 0.0;
                for (const auto& cv : clipped) {
                    sumDepth += cv.z;
                    int sx = static_cast<int>(halfW + (cv.x * fov / cv.z));
                    int sy = static_cast<int>(halfH - (cv.y * fov / cv.z));
                    projPoints.push_back({sx, sy});
                }

                RenderTask task;
                task.screenPoints = projPoints;
                task.numVertices = static_cast<int>(projPoints.size());
                task.colorRef = parseColor(face.color);
                task.depth = sumDepth / clipped.size();
                renderTasks.push_back(task);
            }
        }

        std::sort(renderTasks.begin(), renderTasks.end(), [](const RenderTask& a, const RenderTask& b) {
            return a.depth > b.depth;
        });

        for (const auto& task : renderTasks) {
            if (task.numVertices == 2) {
                HPEN pen = CreatePen(PS_SOLID, 1, task.colorRef);
                HPEN oldPen = (HPEN)SelectObject(hdcMem, pen);
                MoveToEx(hdcMem, task.screenPoints[0].x, task.screenPoints[0].y, NULL);
                LineTo(hdcMem, task.screenPoints[1].x, task.screenPoints[1].y);
                SelectObject(hdcMem, oldPen);
                DeleteObject(pen);
            } else if (task.numVertices > 2) {
                HBRUSH brush = CreateSolidBrush(task.colorRef);
                HPEN pen = CreatePen(PS_SOLID, 1, task.colorRef);
                HBRUSH oldBrush = (HBRUSH)SelectObject(hdcMem, brush);
                HPEN oldPen = (HPEN)SelectObject(hdcMem, pen);

                Polygon(hdcMem, task.screenPoints.data(), task.numVertices);

                SelectObject(hdcMem, oldBrush);
                SelectObject(hdcMem, oldPen);
                DeleteObject(brush);
                DeleteObject(pen);
            }
        }

        present();
    }

    // 2D Engine Methods
    void addRect2D(double x, double y, double w, double h, const std::string& colorName) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        Object2D obj;
        obj.type = "rect";
        obj.x = x; obj.y = y; obj.w = w; obj.h = h; obj.r = 0;
        obj.color = colorName;
        objects2D.push_back(obj);
    }

    void addCircle2D(double x, double y, double r, const std::string& colorName) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        Object2D obj;
        obj.type = "circle";
        obj.x = x; obj.y = y; obj.w = 0; obj.h = 0; obj.r = r;
        obj.color = colorName;
        objects2D.push_back(obj);
    }

    void setCamera2D(double cx, double cy, double zoom) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        cam2DX = cx;
        cam2DY = cy;
        cam2DZoom = zoom;
    }

    void moveCamera2D(double dx, double dy) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        cam2DX += dx;
        cam2DY += dy;
    }

    void clear2D() {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        objects2D.clear();
    }

    void render2D() {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (hdcMem == nullptr) return;

        double halfW = width / 2.0;
        double halfH = height / 2.0;

        for (const auto& obj : objects2D) {
            COLORREF color = parseColor(obj.color);
            HBRUSH brush = CreateSolidBrush(color);
            HPEN pen = CreatePen(PS_SOLID, 1, color);
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdcMem, brush);
            HPEN oldPen = (HPEN)SelectObject(hdcMem, pen);

            double tx = halfW + (obj.x - cam2DX) * cam2DZoom;
            double ty = halfH - (obj.y - cam2DY) * cam2DZoom;

            if (obj.type == "rect") {
                double tw = obj.w * cam2DZoom;
                double th = obj.h * cam2DZoom;
                RECT r = {
                    static_cast<LONG>(tx - tw / 2.0),
                    static_cast<LONG>(ty - th / 2.0),
                    static_cast<LONG>(tx + tw / 2.0),
                    static_cast<LONG>(ty + th / 2.0)
                };
                FillRect(hdcMem, &r, brush);
            } else if (obj.type == "circle") {
                double tr = obj.r * cam2DZoom;
                Ellipse(hdcMem,
                    static_cast<int>(tx - tr), static_cast<int>(ty - tr),
                    static_cast<int>(tx + tr), static_cast<int>(ty + tr)
                );
            }

            SelectObject(hdcMem, oldBrush);
            SelectObject(hdcMem, oldPen);
            DeleteObject(brush);
            DeleteObject(pen);
        }

        present();
    }

    // Input getters
    double getMouseX() {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        return mouseX;
    }

    double getMouseY() {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        return mouseY;
    }

    bool getMouseLeft() {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        return mouseLeft;
    }

    bool getKey(const std::string& keyName) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        int vk = mapKeyNameToVk(keyName);
        if (vk >= 0 && vk < 256) {
            return keys[vk];
        }
        return false;
    }

private:
    std::vector<Vertex3D> clipPolygonNearPlane(const std::vector<Vertex3D>& poly, double nearZ = 0.1) {
        std::vector<Vertex3D> result;
        if (poly.empty()) return result;

        for (size_t i = 0; i < poly.size(); ++i) {
            const Vertex3D& a = poly[i];
            const Vertex3D& b = poly[(i + 1) % poly.size()];

            bool aIn = (a.z >= nearZ);
            bool bIn = (b.z >= nearZ);

            if (aIn && bIn) {
                result.push_back(b);
            } else if (aIn && !bIn) {
                double t = (nearZ - a.z) / (b.z - a.z);
                double ix = a.x + t * (b.x - a.x);
                double iy = a.y + t * (b.y - a.y);
                result.push_back({ix, iy, nearZ});
            } else if (!aIn && bIn) {
                double t = (nearZ - a.z) / (b.z - a.z);
                double ix = a.x + t * (b.x - a.x);
                double iy = a.y + t * (b.y - a.y);
                result.push_back({ix, iy, nearZ});
                result.push_back(b);
            }
        }
        return result;
    }

    std::vector<Vertex3D> clipLineNearPlane(const Vertex3D& a, const Vertex3D& b, double nearZ = 0.1) {
        bool aIn = (a.z >= nearZ);
        bool bIn = (b.z >= nearZ);

        if (aIn && bIn) {
            return {a, b};
        } else if (aIn && !bIn) {
            double t = (nearZ - a.z) / (b.z - a.z);
            double ix = a.x + t * (b.x - a.x);
            double iy = a.y + t * (b.y - a.y);
            return {a, {ix, iy, nearZ}};
        } else if (!aIn && bIn) {
            double t = (nearZ - a.z) / (b.z - a.z);
            double ix = a.x + t * (b.x - a.x);
            double iy = a.y + t * (b.y - a.y);
            return {{ix, iy, nearZ}, b};
        }
        return {};
    }

    void invalidate() {
        if (hwnd != nullptr) {
            InvalidateRect(hwnd, NULL, false);
        }
    }

    int mapKeyNameToVk(const std::string& keyName) {
        if (keyName.empty()) return -1;
        
        if (keyName.length() == 1) {
            char c = keyName[0];
            if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
            if (c >= 'A' && c <= 'Z') return c;
            if (c >= '0' && c <= '9') return c;
            if (c == ' ') return VK_SPACE;
        }

        std::string lower = keyName;
        for (auto& c : lower) c = tolower(c);

        if (lower == "space") return VK_SPACE;
        if (lower == "left") return VK_LEFT;
        if (lower == "right") return VK_RIGHT;
        if (lower == "up") return VK_UP;
        if (lower == "down") return VK_DOWN;
        if (lower == "enter" || lower == "return") return VK_RETURN;
        if (lower == "escape" || lower == "esc") return VK_ESCAPE;
        if (lower == "shift") return VK_SHIFT;
        if (lower == "ctrl" || lower == "control") return VK_CONTROL;
        if (lower == "alt") return VK_MENU;
        if (lower == "backspace") return VK_BACK;
        if (lower == "tab") return VK_TAB;

        return -1;
    }

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        WindowInstance* self = nullptr;
        if (msg == WM_NCCREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            self = (WindowInstance*)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
            self->hwnd = hwnd;
        } else {
            self = (WindowInstance*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        }

        if (self != nullptr) {
            switch (msg) {
                case WM_PAINT: {
                    PAINTSTRUCT ps;
                    HDC hdc = BeginPaint(hwnd, &ps);
                    {
                        std::lock_guard<std::recursive_mutex> lock(self->mtx);
                        if (self->is3D) {
                            if (self->hdcGL) SwapBuffers(self->hdcGL);
                        } else if (self->hdcFront != nullptr) {
                            BitBlt(hdc, 0, 0, self->width, self->height, self->hdcFront, 0, 0, SRCCOPY);
                        }
                    }
                    EndPaint(hwnd, &ps);
                    return 0;
                }
                case WM_MOUSEMOVE: {
                    std::lock_guard<std::recursive_mutex> lock(self->mtx);
                    self->mouseX = GET_X_LPARAM(lParam);
                    self->mouseY = GET_Y_LPARAM(lParam);
                    return 0;
                }
                case WM_LBUTTONDOWN: {
                    std::lock_guard<std::recursive_mutex> lock(self->mtx);
                    self->mouseLeft = true;
                    return 0;
                }
                case WM_LBUTTONUP: {
                    std::lock_guard<std::recursive_mutex> lock(self->mtx);
                    self->mouseLeft = false;
                    return 0;
                }
                case WM_KEYDOWN: {
                    std::lock_guard<std::recursive_mutex> lock(self->mtx);
                    if (wParam >= 0 && wParam < 256) {
                        self->keys[wParam] = true;
                    }
                    return 0;
                }
                case WM_KEYUP: {
                    std::lock_guard<std::recursive_mutex> lock(self->mtx);
                    if (wParam >= 0 && wParam < 256) {
                        self->keys[wParam] = false;
                    }
                    return 0;
                }
                case WM_CLOSE: {
                    DestroyWindow(hwnd);
                    return 0;
                }
                case WM_DESTROY: {
                    std::lock_guard<std::recursive_mutex> lock(self->mtx);
                    self->isWindowOpen = false;
                    PostQuitMessage(0);
                    return 0;
                }
            }
        }

        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    void run() {
        HINSTANCE hInst = GetModuleHandle(NULL);
        
        WNDCLASSW wc = {};
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = &WindowInstance::wndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"EpilespyLangWindowClass";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);

        RegisterClassW(&wc);

        RECT r = {0, 0, width, height};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, false);
        int w = r.right - r.left;
        int h = r.bottom - r.top;

        std::wstring wtitle(title.begin(), title.end());

        HWND tempHwnd = CreateWindowExW(
            0,
            L"EpilespyLangWindowClass",
            wtitle.c_str(),
            WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT,
            w, h,
            NULL, NULL, hInst, this
        );

        if (!tempHwnd) {
            std::lock_guard<std::recursive_mutex> lock(mtx);
            isWindowOpen = false;
            return;
        }

        {
            std::lock_guard<std::recursive_mutex> lock(mtx);
            hdcGL = GetDC(tempHwnd);
            if (is3D) {
                PIXELFORMATDESCRIPTOR pfd = {};
                pfd.nSize = sizeof(pfd);
                pfd.nVersion = 1;
                pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
                pfd.iPixelType = PFD_TYPE_RGBA;
                pfd.cColorBits = 32;
                pfd.cDepthBits = 24;
                pfd.iLayerType = PFD_MAIN_PLANE;

                int format = ChoosePixelFormat(hdcGL, &pfd);
                if (format != 0) {
                    SetPixelFormat(hdcGL, format, &pfd);
                    hglrcGL = wglCreateContext(hdcGL);
                }
            }
        }

        if (!is3D) {
            HDC hdcScreen = GetDC(tempHwnd);
            std::lock_guard<std::recursive_mutex> lock(mtx);
            hdcMem = CreateCompatibleDC(hdcScreen);
            hbmMem = CreateCompatibleBitmap(hdcScreen, width, height);
            hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

            hdcFront = CreateCompatibleDC(hdcScreen);
            hbmFront = CreateCompatibleBitmap(hdcScreen, width, height);
            hbmOldFront = (HBITMAP)SelectObject(hdcFront, hbmFront);

            HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
            RECT clientRect = {0, 0, width, height};
            FillRect(hdcMem, &clientRect, brush);
            FillRect(hdcFront, &clientRect, brush);
            DeleteObject(brush);
            ReleaseDC(tempHwnd, hdcScreen);
        }

        ShowWindow(tempHwnd, SW_SHOW);
        UpdateWindow(tempHwnd);

        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        {
            std::lock_guard<std::recursive_mutex> lock(mtx);
            if (hdcMem != nullptr) {
                SelectObject(hdcMem, hbmOld);
                DeleteObject(hbmMem);
                DeleteDC(hdcMem);
                hdcMem = nullptr;
            }
            if (hdcFront != nullptr) {
                SelectObject(hdcFront, hbmOldFront);
                DeleteObject(hbmFront);
                DeleteDC(hdcFront);
                hdcFront = nullptr;
            }
            if (hglrcGL != nullptr) {
                wglMakeCurrent(NULL, NULL);
                wglDeleteContext(hglrcGL);
                hglrcGL = nullptr;
            }
            if (hdcGL != nullptr) {
                ReleaseDC(tempHwnd, hdcGL);
                hdcGL = nullptr;
            }
            hwnd = nullptr;
            isWindowOpen = false;
        }
    }
};

#define WM_USER_CREATE_CONTROL (WM_USER + 101)

struct FormControl {
    std::string name;
    std::string type; // "button", "label", "textbox", "checkbox"
    HWND hwndControl = nullptr;
    bool clicked = false;
    int id = 0;
};

struct CreateControlData {
    std::string name;
    std::string type;
    std::string text;
    int x, y, w, h;
    int id;
    HWND hwndParent;
    HWND hwndResult = nullptr;
};

class FormInstance {
public:
    HWND hwnd = nullptr;
    std::thread winThread;
    std::recursive_mutex mtx;
    bool isWindowOpen = false;
    int width = 0;
    int height = 0;
    std::string title;
    
    std::unordered_map<std::string, FormControl> controls;
    std::unordered_map<int, std::string> controlIdToName;
    int nextControlId = 1000;

    FormInstance(const std::string& title, int w, int h)
        : title(title), width(w), height(h) {
        isWindowOpen = true;
        winThread = std::thread(&FormInstance::run, this);

        // Wait until HWND is created
        while (hwnd == nullptr && isWindowOpen) {
            std::this_thread::yield();
        }
    }

    ~FormInstance() {
        close();
        if (winThread.joinable()) {
            winThread.join();
        }
    }

    void close() {
        if (hwnd != nullptr) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
        }
    }

    bool isOpen() {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        return isWindowOpen;
    }

    void addButton(const std::string& name, const std::string& text, int x, int y, int w, int h) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (controls.find(name) != controls.end()) return;
        int id = nextControlId++;
        HWND ctrlHwnd = createControlOnGuiThread(name, "button", text, x, y, w, h, id);
        
        FormControl ctrl = { name, "button", ctrlHwnd, false, id };
        controls[name] = ctrl;
        controlIdToName[id] = name;
    }

    void addLabel(const std::string& name, const std::string& text, int x, int y, int w, int h) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (controls.find(name) != controls.end()) return;
        int id = nextControlId++;
        HWND ctrlHwnd = createControlOnGuiThread(name, "label", text, x, y, w, h, id);
        
        FormControl ctrl = { name, "label", ctrlHwnd, false, id };
        controls[name] = ctrl;
        controlIdToName[id] = name;
    }

    void addTextBox(const std::string& name, const std::string& text, int x, int y, int w, int h) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (controls.find(name) != controls.end()) return;
        int id = nextControlId++;
        HWND ctrlHwnd = createControlOnGuiThread(name, "textbox", text, x, y, w, h, id);
        
        FormControl ctrl = { name, "textbox", ctrlHwnd, false, id };
        controls[name] = ctrl;
        controlIdToName[id] = name;
    }

    void addCheckBox(const std::string& name, const std::string& text, int x, int y, int w, int h) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        if (controls.find(name) != controls.end()) return;
        int id = nextControlId++;
        HWND ctrlHwnd = createControlOnGuiThread(name, "checkbox", text, x, y, w, h, id);
        
        FormControl ctrl = { name, "checkbox", ctrlHwnd, false, id };
        controls[name] = ctrl;
        controlIdToName[id] = name;
    }

    bool clicked(const std::string& name) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        auto it = controls.find(name);
        if (it != controls.end() && it->second.type == "button") {
            bool wasClicked = it->second.clicked;
            it->second.clicked = false; // Reset the clicked state on read
            return wasClicked;
        }
        return false;
    }

    std::string get(const std::string& name) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        auto it = controls.find(name);
        if (it != controls.end()) {
            if (it->second.type == "textbox" || it->second.type == "label" || it->second.type == "button") {
                int len = GetWindowTextLengthA(it->second.hwndControl);
                std::string buf(len + 1, '\0');
                int actual = GetWindowTextA(it->second.hwndControl, &buf[0], len + 1);
                buf.resize(actual);
                return buf;
            } else if (it->second.type == "checkbox") {
                LRESULT state = SendMessage(it->second.hwndControl, BM_GETCHECK, 0, 0);
                return (state == BST_CHECKED) ? "true" : "false";
            }
        }
        return "";
    }

    void set(const std::string& name, const std::string& text) {
        std::lock_guard<std::recursive_mutex> lock(mtx);
        auto it = controls.find(name);
        if (it != controls.end()) {
            if (it->second.type == "textbox" || it->second.type == "label" || it->second.type == "button") {
                SetWindowTextA(it->second.hwndControl, text.c_str());
            } else if (it->second.type == "checkbox") {
                WPARAM checkState = (text == "true" || text == "1") ? BST_CHECKED : BST_UNCHECKED;
                SendMessage(it->second.hwndControl, BM_SETCHECK, checkState, 0);
            }
        }
    }

private:
    HWND createControlOnGuiThread(const std::string& name, const std::string& type, const std::string& text, int x, int y, int w, int h, int id) {
        CreateControlData data = { name, type, text, x, y, w, h, id, hwnd };
        SendMessage(hwnd, WM_USER_CREATE_CONTROL, 0, (LPARAM)&data);
        return data.hwndResult;
    }

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        FormInstance* self = nullptr;
        if (msg == WM_NCCREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            self = (FormInstance*)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
            self->hwnd = hwnd;
        } else {
            self = (FormInstance*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        }

        if (self != nullptr) {
            switch (msg) {
                case WM_USER_CREATE_CONTROL: {
                    CreateControlData* data = (CreateControlData*)lParam;
                    DWORD dwStyle = WS_CHILD | WS_VISIBLE;
                    const char* className = "";
                    
                    if (data->type == "button") {
                        className = "BUTTON";
                        dwStyle |= BS_PUSHBUTTON;
                    } else if (data->type == "label") {
                        className = "STATIC";
                        dwStyle |= SS_LEFT;
                    } else if (data->type == "textbox") {
                        className = "EDIT";
                        dwStyle |= ES_LEFT | ES_AUTOHSCROLL | WS_BORDER;
                    } else if (data->type == "checkbox") {
                        className = "BUTTON";
                        dwStyle |= BS_AUTOCHECKBOX;
                    }

                    HWND ctrlHwnd = CreateWindowExA(
                        0, className, data->text.c_str(),
                        dwStyle,
                        data->x, data->y, data->w, data->h,
                        data->hwndParent, (HMENU)(INT_PTR)data->id,
                        GetModuleHandle(NULL), NULL
                    );

                    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                    SendMessage(ctrlHwnd, WM_SETFONT, (WPARAM)hFont, TRUE);

                    data->hwndResult = ctrlHwnd;
                    return TRUE;
                }
                case WM_COMMAND: {
                    int id = LOWORD(wParam);
                    int code = HIWORD(wParam);
                    std::lock_guard<std::recursive_mutex> lock(self->mtx);
                    auto itName = self->controlIdToName.find(id);
                    if (itName != self->controlIdToName.end()) {
                        std::string name = itName->second;
                        auto itCtrl = self->controls.find(name);
                        if (itCtrl != self->controls.end()) {
                            if (itCtrl->second.type == "button" && code == BN_CLICKED) {
                                itCtrl->second.clicked = true;
                            }
                        }
                    }
                    return 0;
                }
                case WM_CLOSE: {
                    DestroyWindow(hwnd);
                    return 0;
                }
                case WM_DESTROY: {
                    std::lock_guard<std::recursive_mutex> lock(self->mtx);
                    self->isWindowOpen = false;
                    PostQuitMessage(0);
                    return 0;
                }
            }
        }

        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    void run() {
        HINSTANCE hInst = GetModuleHandle(NULL);
        
        WNDCLASSW wc = {};
        wc.lpfnWndProc = &FormInstance::wndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"EpilespyLangFormClass";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); // standard Windows forms dialog background
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);

        RegisterClassW(&wc);

        RECT r = {0, 0, width, height};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, false);
        int w = r.right - r.left;
        int h = r.bottom - r.top;

        std::wstring wtitle(title.begin(), title.end());

        HWND tempHwnd = CreateWindowExW(
            0,
            L"EpilespyLangFormClass",
            wtitle.c_str(),
            WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT,
            w, h,
            NULL, NULL, hInst, this
        );

        if (!tempHwnd) {
            std::lock_guard<std::recursive_mutex> lock(mtx);
            isWindowOpen = false;
            return;
        }

        ShowWindow(tempHwnd, SW_SHOW);
        UpdateWindow(tempHwnd);

        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            if (!IsDialogMessage(tempHwnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        {
            std::lock_guard<std::recursive_mutex> lock(mtx);
            hwnd = nullptr;
            isWindowOpen = false;
        }
    }
};

#ifdef TRUE
#undef TRUE
#endif
#ifdef FALSE
#undef FALSE
#endif
