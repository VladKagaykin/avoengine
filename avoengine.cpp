//              звук
// указываем что здесь реализация библиотеки, т.к. miniaudio это только заголовочный файл и даём понять что
// это главная программа
#define MINIAUDIO_IMPLEMENTATION
// указываем что пользуемся только указанным api для воспроизведения звука(pulseaudio и прочая шняга), если они не указаны,
// то программа компилируется для всех api
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
// указываем что api некий alsa(встроенный в линукс)
#define MA_ENABLE_ALSA
// импортируем сам miniaudio
#include "miniaudio.h"

//              движок
// указываем заголовочный файл движка
#include "avoengine.h"
// библиотеки для многопоточности
#include <omp.h>
#include <mutex>

//              графика
// вспомогательные утилиты для opengl(матрицы, проекции и прочие нежности для немощей)
#include <GL/glu.h>
// основная библиотека opengl
#include <GL/glut.h>
// библиотека для импорта текстур
#include <SOIL/SOIL.h>

//              утилиты
// библиотека для работы со временем для замеров производительности
#include <chrono>
// библиотека для того чтобы определить названия компонентов
#include <hwinfo/hwinfo.h>
// математика(п, синусы, косинусы)
#include <cmath>
// удобная запись в переменные через printf и прочую хрень
#include <cstdio>
// взаимодействия с консолью
#include <iostream>
// таблица номер-значение, поможет для текстур
#include <unordered_map>
// нелоховские массивы
#include <vector>
// лоховской текст
#include <string>

//              объявления
// использование пространства имён std 😲
using namespace std;
// переменные для хранения в них размеров окна и экрана
int window_w = 0, window_h = 0, screen_w = 0, screen_h = 0;
string cpu_name;
string ram_v;
string gpu_name;
// создание таблицы текстур и их id 
//       имя файла текстуры  его id        
static unordered_map<string, GLuint> textureCache;
// переменная для хранения того, обрабатывает ли текстуры какой-то поток или нет(вроде бы, не знаю как точно)
static mutex textureCacheMutex;
// храним id последней загруженной текстуры
static GLuint boundTextureID = 0;

// инициализация звукового движка(ma_engine тип данных, а audio_engine название)
ma_engine audio_engine;
// вектор в котором хранятся звуки, которые играют на постоянке
static vector<ma_sound*> loopingSounds;

// структура для зранения параметров камеры
struct CameraParams {
    float fov   = 60.0f;
    float znear = 0.1f;
    float zfar  = 1000.0f;
    float eye_x = 0, eye_y = 0, eye_z = 0;
    float ctr_x = 0, ctr_y = 0, ctr_z = 1;
    float up_x  = 0, up_y  = 1, up_z  = 0;
};
// инициализация камеры
static CameraParams camera;

//              утилиты для камеры
// вычисляем то, куда смотрит центр камеры
static inline void lookAtForward(float eye_x,float eye_y,float eye_z,float pitch_deg,float yaw_deg,float& cx,float& cy,float& cz){
    // радианы наклона по вертикали
    const float p=pitch_deg*float(M_PI)/180.0f;
    // радианы наклона по горизонтали
    const float y=yaw_deg*float(M_PI)/180.0f;
    // считаем проекцию почти по теореме пифагора
    cx = eye_x + cosf(p) * sinf(y);
    cy = eye_y + sinf(p);
    cz = eye_z + cosf(p) * cosf(y);
}
// вычисляем тоже самое, только не то куда смотрит камера, а то, куда она не смотрит(не знаю как объяснить, короче,
// куда смотрела бы камера если бы её развернуло на 180)
static inline void lookAtBackward(float eye_x,float eye_y,float eye_z,float pitch_deg,float yaw_deg,float& cx,float& cy,float& cz,float& dx,float& dy,float& dz){
    const float p=pitch_deg*float(M_PI)/180.0f;
    const float y=yaw_deg*float(M_PI)/180.0f;
    dx=-cosf(p)*sinf(y);
    dy=-sinf(p);
    dz=-cosf(p)*cosf(y);
    cx=eye_x+dx;
    cy=eye_y+dy;
    cz=eye_z+dz;
}

// проверка на то, привязана ли эта текстура или нет
static inline void bindTexture(GLuint id){
    if (id!=boundTextureID){
        glBindTexture(GL_TEXTURE_2D,id);
        boundTextureID=id;
    }
}

//              текстуры
// функция для загрузки текстуры
GLuint loadTextureFromFile(const char* filename){
    // проверяем загружена ли текстура, закрываем замок чтобы другой поток не лез одновременно
    {
        lock_guard<mutex> lock(textureCacheMutex);
        auto it=textureCache.find(filename);
        if(it!=textureCache.end()) return it->second;
    }
    // загружаем изображение и записываем ей ширину и высоту в w и h
    int w,h;
    // название текстуры / w / h / сюда можно записать сколько каналов у изображения / принудительно указываем что 4 канала, чтобы была прозрачность
    unsigned char* img=SOIL_load_image(filename,&w,&h,nullptr,SOIL_LOAD_RGBA);
    if(!img){
        cerr<<"Cannot load texture: "<<filename<<" ("<<SOIL_last_result()<<")"<<endl;
        // закрываем замок и записываем что текстуры нет
        lock_guard<mutex> lock(textureCacheMutex);
        return textureCache[filename]=0;
    }
    // создаём новый id для текстуры
    GLuint id;
    // размер / записываем id
    glGenTextures(1,&id);
    // биндим эту текстуру как 2д
    glBindTexture(GL_TEXTURE_2D, id);
    boundTextureID=id;
    // параметры текстуры
    //      указываем цель / параметр который надо изменить/ его значение
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    // передаём текстуру в видеопамять
    // формат / детализация(хз что это значит) / формат хранения / ширина / высота / граница(также хз) / входной формат / тип данных / изображение
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,img);
    // освобождаем текстуру из памяти
    SOIL_free_image_data(img);
    // закрываем замок и возвращаем id текстуры
    lock_guard<mutex> lock(textureCacheMutex);
    return textureCache[filename]=id;
}
// загружаем много текстур параллельно
void preloadTextures(const vector<string>& filenames){
    // структура для хранения загруженных с диска данных до передачи в видеопамять
    struct RawTex{string name;unsigned char* data;int w,h;};
    vector<RawTex> loaded(filenames.size());
    // параллельно грузим файлы с диска
    // schedule(dynamic) значит что потоки берут задачи по одной по мере освобождения, а не поровну сразу
    // это нужно т.к. текстуры разного размера и некоторые грузятся дольше
    #pragma omp parallel for schedule(dynamic)
    for(int i=0;i<(int)filenames.size();++i){
        // пропускаем если текстура уже загружена, закрываем замок чтобы проверить безопасно
        {
            lock_guard<mutex> lock(textureCacheMutex);
            if(textureCache.count(filenames[i])){
                loaded[i]={filenames[i],nullptr,0,0};
                continue;
            }
        }
        int w,h;
        unsigned char* img=SOIL_load_image(filenames[i].c_str(),&w,&h,nullptr,SOIL_LOAD_RGBA);
        loaded[i]={filenames[i],img,w,h};
    }
    // передаём в видеопамять строго из одного потока т.к. opengl однопоточный
    for(auto& t:loaded){
        if(!t.data) continue;
        GLuint id;
        glGenTextures(1,&id);
        glBindTexture(GL_TEXTURE_2D,id);
        boundTextureID=id;
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,t.w,t.h,0,GL_RGBA,GL_UNSIGNED_BYTE,t.data);
        SOIL_free_image_data(t.data);
        // закрываем замок и записываем текстуру в таблицу
        lock_guard<mutex> lock(textureCacheMutex);
        textureCache[t.name]=id;
    }
}
// удаляем все текстуры из памяти
void clearTextureCache(){
    // перебираем все имена и id и удаляем их
    for(auto& [name,id] : textureCache)
        if(id) glDeleteTextures(1, &id);
    textureCache.clear();
    boundTextureID=0;
}

//              хз как это назвать
// поворачиваем текстуру вокруг точки
void rotatePoint(float& x,float& y,float cx,float cy,float angle_rad){
    // перенос в 0 для удобного рассчёта
    const float tx=x-cx,ty=y-cy;
    // рассчёт поворота и возвращаем как было
    const float c=cosf(angle_rad),s=sinf(angle_rad);
    x=cx+tx*c-ty*s;
    y=cy+tx*s+ty*c;
}

// функция для 2д фигур: указываем название текстуры и если она существует, то биндим её, если нет, то указываем что фигура не использует текстуру
static void enableTex(const char* file){
    if(!file){
        glDisable(GL_TEXTURE_2D);
        return;
    }
    GLuint id=loadTextureFromFile(file);
    if(id){
        glEnable(GL_TEXTURE_2D);
        bindTexture(id);
    }else{
        glDisable(GL_TEXTURE_2D);
    }
}

//              простые 2д фигуры
// треугольник
void triangle(float scale,float cx,float cy,double r,double g,double b,float rotate,const float* vertices,const char* tex){
    // задаём цвет
    glColor3f(float(r), float(g), float(b));
    // задаём/не задаём текстуру
    enableTex(tex);
    // преобразуем поворот в радианы
    const float ar=rotate*float(M_PI)/-180.0f;
    // задаём координаты текстуры
    const float tc[6]={0,1, 1,1, 0,0};
    // задаём что фигура-треугольник
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < 3; ++i) {
        // берём координаты вершины и рассчитываем их поворот
        float px=vertices[i*2], py=vertices[i*2+1];
        rotatePoint(px,py,0,0,ar);
        // отправляем координату текстуры
        if(tex)glTexCoord2f(tc[i*2],tc[i*2+1]);
        // отправляем вершину в opengl
        glVertex2f(cx+px*scale,cy+py*scale);
    }
    // объявляем что рисовка фигуры завершена
    glEnd();
    // выключаем текстуру
    if(tex)glDisable(GL_TEXTURE_2D);
}
// всё тоже самое, только квадрат
void square(float local_size,float x,float y,double r,double g,double b,float rotate,const float* vertices,const char* tex){
    glColor3f(float(r),float(g),float(b));
    enableTex(tex);
    const float ar=rotate*float(M_PI)/-180.0f;
    const float tc[8]={0,1, 1,1, 1,0, 0,0};
    glBegin(GL_QUADS);
    for (int i=0;i<4;++i){
        float px=vertices[i*2],py=vertices[i*2+1];
        rotatePoint(px,py,0,0,ar);
        if(tex)glTexCoord2f(tc[i*2],tc[i*2+1]);
        glVertex2f(x+px*local_size,y+py*local_size);
    }
    glEnd();
    if(tex)glDisable(GL_TEXTURE_2D);
}
// круг
void circle(float scale,float cx,float cy,double r,double g,double b,float radius,float in_radius,float rotate,int slices,int loops,const char* tex){
    // уже было
    glColor3f(float(r),float(g),float(b));
    enableTex(tex);
    // как я понял мы делаем круг в отдельной матрице(квадрика), а потом прибавляем к основной
    glPushMatrix();
    glTranslatef(cx,cy,0);
    glRotatef(-rotate,0,0,1);
    glScalef(scale,scale,1);
    static GLUquadric* q=nullptr;
    if (!q){
        q=gluNewQuadric();
        gluQuadricTexture(q,GL_TRUE);
        gluQuadricDrawStyle(q,GLU_FILL);
    }
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glScalef(1,-1,1);
    gluDisk(q,in_radius,radius,slices,loops);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    if(tex)glDisable(GL_TEXTURE_2D);
}
// рисовка текста
void draw_text(const char* text,float x,float y,void* font,float r,float g,float b,float a){
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    // задаём цвет
    glColor4f(r,g,b,a);
    // задаём позицию
    glRasterPos2f(x,y);
    // рисуем текст по символам
    for(const char* c=text;*c;++c){
        glutBitmapCharacter(font,*c);
    }
}

//              класс для рисовки псевдо 3д существ(как в думе например)
// переменные класса
pseudo_3d_entity::pseudo_3d_entity(float x,float y,float z,float g_angle,float v_angle,vector<const char*> textures,int v_angles,float* vertices)
    :x(x),y(y),z(z),g_angle(g_angle),v_angle(v_angle),textures(std::move(textures)),v_angles(v_angles),vertices(vertices){}

// проверяем есть ли на экране
bool pseudo_3d_entity::isVisible(float cam_x, float cam_y, float cam_z) const{
    const float dx=x-cam_x,dy=y-cam_y,dz=z-cam_z;
    const float fx=camera.ctr_x-camera.eye_x;
    const float fy=camera.ctr_y-camera.eye_y;
    const float fz=camera.ctr_z-camera.eye_z;

    const float depth=dx*fx+dy*fy+dz*fz;

    if(depth+radius<camera.znear)return false;
    if(depth-radius>camera.zfar)return false;

    const float dist=sqrtf(dx*dx+dy*dy+dz*dz);
    if(dist<1e-4f)return true;

    const float aspect=(window_h>0)?float(window_w)/float(window_h):1.0f;
    const float half_v=camera.fov*0.5f*float(M_PI)/180.0f;
    const float half_h=atanf(tanf(half_v)*aspect);
    const float half_diag=sqrtf(half_h*half_h+half_v*half_v);
    const float slack=asinf(fminf(1.0f,radius/dist));

    return (depth/dist)>=cosf(half_diag+slack);
}

// вычисляем какую текстуру поставить
int pseudo_3d_entity::getTextureIndex(float cam_h,float cam_v)const{
    if(fabsf(cam_h-cachedCamH)<0.5f&&fabsf(cam_v-cachedCamV)<0.5f)return cachedTexIdx;
    cachedCamH=cam_h;
    cachedCamV=cam_v;
    if(textures.empty()){
        cachedTexIdx=-1;
        return -1;
    }
    const int total=int(textures.size());
    const int h_count=total/v_angles;
    if(h_count<=0){
        cachedTexIdx=-1;
        return -1;
    }
    const float ch=cam_h*float(M_PI)/180.0f;
    const float cv=cam_v*float(M_PI)/180.0f;
    const float cos_cv=cosf(cv);
    const float dir_x=cos_cv*sinf(ch);
    const float dir_y=sinf(cv);
    const float dir_z=cos_cv*cosf(ch);

    const float ga=g_angle*float(M_PI)/180.0f;
    const float va=v_angle*float(M_PI)/180.0f;

    const float cos_ga=cosf(-ga),sin_ga=sinf(-ga);
    const float lx=dir_x*cos_ga+dir_z*sin_ga;
    const float ly=dir_y;
    const float lz=-dir_x*sin_ga+dir_z*cos_ga;

    const float cos_va=cosf(va),sin_va=sinf(va);
    const float fx=lx;
    const float fy=ly*cos_va+lz*sin_va;
    const float fz=-ly*sin_va+lz*cos_va;

    const float local_v=atan2f(fy,sqrtf(fx*fx+fz*fz))*180.0f/float(M_PI);
    const float local_h=(fabsf(local_v)>88.0f)?0.0f:atan2f(fx,fz)*180.0f/float(M_PI);

    const float v_rel=fmaxf(0.0f,fminf(180.0f,local_v+90.0f));
    const int v_index=int(fminf(v_rel/(180.0f/v_angles),float(v_angles-1)));

    const float step_h=360.0f/h_count;
    const int h_index=int((fmodf(local_h+360.0f,360.0f)+step_h*0.5f)/step_h)%h_count;

    cachedTexIdx=v_index*h_count+h_index;
    if(cachedTexIdx>=total)cachedTexIdx=total-1;
    return cachedTexIdx;
}
// рисуем сущность
void pseudo_3d_entity::draw(float cam_h,float cam_x,float cam_y,float cam_z)const{
    if(!isVisible(cam_x,cam_y,cam_z))return;

    const float dx=cam_x-x,dy=cam_y-y,dz=cam_z-z;
    const float dist=sqrtf(dx*dx+dy*dy+dz*dz);

    const float pitch=atan2f(dy,sqrtf(dx*dx+dz*dz))*180.0f/float(M_PI);

    const int tidx=getTextureIndex(cam_h,pitch);
    const char* tex=(tidx>=0)?textures[tidx]:nullptr;

    const float fx=(dist>1e-4f)?dx/dist:0.0f;
    const float fy=(dist>1e-4f)?dy/dist:1.0f;
    const float fz=(dist>1e-4f)?dz/dist:0.0f;

    float wx=0,wy=1,wz=0;
    if(fabsf(fy)>0.999f){wx=0;wy=0;wz=1;}
    float rx=wy*fz-wz*fy,ry=wz*fx-wx*fz,rz=wx*fy-wy*fx;
    const float rlen=sqrtf(rx*rx+ry*ry+rz*rz);
    if(rlen>1e-4f){rx/=rlen;ry/=rlen;rz/=rlen;}

    const float ux=fy*rz-fz*ry,uy=fz*rx-fx*rz,uz=fx*ry-fy*rx;

    const float mat[16]={
        rx, ry, rz, 0,
        ux, uy, uz, 0,
        fx, fy, fz, 0,
         0,  0,  0, 1
    };

    const float ga=g_angle*float(M_PI)/180.0f;
    const float va=v_angle*float(M_PI)/180.0f;

    float eu_x=-sinf(ga)*sinf(va);
    float eu_y=-cosf(va);
    float eu_z=-cosf(ga)*sinf(va);

    float dot=eu_x*fx+eu_y*fy+eu_z*fz;
    float pu_x=eu_x-dot*fx,pu_y=eu_y-dot*fy,pu_z=eu_z-dot*fz;
    float plen=sqrtf(pu_x*pu_x+pu_y*pu_y+pu_z*pu_z);

    if(plen<0.01f){
        const float ef_x=cosf(va)*sinf(ga);
        const float ef_y=-sinf(va);
        const float ef_z=cosf(va)*cosf(ga);
        const float d2=ef_x*fx+ef_y*fy+ef_z*fz;
        pu_x=ef_x-d2*fx;pu_y=ef_y-d2*fy;pu_z=ef_z-d2*fz;
    }

    const float roll=atan2f(-(pu_x*rx+pu_y*ry+pu_z*rz),pu_x*ux+pu_y*uy+pu_z*uz)*180.0f/float(M_PI);

    const bool mirror=(tidx==0);

    glPushMatrix();
    glTranslatef(x,y,z);
    glMultMatrixf(mat);
    glRotatef(roll+180.0f,0,0,1);
    square(1.0f,0,0,1,1,1,mirror?-180.0f:0.0f,vertices,tex);
    glPopMatrix();
}

// вызывай вместо ручного перебора сущностей
void drawEntities(vector<pseudo_3d_entity>& entities,float cam_h,float cam_x,float cam_y,float cam_z){
    const int n=int(entities.size());
    // массив для хранения результатов параллельного просчёта видимости
    vector<bool> visible(n);

    // параллельно считаем видимость каждой сущности через публичные геттеры
    // цифра 4 значит что потоки берут задачи по 4 штуки, а не по одной - так меньше накладных расходов
    #pragma omp parallel for schedule(dynamic,4)
    for(int i=0;i<n;++i){
        // используем геттеры т.к. поля x,y,z приватные
        const float ex=entities[i].getX();
        const float ey=entities[i].getY();
        const float ez=entities[i].getZ();
        const float dx=ex-cam_x,dy=ey-cam_y,dz=ez-cam_z;
        const float fx=camera.ctr_x-camera.eye_x;
        const float fy=camera.ctr_y-camera.eye_y;
        const float fz=camera.ctr_z-camera.eye_z;
        const float depth=dx*fx+dy*fy+dz*fz;
        const float dist=sqrtf(dx*dx+dy*dy+dz*dz);
        // простая проверка по глубине и расстоянию без радиуса т.к. это прикидка
        visible[i]=(depth>camera.znear-1.0f)&&(dist<camera.zfar);
    }

    // рисуем в главном потоке т.к. opengl однопоточный
    // draw внутри сам проверяет isVisible точнее, нам нужна параллельная прикидка только чтобы отсечь явно невидимых
    for(int i=0;i<n;++i)
        if(visible[i]) entities[i].draw(cam_h,cam_x,cam_y,cam_z);
}
//              opengl
// настройка изменения размеров в 3д режиме
void changeSize3D(int w,int h){
    // проверка чтобы избежать деления на 0
    if(h==0)h=1;
    // задаём область вывода от координат 0,0 до координат w,h 
    glViewport(0,0,w,h);
    // переключение матрицы в проекцию(хз что это значит)
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // настройка перспективы
    // fov | соотношение сторон / ближняя плоскость где не отображаем / дальняя плоскость где не отображаем 
    gluPerspective(camera.fov,float(w)/float(h),camera.znear,camera.zfar);
    // переключение матрицы обратно в модельно-видовую(хз что это значит) 
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    // настройка камеры
    gluLookAt(camera.eye_x,camera.eye_y,camera.eye_z,
              camera.ctr_x,camera.ctr_y,camera.ctr_z,
              camera.up_x,camera.up_y,camera.up_z);
}
// настройка изменения размеров в 2д режиме
void changeSize2D(int w,int h){
    // уже было
    if(h==0)h=1;
    glViewport(0,0,w,h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // соотношение сторон
    const float ratio=float(w)/float(h);
    // установка 2д проекции чтобы всегда была одна и таже система координат
    if(w<=h)glOrtho(-1,1,-1/ratio,1/ratio,1,-1);
    else glOrtho(-ratio,ratio,-1,1,1,-1);
    // уже было
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    // выводим размеры окна в переменные, чтобы разработчик игры их мог использовать
    window_w=w;
    window_h=h;
}

void setup_display(int* argc,char** argv,float r,float g,float b,float a,const char* name,int w,int h){
    // инициализация аудио(будет потом)
    init_audio();
    // инициализация glut
    glutInit(argc,argv);
    // записываем в переменные размеры окна и монитора
    screen_w=glutGet(GLUT_SCREEN_WIDTH);
    screen_h=glutGet(GLUT_SCREEN_HEIGHT);
    window_w=w;
    window_h=h;
    auto cpus=hwinfo::getAllCPUs();
    cpu_name=cpus.empty() ? "Unknown" : cpus[0].modelName();
    hwinfo::Memory mem=hwinfo::Memory();
    ram_v=std::to_string(mem.total_Bytes()/(1024*1024))+" MB";
    auto gpus=hwinfo::getAllGPUs();
    gpu_name=gpus.empty()?"Unknown":gpus[0].name();
    // параметры буфера кадра
    // двойная буферизация / 4 канала / буфер глубины(хз что значит)
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    // позиция окна
    glutInitWindowPosition(screen_w/4,screen_h/8);
    // размеры окна
    glutInitWindowSize(w,h);
    // имя окна
    glutCreateWindow(name);
    // функция для изменения размеров по умолчанию
    glutReshapeFunc(changeSize2D);
    // цвет заднего фона
    glClearColor(r,g,b,a);
    // настройка глубины(хз что это)
    glEnable(GL_DEPTH_TEST);
    glClearDepth(1.0f);
    // возможность прозрачности
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}
// настройка камеры
void setup_camera(float fov,float eye_x,float eye_y,float eye_z,float pitch,float yaw){
    // задаём параметры камеры
    camera.fov=fov;
    camera.znear=0.1f;
    camera.zfar=1000.0f;
    camera.eye_x=eye_x; 
    camera.eye_y=eye_y;
    camera.eye_z=eye_z;
    camera.up_x=0;
    camera.up_y=1;
    camera.up_z=0;
    // вычисляем точку взгляда
    lookAtForward(eye_x,eye_y,eye_z,pitch,yaw,camera.ctr_x,camera.ctr_y,camera.ctr_z);
    // настройка матрицы на проекцию
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // устанавливаем перспективу
    const float aspect=(window_h>0)? float(window_w)/float(window_h):1.0f;
    gluPerspective(fov,aspect,camera.znear,camera.zfar);
    // переключаемся на модельно-видовую матрицу и устанавливаем камеру
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(eye_x,eye_y,eye_z,camera.ctr_x,camera.ctr_y,camera.ctr_z,0,1,0);
    // устанавливаем позицию где мы слышим звуки
    ma_engine_listener_set_position(&audio_engine,0,eye_x,eye_y,eye_z);
}
// перемещение камеры
void move_camera(float eye_x,float eye_y,float eye_z,float pitch,float yaw){
    // обновляем параметры камеры
    camera.eye_x=eye_x;
    camera.eye_y=eye_y;
    camera.eye_z=eye_z;
    // считаем то, куда смотрит и не смотрит
    float dx, dy, dz;
    lookAtBackward(eye_x,eye_y,eye_z,pitch,yaw,camera.ctr_x,camera.ctr_y,camera.ctr_z,dx,dy,dz);
    // обновляем всё на экране
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(eye_x,eye_y,eye_z,camera.ctr_x,camera.ctr_y,camera.ctr_z,0,1,0);
    // обновляем звук
    ma_engine_listener_set_position (&audio_engine,0,eye_x, eye_y,eye_z);
    ma_engine_listener_set_direction(&audio_engine,0,dx,dy,dz);
}

//              3д(может быть потом ещё что-то будет)
// рисуем 3д объект, указывая вершины треугольников
void draw3DObject(float cx,float cy,float cz,double r,double g,double b,const char* tex,const vector<float>& vertices,const vector<int>& indices,const vector<float>& texcoords){
    // цвет
    glColor3f(float(r),float(g),float(b));
    // текстура
    if(tex){
        GLuint id=loadTextureFromFile(tex);
        if(id){
            glEnable(GL_TEXTURE_2D);
            bindTexture(id);
        }else{
            glDisable(GL_TEXTURE_2D);
        }
    }else{
        glDisable(GL_TEXTURE_2D);
    }
    // позицианирование
    glPushMatrix();
    glTranslatef(cx,cy,cz);
    // делаем объект
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3,GL_FLOAT,0,vertices.data());
    // делаем текстуру
    const bool hasTex=(!texcoords.empty()&&tex);
    if(hasTex){
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2,GL_FLOAT,0,texcoords.data());
    }
    // рендерим
    glDrawElements(GL_TRIANGLES,int(indices.size()),GL_UNSIGNED_INT,indices.data());
    // очищаем память
    glDisableClientState(GL_VERTEX_ARRAY);
    if(hasTex)glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glPopMatrix();
    if(tex)glDisable(GL_TEXTURE_2D);
}

//              включение/выключение 3д т.к. опенжиэль не может рисовать одновременно и так и так
// переключаем матрицу на 2д
void begin_2d(int w,int h){
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0,w,0,h,-1,1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
}
// переключаем матрицу на 3д(невероятно)
void end_2d(){
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

//              аудио
// инициализация аудио
void init_audio(){
    if(ma_engine_init(nullptr,&audio_engine)!=MA_SUCCESS){
        cerr<<"Failed to init audio engine\n";
        return;
    }
    cout<<"Audio: "<<ma_engine_get_channels(&audio_engine)<<" ch, "<<ma_engine_get_sample_rate(&audio_engine)<<" Hz"<<endl;
}
// проигрывание звука(просто)
void play_sound(const char* filename,float volume){
    // проверка на существование звука
    auto* sound=new ma_sound;
    if(ma_sound_init_from_file(&audio_engine,filename,MA_SOUND_FLAG_ASYNC,nullptr,nullptr,sound)!=MA_SUCCESS){
        delete sound;
        return;
    }
    // указываем что звук вне пространства
    ma_sound_set_spatialization_enabled(sound, MA_FALSE);
    // звук
    ma_sound_set_volume(sound, volume);
    // проигрывание
    ma_sound_start(sound);
    // когда проигралось удаляем из памяти
    ma_sound_set_end_callback(sound,[](void*,ma_sound* s){
        ma_sound_uninit(s);
        delete s;
    },nullptr);
}
void play_sound_loop(const char* filename,float volume){
    auto* sound=new ma_sound;
    if(ma_sound_init_from_file(&audio_engine,filename,0,nullptr,nullptr,sound)!=MA_SUCCESS){
        cerr<<"Cannot load looping sound: "<<filename<<endl;
        delete sound;
        return;
    }
    // отключаем пространственную обработку
    ma_sound_set_spatialization_enabled(sound,MA_FALSE);
    // устанавливаем громкость
    ma_sound_set_volume(sound,volume);
    // включаем зацикливание
    ma_sound_set_looping(sound,MA_TRUE);
    // проигрываем звук
    ma_sound_start(sound);
    // добавляем в вектор для последующей очистки
    loopingSounds.push_back(sound);
}
// проигрываем звук в 3д(сложно)
void play_sound_3d(const char* filename,float x,float y,float z,float volume){
    // проверка
    auto* sound=new ma_sound;
    if(ma_sound_init_from_file(&audio_engine,filename,MA_SOUND_FLAG_ASYNC,nullptr,nullptr,sound)!=MA_SUCCESS){
        delete sound;
        return;
    }
    // задаём где слушатель и звук и другие параметры
    ma_sound_set_positioning(sound,ma_positioning_absolute);
    ma_sound_set_position(sound,x,y,z);
    ma_sound_set_spatialization_enabled(sound,MA_TRUE);
    ma_sound_set_volume(sound,volume);
    // проигрываем звук
    ma_sound_start(sound);
    // когда проигралось удаляем из памяти
    ma_sound_set_end_callback(sound,[](void*,ma_sound* s){
        ma_sound_uninit(s);
        delete s;
    },nullptr);
}
// проигрываем звук в 3д бесконечно(всё то же самое, только бесконечно)
void play_sound_3d_loop(const char* filename,float x,float y,float z,float volume){
    auto* sound=new ma_sound;
    if(ma_sound_init_from_file(&audio_engine,filename,0,nullptr,nullptr,sound)!=MA_SUCCESS){
        cerr<<"Cannot load looping sound: "<<filename<<endl;
        delete sound;
        return;
    }
    ma_sound_set_positioning(sound,ma_positioning_absolute);
    ma_sound_set_position(sound,x,y,z);
    ma_sound_set_spatialization_enabled(sound,MA_TRUE);
    ma_sound_set_volume(sound,volume);
    ma_sound_set_looping(sound,MA_TRUE);
    ma_sound_start(sound);
    loopingSounds.push_back(sound);
}
// останавливаем все бесконечные звуки(тут из названий всё понятно)
void stop_all_looping_sounds(){
    for(auto* s:loopingSounds){
        ma_sound_stop(s);
        ma_sound_uninit(s);
        delete s;
    }
    loopingSounds.clear();
}
//              оверлей
// сколько заполнено оперативки/процессора
void draw_performance_hud(int win_w,int win_h){
    // переменные для рассчётов
    static long prev_cpu=0;
    static double cpu_pct=0.0;
    static long ram_kb=0;
    static int frame_cnt=0;
    static double fps=0.0;
    static auto prev_time=chrono::steady_clock::now();
    // счётчик кадров
    ++frame_cnt;
    auto now=chrono::steady_clock::now();
    double elapsed=chrono::duration<double>(now-prev_time).count();
    // обновление статистики
    if(elapsed>=1.0){
        fps=frame_cnt/elapsed;
        frame_cnt=0;

        // объявляем до параллельного блока т.к. внутри они должны быть видны обоим потокам
        long local_ram=0;
        long utime=0,stime=0;

        // параллельно читаем оба файла /proc т.к. это два независимых чтения с диска
        // sections значит что каждый кусок кода помеченный как section выполняется в отдельном потоке
        #pragma omp parallel sections
        {
            #pragma omp section
            {
                if(FILE* f=fopen("/proc/self/status","r")){
                    char line[128];
                    while(fgets(line,sizeof(line),f))
                        if(sscanf(line,"VmRSS: %ld",&local_ram)==1)break;
                    fclose(f);
                }
            }
            #pragma omp section
            {
                if(FILE* s=fopen("/proc/self/stat","r")){
                    fscanf(s,"%*d %*s %*c %*d %*d %*d %*d %*d "
                              "%*u %*u %*u %*u %*u %ld %ld",&utime,&stime);
                    fclose(s);
                }
            }
        }

        ram_kb=local_ram;
        long cur_cpu=utime+stime;
        cpu_pct=(cur_cpu-prev_cpu)/(double)sysconf(_SC_CLK_TCK)/elapsed*10.0;
        prev_cpu=cur_cpu;
        prev_time=now;
    }
    // вывод статистики в левом верхнем углу
    char buf[100];
    snprintf(buf,sizeof(buf),"FPS: %.0f  RAM: %ld MB  CPU: %.1f%%",fps,ram_kb / 1024,cpu_pct);
    begin_2d(win_w,win_h);
    draw_text(buf,10.0f,float(win_h)-20.0f,GLUT_BITMAP_HELVETICA_12,1.0f,1.0f,1.0f);
    snprintf(buf,sizeof(buf),"X: %.10f  Y: %.10f  Z: %.10f",camera.eye_x,camera.eye_y,camera.eye_z);
    draw_text(buf,10.0f,float(win_h)-32.0f,GLUT_BITMAP_HELVETICA_12,1.0f,1.0f,1.0f);
    snprintf(buf,sizeof(buf),"CPU: %s  RAM: %s  GPU: %s",cpu_name.c_str(),ram_v.c_str(),gpu_name.c_str());
    draw_text(buf,10.0f,float(win_h)-44.0f,GLUT_BITMAP_HELVETICA_12,1.0f,1.0f,1.0f);
    end_2d();
}