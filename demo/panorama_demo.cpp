// подключаем движок
#include "../avoengine.h"
#include "../avoextension.h"
// графика
#include <GL/glu.h>
#include <GL/glut.h>
#include <SOIL/SOIL.h>
// математика
#include <cmath>

using namespace std;

// берём размеры окна из движка
extern int window_w,window_h;

//              параметры камеры
static float cam_x=0,cam_y=1.7f,cam_z=0;
// углы поворота в градусах
static float cam_pitch=0,cam_yaw=0;
// какие клавиши зажаты
static bool keys[256]={},skeys[512]={};

//              константы игры
// скорость движения и поворота
static const float MOVE_SPEED=6.0f;
static const float TURN_SPEED=90.0f;
// шаг сетки — одна сфера на ячейку
static const float CELL=8.0f;
// дальность прорисовки
static const int   FLOOR_R=40,SPHERE_R=25;
// дальняя граница отсечения
static const float MAX_DIST=200.0f;

//              display list для сферы — рисуется один раз, потом только glCallList
static GLuint sph_list=0;

static void buildSphereList(float R,int stacks,int slices){
    sph_list=glGenLists(1);
    glNewList(sph_list,GL_COMPILE);
    // строим сферу треугольными полосами
    for(int i=0;i<stacks;++i){
        const float phi0=float(M_PI)*i/stacks;
        const float phi1=float(M_PI)*(i+1)/stacks;
        glBegin(GL_TRIANGLE_STRIP);
        for(int j=0;j<=slices;++j){
            const float theta=2.0f*float(M_PI)*j/slices;
            // верхняя вершина полосы
            glVertex3f(R*sinf(phi0)*cosf(theta),R*cosf(phi0),R*sinf(phi0)*sinf(theta));
            // нижняя вершина полосы
            glVertex3f(R*sinf(phi1)*cosf(theta),R*cosf(phi1),R*sinf(phi1)*sinf(theta));
        }
        glEnd();
    }
    glEndList();
}

//              позиция сферы по функции от координат ячейки
static inline void sphereAt(int ix,int iz,float& x,float& y,float& z){
    // смещаем синусоидами чтобы не было скучной сетки
    x=ix*CELL+0.5f*CELL*sinf(ix*1.3f+iz*0.7f);
    z=iz*CELL+0.5f*CELL*cosf(iz*1.1f-ix*0.9f);
    // высота волнообразная, достаточно высоко чтобы не мешала ходить
    y=6.0f+3.0f*sinf(ix*0.8f)*cosf(iz*0.6f);
}

//              вектор взгляда в плоскости XZ, обновляется каждый кадр
static float fwd_x=0,fwd_z=1;

// отсечение — точка за спиной или дальше MAX_DIST
static inline bool inFront(float px,float pz,float margin){
    const float dx=px-cam_x,dz=pz-cam_z;
    const float dist2=dx*dx+dz*dz;
    const float lim=MAX_DIST+margin;
    if(dist2>lim*lim) return false;
    const float dot=dx*fwd_x+dz*fwd_z;
    return dot>-margin||dist2<margin*margin*4.0f;
}

//              рисуем весь пол за один glBegin/glEnd — ноль лишних draw calls
static void drawFloor(){
    glDisable(GL_TEXTURE_2D);
    const int tcx=(int)floorf(cam_x*0.5f);
    const int tcz=(int)floorf(cam_z*0.5f);
    // светлые и тёмные плитки в двух отдельных батчах чтобы не менять цвет внутри
    // светлые
    glColor3f(0.52f,0.46f,0.36f);
    glBegin(GL_QUADS);
    for(int di=-FLOOR_R;di<=FLOOR_R;++di)
        for(int dj=-FLOOR_R;dj<=FLOOR_R;++dj){
            const int tx=tcx+di,tz=tcz+dj;
            if(((tx+tz)&1)!=0) continue;
            const float wx=tx*2.0f,wz=tz*2.0f;
            if(!inFront(wx,wz,4.0f)) continue;
            glVertex3f(wx-1,0,wz-1);
            glVertex3f(wx+1,0,wz-1);
            glVertex3f(wx+1,0,wz+1);
            glVertex3f(wx-1,0,wz+1);
        }
    glEnd();
    // тёмные
    glColor3f(0.32f,0.27f,0.20f);
    glBegin(GL_QUADS);
    for(int di=-FLOOR_R;di<=FLOOR_R;++di)
        for(int dj=-FLOOR_R;dj<=FLOOR_R;++dj){
            const int tx=tcx+di,tz=tcz+dj;
            if(((tx+tz)&1)!=1) continue;
            const float wx=tx*2.0f,wz=tz*2.0f;
            if(!inFront(wx,wz,4.0f)) continue;
            glVertex3f(wx-1,0,wz-1);
            glVertex3f(wx+1,0,wz-1);
            glVertex3f(wx+1,0,wz+1);
            glVertex3f(wx-1,0,wz+1);
        }
    glEnd();
}

//              рисуем сферы через display list — геометрия уже на видеокарте
static void drawSpheres(){
    glDisable(GL_TEXTURE_2D);
    const int cx=(int)floorf(cam_x/CELL);
    const int cz=(int)floorf(cam_z/CELL);
    // цвет один для всех сфер — задаём один раз
    glColor3f(1.0f,0.78f,0.0f);
    glDisable(GL_TEXTURE_2D);
    for(int di=-SPHERE_R;di<=SPHERE_R;++di)
        for(int dj=-SPHERE_R;dj<=SPHERE_R;++dj){
            float sx,sy,sz;
            sphereAt(cx+di,cz+dj,sx,sy,sz);
            if(!inFront(sx,sz,2.0f)) continue;
            // сдвигаемся к позиции сферы и вызываем display list
            glPushMatrix();
            glTranslatef(sx,sy,sz);
            glCallList(sph_list);
            glPopMatrix();
        }
}

//              полноэкранный режим
static bool is_fullscreen=false;
static int  saved_x,saved_y,saved_w,saved_h;

static void toggleFullscreen(){
    if(!is_fullscreen){
        saved_x=glutGet(GLUT_WINDOW_X); saved_y=glutGet(GLUT_WINDOW_Y);
        saved_w=glutGet(GLUT_WINDOW_WIDTH); saved_h=glutGet(GLUT_WINDOW_HEIGHT);
        glutFullScreen();
    }else{
        glutReshapeWindow(saved_w,saved_h);
        glutPositionWindow(saved_x,saved_y);
    }
    is_fullscreen=!is_fullscreen;
}

//              колбэки glut
static void onDisplay(){
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // 1. Обновляем вектор взгляда
    const float yr = cam_yaw * float(M_PI) / 180.0f;
    fwd_x = sinf(yr); fwd_z = cosf(yr);

    // 2. Устанавливаем камеру (обновляет матрицы проекции и вида)
    setup_camera(60.0f, cam_x, cam_y, cam_z, cam_pitch, cam_yaw);

    // 3. Рисуем небо/панораму (в avoextension она обычно сбрасывает позицию, оставляя поворот)
    draw_panorama(cam_x, cam_y, cam_z);

    // 4. Мир
    drawFloor();
    drawSpheres();

    // 5. Интерфейс (движок сам переключит матрицы в 2D и обратно)
    draw_performance_hud(window_w, window_h);

    glutSwapBuffers();
}

static void onReshape(int w,int h){
    window_w=w; window_h=h;
    changeSize3D(w,h);
}

static void onKeyDown(unsigned char key,int,int){
    keys[key]=true;
    if(key==27){if(is_fullscreen) toggleFullscreen(); else exit(0);}
    if(key=='f'||key=='F') toggleFullscreen();
}

static void onKeyUp     (unsigned char key,int,int){ keys[key] =false; }
static void onSpecialDown(int key,int,int)          { skeys[key]=true;  }
static void onSpecialUp  (int key,int,int)          { skeys[key]=false; }

static void onIdle(){
    static float prev_t=0;
    const float t=glutGet(GLUT_ELAPSED_TIME)*0.001f;
    const float dt=fminf(t-prev_t,0.05f);
    prev_t=t;
    // поворот стрелками
    if(skeys[GLUT_KEY_LEFT])  cam_yaw +=TURN_SPEED*dt;
    if(skeys[GLUT_KEY_RIGHT]) cam_yaw -=TURN_SPEED*dt;
    if(skeys[GLUT_KEY_UP])    cam_pitch+=TURN_SPEED*dt;
    if(skeys[GLUT_KEY_DOWN])  cam_pitch-=TURN_SPEED*dt;
    // ограничиваем вертикальный угол чтобы не перевернуться
    if(cam_pitch> 89) cam_pitch= 89;
    if(cam_pitch<-89) cam_pitch=-89;
    // движение wasd
    const float yr=cam_yaw*float(M_PI)/180.0f;
    const float mv=MOVE_SPEED*dt;
    if(keys['w']||keys['W']){ cam_x+=sinf(yr)*mv; cam_z+=cosf(yr)*mv; }
    if(keys['s']||keys['S']){ cam_x-=sinf(yr)*mv; cam_z-=cosf(yr)*mv; }
    if(keys['a']||keys['A']){ cam_x+=cosf(yr)*mv; cam_z-=sinf(yr)*mv; }
    if(keys['d']||keys['D']){ cam_x-=cosf(yr)*mv; cam_z+=sinf(yr)*mv; }
    glutPostRedisplay();
}

//              точка входа
int main(int argc, char** argv) {
    // СНАЧАЛА инициализация контекста
    setup_display(&argc, argv, 0.02f, 0.02f, 0.12f, 1.0f, "fog_demo", 1280, 720);
    
    // ТЕПЕРЬ можно работать с ресурсами OpenGL
    set_panorama("demo/src/stargazer.png");
    enable_fog(0.005, 0.02f, 0.02f, 0.12f);
    buildSphereList(0.45f, 6, 7); 

    // Регистрация колбэков
    glutDisplayFunc(onDisplay);
    glutReshapeFunc(onReshape);
    glutKeyboardFunc(onKeyDown);
    glutKeyboardUpFunc(onKeyUp);
    glutSpecialFunc(onSpecialDown);
    glutSpecialUpFunc(onSpecialUp);
    glutIdleFunc(onIdle);

    glutMainLoop();
    return 0;
}