### triangle()
```
triangle(float local_size, float x, float y, double r, double g, double b, float rotate, float* vertices, const char* texture_file = nullptr)
```

`local_size` - size

`x, y` - coordinate x, coordinate y

`r, g, b` - color(r,g,b)

`rotate` - angle/rotate

`vertices` - coordinates of dots

`texture_file` - texture, may not be specified

**Example:**
```
triangle(figure_size, center_x*2, center_y*2, 1.0f, 1.0f, 1.0f, global_angle, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f},"src/penza.png");
```

### square()
```
square(float local_size, float x, float y, double r, double g, double b, float rotate, float* vertices, const char* texture_file = nullptr)
```

`local_size` - size

`x, y` - coordinate x, coordinate y

`r, g, b` - color(r,g,b)

`rotate` - angle/rotate

`vertices` - coordinates of dots

`texture_file` - texture, may not be specified

**Example:**
```
square(figure_size, center_x, center_y, 1.0f, 1.0f, 1.0f, global_angle, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f}, "src/god_png.png");
```

### circle()
```
circle(float local_size, float x, float y, double r, double g, double b, float radius,float in_radius, float rotate, int slices, int loops, const char* texture_file = nullptr)
```

`local_size` - size

`x, y` - coordinate x, coordinate y

`r, g, b` - color(r,g,b)

`radius` - just radius

`in_radius` - inner radius

`rotate` - angle/rotate

`slices` - slices

`loops` - loops(idk whts ts)

`texture_file` - texture, also may not be specified

**Example:**
```
circle(figure_size, center_x*1.5, center_y*1.5, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, global_angle, 115, 1, "src/diskriminant.png");
```

## Display Functions

### displayWrapper()
what you do on screen

**Example:**
```c
void displayWrapper() {
    glClear(GL_COLOR_BUFFER_BIT);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    square(figure_size, center_x, center_y, 1.0f, 1.0f, 1.0f, global_angle, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f}, "src/god_png.png");
    circle(figure_size, center_x*1.5, center_y*1.5, 1.0f, 1.0f, 1.0f, 1.0f, 0.2f, global_angle, 115, 1, "src/diskriminant.png");
    triangle(figure_size, center_x*2, center_y*2, 1.0f, 1.0f, 1.0f, global_angle, (float[]){-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f},"src/penza.png");
    
    glutSwapBuffers();
}
```

### setup_display()
```
setup_display(int* argc, char** argv, float r, float g, float b, float a)
```

`argc, argv` - dont touch, dont touch

`r, g, b, a` - color(r,g,b,alpha channel)

**Example:**
```c
setup_display(&argc, argv, 1.0f, 1.0f, 1.0f, 1.0f);
```

## Main Function

**Example:**
```c
int main(int argc, char** argv) {
    setup_display(&argc, argv, 0.2f, 0.2f, 0.2f, 1.0f);
    
    glEnable(GL_TEXTURE_2D);
    
    glutDisplayFunc(displayWrapper);
    glutKeyboardFunc(keyboardDown);
    glutKeyboardUpFunc(keyboardUp);
    glutTimerFunc(0, timer, 0);
    glutMainLoop();
    
    for (auto& texture : textureCache) {
        glDeleteTextures(1, &texture.second);
    }
    
    return 0;
}
```

other so it's clear
