# triangle(float local_size, float x, float y, double r, double g, double b, float rotate, float* vertices)
size, coordinate x, coordinate y, color(r,g,b), angle/rotate, coordinates of dots

example:

# triangle(figure_size, center_x, center_y, 0.0f, 0.0f, 1.0f, global_angle, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f});





# square(float local_size, float x, float y, double r, double g, double b, float rotate, float* vertices)
size, coordinate x, coordinate y, color(r,g,b), angle/rotate, coordinates of dots

example:

# square(figure_size, center_x, center_y, 1.0f, 0.0f, 0.0f, global_angle, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f});

# circle(float local_size, float x, float y, double r, double g, double b, float radius,float in_radius, float rotate, int slices, int loops)
size, coordinate x, coordinate y, color(r,g,b),just radius, inner radius, angle/rotate, slices, loops(idk whts ts)

example:

# circle(figure_size, center_x, center_y, 0.0f, 1.0f, 0.0f, 1.0f, 0.2f, global_angle, 15, 1);

# displayWrapper()

what you do on screen

example:

#  void displayWrapper() {
    glClear(GL_COLOR_BUFFER_BIT);
    square(figure_size, center_x, center_y, 1.0f, 0.0f, 0.0f, global_angle, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f});
    circle(figure_size, center_x, center_y, 0.0f, 1.0f, 0.0f, 1.0f, 0.2f, global_angle, 15, 1);
    triangle(figure_size, center_x, center_y, 0.0f, 0.0f, 1.0f, global_angle, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f});
    
    glutSwapBuffers();
 }

 # setup_display(int* argc, char** argv, float r, float g, float b, float a)

dont touch, dont touch,color(r,g,b,alpha channel)

# example {
    setup_display(&argc, argv, 1.0f, 1.0f, 1.0f, 1.0f);
}

other so it's clear

# main example {
    int main(int argc, char** argv) {
      setup_display(&argc, argv, 1.0f, 1.0f, 1.0f, 1.0f);
      glutDisplayFunc(displayWrapper);
      glutKeyboardFunc(keyboardDown);
      glutKeyboardUpFunc(keyboardUp);
      glutTimerFunc(0, timer, 0);
      glutMainLoop();
      return 0;
    }
}
