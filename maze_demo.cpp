#include "avoengine.h"
#include <GL/glut.h>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <stack>
#include <iostream>
#include <ctime>

using namespace std;

// ── Константы ────────────────────────────────────────────────────
static const float C  = 4.0f;  // размер клетки в мировых единицах
static const float WH = 2.0f;  // высота стены
static const float PR = 0.4f;  // радиус коллизии игрока
static const int   CN = 16;    // клеток в чанке

// ── Камера ───────────────────────────────────────────────────────
static float camX=2, camY=1, camZ=2, yaw=0, pitch=0;
static bool  keys[256]={};
static int   winW=1280, winH=720;
static bool  ignoreWarp=false;
static bool  fullscreen=false;
static long long SEED=0;

// ── Лабиринт ─────────────────────────────────────────────────────
struct Cell { bool N,S,E,W; };
static unordered_map<long long,vector<Cell>> cache;

static unsigned int lcg(unsigned int&s){s=s*1664525u+1013904223u;return s;}
static long long ck(int cx,int cz){return((long long)cx<<32)|(unsigned)cz;}

static vector<Cell> makeChunk(int cx,int cz){
    vector<Cell> g(CN*CN,{true,true,true,true});
    unsigned int r=(unsigned int)(SEED^((long long)cx*2654435761LL)^((long long)cz*2246822519LL));
    vector<bool> vis(CN*CN,false);
    stack<int> st;
    vis[0]=true; st.push(0);
    while(!st.empty()){
        int cur=st.top(), x=cur%CN, z=cur/CN;
        int dx[]={0,0,1,-1}, dz[]={1,-1,0,0};
        for(int i=3;i>0;i--){
            int j=lcg(r)%(i+1);
            swap(dx[i],dx[j]); swap(dz[i],dz[j]);
        }
        bool mv=false;
        for(int i=0;i<4;i++){
            int nx=x+dx[i],nz=z+dz[i];
            if(nx<0||nx>=CN||nz<0||nz>=CN) continue;
            int ni=nz*CN+nx;
            if(vis[ni]) continue;
            if(dx[i]== 1){g[cur].E=false;g[ni].W=false;}
            if(dx[i]==-1){g[cur].W=false;g[ni].E=false;}
            if(dz[i]== 1){g[cur].N=false;g[ni].S=false;}
            if(dz[i]==-1){g[cur].S=false;g[ni].N=false;}
            vis[ni]=true; st.push(ni); mv=true; break;
        }
        if(!mv) st.pop();
    }
    // пробиваем границы чанков
    unsigned int r2=(unsigned int)(SEED^((long long)cx*1234567891LL)^((long long)cz*987654321LL));
    for(int z=0;z<CN;z++){lcg(r2);if(r2%3!=0)g[z*CN+CN-1].E=false;}
    unsigned int r3=(unsigned int)(SEED^((long long)cx*111111111LL)^((long long)cz*222222222LL));
    for(int x=0;x<CN;x++){lcg(r3);if(r3%3!=0)g[(CN-1)*CN+x].N=false;}
    return g;
}

static const vector<Cell>& getChunk(int cx,int cz){
    auto k=ck(cx,cz);
    if(!cache.count(k)) cache[k]=makeChunk(cx,cz);
    return cache[k];
}

// Получить клетку по мировым координатам клетки (не пикселям!)
// wx, wz — индекс клетки (floor(worldX / C))
static Cell cellAt(int wx,int wz){
    int cx=(wx>=0)?wx/CN:(wx-CN+1)/CN;
    int cz=(wz>=0)?wz/CN:(wz-CN+1)/CN;
    int lx=wx-cx*CN, lz=wz-cz*CN;
    return getChunk(cx,cz)[lz*CN+lx];
}

// ── Коллизия ─────────────────────────────────────────────────────
static void collide(float&nx,float&nz){
    // Индекс клетки по мировым координатам
    auto ci=[](float v)->int{return(v>=0)?(int)(v/C):(int)floor(v/C);};
    for(int dz=-1;dz<=1;dz++) for(int dx=-1;dx<=1;dx++){
        int cx=ci(nx)+dx, cz=ci(nz)+dz;
        Cell c=cellAt(cx,cz);
        float L=cx*C, R=L+C, B=cz*C, T=B+C;
        const float E=0.01f;
        // Стена по Z (N/S)
        auto pZ=[&](float w){
            if(nx+PR>L&&nx-PR<R){
                if(nz<w  && nz+PR>w-E) nz=w-PR-E;
                if(nz>=w && nz-PR<w+E) nz=w+PR+E;
            }
        };
        // Стена по X (E/W)
        auto pX=[&](float w){
            if(nz+PR>B&&nz-PR<T){
                if(nx<w  && nx+PR>w-E) nx=w-PR-E;
                if(nx>=w && nx-PR<w+E) nx=w+PR+E;
            }
        };
        if(c.N) pZ(T); if(c.S) pZ(B);
        if(c.E) pX(R); if(c.W) pX(L);
    }
}

// ── Рисование ────────────────────────────────────────────────────
// Шахматный цвет пола (синие тона)
static void floorCol(int tx,int tz,float&r,float&g,float&b){
    if(((tx&1)^(tz&1))==0){r=0.20f;g=0.30f;b=0.55f;}
    else                   {r=0.45f;g=0.58f;b=0.78f;}
}
// Шахматный цвет стен (коричневые тона)
static void wallCol(int tx,int tz,float&r,float&g,float&b){
    if(((tx&1)^(tz&1))==0){r=0.55f;g=0.35f;b=0.18f;}
    else                   {r=0.85f;g=0.70f;b=0.50f;}
}

// Плоская вертикальная стена через square().
// square() рисует квад [-0.5..0.5]^2 в плоскости XY, масштабируется local_size.
// Нам нужна стена шириной C и высотой WH.
// Используем local_size=1, вершины задаём вручную через vertices.
// vertices для square: 4 точки (x,y) в локальном пространстве.
static void drawWall(float wx,float wy,float wz,bool alongX,float r,float g,float b){
    // Стена в плоскости XY (local), потом повернём:
    //   alongX=true  → стена вдоль X, rotate=0
    //   alongX=false → стена вдоль Z, rotate=90
    float hw=C*0.5f, hh=WH*0.5f;
    float verts[8]={-hw,-hh, hw,-hh, hw,hh, -hw,hh};

    // square рисует в плоскости XY с центром (x,y).
    // Нам нужна вертикальная стена в 3D — используем glPushMatrix вручную
    // или... используем draw3DObject с нулевой толщиной.
    // draw3DObject — функция движка, используем её.
    // Квад: 4 вершины, 2 треугольника
    float v[]={
        -hw,-hh,0,  hw,-hh,0,  hw,hh,0, -hw,hh,0
    };
    int idx[]={0,1,2,0,2,3, 2,1,0,3,2,0}; // двусторонний
    float tc[]={0,0,1,0,1,1,0,1, 0,0,1,0,1,1,0,1};

    float ry = alongX ? 0.0f : 90.0f;
    draw3DObject(1.0f, wx,wy,wz, 0,ry,0, r,g,b, nullptr, 4,v,12,idx,tc);
}

// Пол через square() — горизонтальный квад.
// square() рисует в плоскости XY, нам нужна плоскость XZ.
// Используем draw3DObject повёрнутый на 90° по X.
static void drawFloorTile(float wx,float wz,float r,float g,float b){
    float hw=C*0.5f;
    float v[]={-hw,0,-hw, hw,0,-hw, hw,0,hw, -hw,0,hw};
    int idx[]={0,1,2,0,2,3, 2,1,0,3,2,0};
    float tc[]={0,0,1,0,1,1,0,1, 0,0,1,0,1,1,0,1};
    draw3DObject(1.0f, wx,0,wz, 0,0,0, r,g,b, nullptr, 4,v,12,idx,tc);
}

static void drawWorld(){
    int pcx=(int)floor(camX/(CN*C));
    int pcz=(int)floor(camZ/(CN*C));

    for(int dcz=-2;dcz<=2;dcz++) for(int dcx=-2;dcx<=2;dcx++){
        int chx=pcx+dcx, chz=pcz+dcz;
        const auto&g=getChunk(chx,chz);

        for(int z=0;z<CN;z++) for(int x=0;x<CN;x++){
            int tx=chx*CN+x, tz=chz*CN+z;
            float wx=tx*C+C*0.5f, wz=tz*C+C*0.5f;
            float r,gb,b;

            // Пол
            floorCol(tx,tz,r,gb,b);
            drawFloorTile(wx,wz,r,gb,b);

            // Стены
            const Cell&c=g[z*CN+x];
            if(c.N){wallCol(tx,tz,r,gb,b); drawWall(wx,WH*0.5f,wz+C*0.5f, true, r,gb,b);}
            if(c.W){wallCol(tx,tz,r,gb,b); drawWall(wx-C*0.5f,WH*0.5f,wz, false,r,gb,b);}
            if(z==0&&c.S){wallCol(tx,tz,r,gb,b); drawWall(wx,WH*0.5f,wz-C*0.5f, true, r,gb,b);}
            if(x==CN-1&&c.E){wallCol(tx,tz,r,gb,b); drawWall(wx+C*0.5f,WH*0.5f,wz, false,r,gb,b);}
        }
    }
}

// ── Рендер ───────────────────────────────────────────────────────
static void render(){
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    float yr=yaw*(float)M_PI/180, pr=pitch*(float)M_PI/180;
    float lx=cosf(pr)*sinf(yr), ly=sinf(pr), lz=cosf(pr)*cosf(yr);
    setup_camera(75,(float)winW/winH,0.05f,300,
                 camX,camY,camZ, camX+lx,camY+ly,camZ+lz, 0,1,0);
    drawWorld();
    glutSwapBuffers();
}

static void update(int){
    float yr=yaw*(float)M_PI/180;
    float fx=sinf(yr),fz=cosf(yr), rx=cosf(yr),rz=-sinf(yr);
    float nx=camX,nz=camZ;
    if(keys['w']||keys['W']){nx+=fx*0.1f;nz+=fz*0.1f;}
    if(keys['s']||keys['S']){nx-=fx*0.1f;nz-=fz*0.1f;}
    if(keys['a']||keys['A']){nx+=rx*0.1f;nz+=rz*0.1f;}
    if(keys['d']||keys['D']){nx-=rx*0.1f;nz-=rz*0.1f;}
    collide(nx,nz);
    camX=nx; camZ=nz;
    glutPostRedisplay();
    glutTimerFunc(16,update,0);
}

static void onMouse(int mx,int my){
    if(ignoreWarp){ignoreWarp=false;return;}
    int cx=winW/2,cy=winH/2;
    yaw  -=(mx-cx)*0.15f;
    pitch-=(my-cy)*0.15f;
    if(pitch>89)pitch=89; if(pitch<-89)pitch=-89;
    ignoreWarp=true;
    glutWarpPointer(cx,cy);
}

static void reshape(int w,int h){
    winW=w;winH=h;
    changeSize3D(w,h);
    ignoreWarp=true;
    glutWarpPointer(w/2,h/2);
}

static void keyDown(unsigned char k,int,int){
    keys[k]=true;
    if(k==27) exit(0);
    if(k=='f'||k=='F'){
        fullscreen=!fullscreen;
        if(fullscreen) glutFullScreen();
        else{ glutReshapeWindow(1280,720); glutPositionWindow(100,100); }
    }
}
static void keyUp(unsigned char k,int,int){keys[k]=false;}

int main(int argc,char**argv){
    SEED=(long long)time(nullptr);
    setup_display(&argc,argv,0.02f,0.02f,0.05f,1,"Infinite 3D Maze",winW,winH);
    glutReshapeFunc(reshape);  glutDisplayFunc(render);
    glutKeyboardFunc(keyDown); glutKeyboardUpFunc(keyUp);
    glutPassiveMotionFunc(onMouse); glutMotionFunc(onMouse);
    glutSetCursor(GLUT_CURSOR_NONE);
    glutTimerFunc(16,update,0);
    cout<<SEED<<endl;
    glutMainLoop();
}