#include "../avoengine.h"
#include "../avoextension.h"

void displayWrapper() {
    // Очистка буферов (цвета и глубины)
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Переходим в 2D режим для отрисовки текста
    // Используем window_w/h, которые движок обновил при запуске
    begin_2d(window_w, window_h);

    // ВАЖНО: В 2D режиме координаты обычно идут от 0 до window_w.
    // Если твои функции в extension используют нормализованные координаты (-1..1), 
    // убедись, что begin_2d в avoengine настроен на glOrtho(-1, 1...). 
    // В твоем текущем avoengine.cpp стоит glOrtho(0, w, 0, h), 
    // поэтому заменим координаты на экранные (пиксельные):

    // Появляется по буквам (в центре экрана по X, чуть выше центра по Y)
    delay_text("test1",
               window_w / 2.0f - 50.0f, window_h / 2.0f + 20.0f, 
               GLUT_BITMAP_HELVETICA_18,
               1.0f, 1.0f, 1.0f, 1.0f, 128, true);

    // Плавно пропадает
    disappearing_text("test2",
                      window_w / 2.0f - 50.0f, window_h / 2.0f - 20.0f, 
                      GLUT_BITMAP_HELVETICA_18,
                      1.0f, 0.4f, 0.4f, 1.0f, 128, true);
    
    end_2d(); // Возвращаемся из 2D

    // HUD вызывается отдельно, так как он сам управляет своими матрицами
    draw_performance_hud(window_w, window_h);

    glutSwapBuffers();
}

int main(int argc, char** argv) {
    // Инициализация через ядро
    setup_display(&argc, argv, 0.1f, 0.1f, 0.1f, 1.0f, "Text Effects Demo", 800, 400);
    
    init_tick_system();

    glutDisplayFunc(displayWrapper);
    
    // В Idle добавляем обновление тиков для работы анимаций
    glutIdleFunc([]{ 
        glutPostRedisplay(); 
    });

    glutMainLoop();
    return 0;
}