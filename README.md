### triangle()
```
triangle(float local_size, float x, float y, double r, double g, double b, float rotate, float* vertices)
```
`local_size` - size
`x, y` - coordinate x, coordinate y
`r, g, b` - color(r,g,b)
`rotate` - angle/rotate
`vertices` - coordinates of dots

**Example:**
```
triangle(figure_size, center_x, center_y, 0.0f, 0.0f, 1.0f, global_angle, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f});
```

### square()
```
square(float local_size, float x, float y, double r, double g, double b, float rotate, float* vertices)
```
**Parameters:**
`local_size` - size
`x, y` - coordinate x, coordinate y
`r, g, b` - color(r,g,b)
`rotate` - angle/rotate
`vertices` - coordinates of dots

**Example:**
```
square(figure_size, center_x, center_y, 1.0f, 0.0f, 0.0f, global_angle, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f});
```

### circle()
```
circle(float local_size, float x, float y, double r, double g, double b, float radius, float in_radius, float rotate, int slices, int loops)
```
**Parameters:**
`local_size` - size
`x, y` - coordinate x, coordinate y
`r, g, b` - color(r,g,b)
`radius` - just radius
`in_radius` - inner radius
`rotate` - angle/rotate
`slices` - slices
`loops` - loops(idk whts ts)

**Example:**
```
circle(figure_size, center_x, center_y, 0.0f, 1.0f, 0.0f, 1.0f, 0.2f, global_angle, 15, 1);
```

## Display Functions

### displayWrapper()
what you do on screen

**Example:**
```c
void displayWrapper() {
    glClear(GL_COLOR_BUFFER_BIT);
    square(figure_size, center_x, center_y, 1.0f, 0.0f, 0.0f, global_angle, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f});
    circle(figure_size, center_x, center_y, 0.0f, 1.0f, 0.0f, 1.0f, 0.2f, global_angle, 15, 1);
    triangle(figure_size, center_x, center_y, 0.0f, 0.0f, 1.0f, global_angle, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f});
    
    glutSwapBuffers();
}
```

### setup_display()
```
setup_display(int* argc, char** argv, float r, float g, float b, float a)
```
**Parameters:**
- `argc, argv` - dont touch, dont touch
- `r, g, b, a` - color(r,g,b,alpha channel)

**Example:**
```c
setup_display(&argc, argv, 1.0f, 1.0f, 1.0f, 1.0f);
```

## Main Function

**Example:**
```c
int main(int argc, char** argv) {
    setup_display(&argc, argv, 1.0f, 1.0f, 1.0f, 1.0f);
    glutDisplayFunc(displayWrapper);
    glutKeyboardFunc(keyboardDown);
    glutKeyboardUpFunc(keyboardUp);
    glutTimerFunc(0, timer, 0);
    glutMainLoop();
    return 0;
}
```
