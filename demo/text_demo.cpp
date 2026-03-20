#include "../avoengine.h"
#include "../avoextension.h"

void displayWrapper() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // появляется по буквам за 128 тиков
    delay_text("test1",
               -0.5f, 0.1f, GLUT_BITMAP_HELVETICA_18,
               1.0f, 1.0f, 1.0f, 1.0f, 128, true);

    // плавно пропадает за 128 тиков
    disappearing_text("test2",
                      -0.5f, -0.1f, GLUT_BITMAP_HELVETICA_18,
                      1.0f, 0.4f, 0.4f, 1.0f, 128,true);
    draw_performance_hud(window_w,window_h);
    glutSwapBuffers();
}

int main(int argc, char** argv) {
    setup_display(&argc, argv, 0.1f, 0.1f, 0.1f, 1.0f, "Text Effects Demo", 800, 400);
    glutDisplayFunc(displayWrapper);
    glutIdleFunc([]{ glutPostRedisplay(); });
    init_tick_system();
    glutMainLoop();
    return 0;
}