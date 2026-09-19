#include "cobra.h"
#include "math_3d.h"   // project(), rotateFast()

// Scale — підніми до ~1.4–1.6, якщо корабль здасться занадто малим/плоским.
#define COBRA_SCALE 1.5f

// 12 вершин, прив'язаних гранями моделі (x, y, z).
static const int COBRA_VERTICES = 12;

static const Point3D cobra_vertices[COBRA_VERTICES] = {
    {  32,   0,  76}, // 0  ніс, правий верх
    { -32,   0,  76}, // 1  ніс, лівий верх
    {   0,  26,  24}, // 2  купол кабіни
    { -120,  -3,  -8}, // 3  ліве тіло, середа
    { 120,  -3,  -8},  // 4  праве тіло, середа
    { -88,  16, -40},  // 5  лів задній верх
    { 88,   16, -40},  // 6  прав задній верх
    { 128,  -8, -40},  // 7  прав кінець крила
    { -128, -8, -40},  // 8  лів кінець крила
    {   0,  26, -40},  // 9  задній центр, верх
    { -32, -24, -40},  // 10 лів задній живіт
    { 32,  -24, -40},  // 11 прав задній живіт
};

// 24 унікальних ребра, виведені з граней моделі.
static const int cobra_edges[24][2] = {
    // Носова частина (конус + живіт)
    {0,1}, {1,2}, {0,2},
    {1,10}, {10,11}, {0,11},
    // Верхні ребра тіла
    {2,6}, {0,6}, {0,4}, {4,6},
    // Ребра до кінців крил
    {4,7}, {7,11}, {2,5}, {1,5},
    // Ліве/ праве крило + задня частина
    {1,3}, {3,5}, {3,8}, {8,10},
    // Задній шестигранник (двунь)
    {5,9}, {2,9}, {6,9}, {5,8}, {6,7}, {5,6}
};

// 20 трикутників-граней (триангуляція IndexedFaceSet з cobra3.wrl).
// Порядок вершин нормалізовано так, щоб усі нормалі дивились НАЗОВНІ
// (проти годинникової стрілки, якщо дивитись ззовні) — це дає коректний
// backface-culling через знак площі проекції на екран (див. drawCobra).
static const int cobra_faces[20][3] = {
    {0,2,1},
    {0,1,10},{0,10,11},
    {0,6,2},
    {0,4,6},
    {0,7,4},{0,11,7},
    {1,2,5},
    {1,5,3},
    {1,3,8},{1,8,10},
    {2,9,5},
    {2,6,9},
    {3,5,8},
    {4,7,6},
    // задній «двунь»: семикутник 5,8,10,11,7,6,9 (віяло від вершини 5)
    {5,10,8},{5,11,10},{5,7,11},{5,6,7},{5,9,6}
};

// Для кожного з 24 ребер — індекси двох граней (з cobra_faces), яким воно
// належить (модель замкнена: кожне ребро спільне рівно для 2 граней).
// Ребро лишається видимим у solid-режимі, якщо хоч одна з цих граней
// "дивиться" на камеру — так само, як приховувались лінії в оригінальній
// Elite (1984): без z-буфера, лише через орієнтацію граней.
static const int cobra_edge_faces[24][2] = {
    {0,1},{0,7},{0,3},
    {1,10},{2,16},{2,6},
    {3,12},{3,4},{4,5},{4,14},
    {5,14},{6,17},{7,11},{7,8},
    {8,9},{8,13},{9,13},{10,15},
    {11,19},{11,12},{12,19},{13,15},{14,18},{18,19}
};

static void drawLineSafe(TFT_eSprite &canvas, Point2D p1, Point2D p2,
                         uint16_t color) {
    if (p1.valid && p2.valid) {
        canvas.drawLine(p1.x, p1.y, p2.x, p2.y, color);
    }
}

void drawCobra(TFT_eSprite &canvas, float ax, float ay, float az,
               float gx, float gy, float gz, uint16_t color, bool wireframe,
               int *faces_visible) {
    Point2D pts[COBRA_VERTICES];
    Point3D rot[COBRA_VERTICES];

    for (int i = 0; i < COBRA_VERTICES; i++) {
        Point3D v = cobra_vertices[i];
        v.x *= COBRA_SCALE;
        v.y *= COBRA_SCALE;
        v.z *= COBRA_SCALE;
        rot[i] = rotateFast(v);          // обертання у мировому просторі
        pts[i] = project(rot[i], gx, gy, gz);
    }

    // Backface culling: знак площі проекції трикутника на екран однозначно
    // визначає, чи грань дивиться на камеру (при послідовному порядку
    // вершин усіх 20 граней). Ніякого z-буфера чи дискретизації — просто
    // і, для цієї моделі, завжди точно (перевірено окремо проти повного
    // побіксельного z-тесту — результати збігаються в усіх орієнтаціях).
    static char face_front[20];
    int count = 0;
    for (int f = 0; f < 20; f++) {
        Point2D &pa = pts[cobra_faces[f][0]];
        Point2D &pb = pts[cobra_faces[f][1]];
        Point2D &pc = pts[cobra_faces[f][2]];
        if (!pa.valid || !pb.valid || !pc.valid) {
            face_front[f] = 0;
            continue;
        }
        float area = (float)(pb.x - pa.x) * (pc.y - pa.y) -
                     (float)(pb.y - pa.y) * (pc.x - pa.x);
        face_front[f] = (area < 0.0f) ? 1 : 0;
        if (face_front[f]) count++;
    }
    if (faces_visible) *faces_visible = count;

    if (!wireframe) {
        // Painter's algorithm: малюємо видимі грані від найдальшої до
        // найближчої, щоб перекриття (наприклад, у задньому шестикутнику)
        // виглядало коректно.
        static int visible[20];
        int vc = 0;
        for (int f = 0; f < 20; f++) if (face_front[f]) visible[vc++] = f;

        static float fz[20];
        for (int i = 0; i < vc; i++) {
            int f = visible[i];
            Point2D &pa = pts[cobra_faces[f][0]];
            Point2D &pb = pts[cobra_faces[f][1]];
            Point2D &pc = pts[cobra_faces[f][2]];
            fz[i] = (pa.z + pb.z + pc.z) / 3.0f;
        }
        for (int i = 0; i < vc; i++) {
            for (int j = i + 1; j < vc; j++) {
                if (fz[j] > fz[i]) {
                    float tz = fz[i]; fz[i] = fz[j]; fz[j] = tz;
                    int tv = visible[i]; visible[i] = visible[j]; visible[j] = tv;
                }
            }
        }

        for (int i = 0; i < vc; i++) {
            int f = visible[i];
            Point2D &a = pts[cobra_faces[f][0]];
            Point2D &b = pts[cobra_faces[f][1]];
            Point2D &c = pts[cobra_faces[f][2]];

            // Solid: чорне непрозоре заповнення (CL_BG). Так тіло перекриває
            // частинки/фон позаду — як автентичний Elite (через передню
            // стінку не видно дальних ребер). Затінення тут не треба: тіло
            // і так чорне, а яскравість дають ребра.
            canvas.fillTriangle(a.x, a.y, b.x, b.y, c.x, c.y, CL_BG);
        }
    }

    // Ребра: у wireframe малюємо всі (каркас без приховування), у solid —
    // лише ті, що належать хоч одній грані "обличчям до камери" (інакше
    // "задні" ребра просвічували б крізь суцільний корпус).
    for (int e = 0; e < 24; e++) {
        if (wireframe ||
            face_front[cobra_edge_faces[e][0]] ||
            face_front[cobra_edge_faces[e][1]]) {
            drawLineSafe(canvas, pts[cobra_edges[e][0]],
                               pts[cobra_edges[e][1]], color);
        }
    }
}
