#ifndef PRIMITIVES_2D
#define PRIMITIVES_2D

extern GLuint current2DShader;

extern GLint loc_tex_2d;
extern GLint loc_u_projection_2d;
extern GLint loc_u_modelView_2d;

void init2DShader();

void draw_line_2d(float x, float y, float x1, float y1, float x2, float y2, float r, float g, float b, float a, float thickness);
void square(float local_size, float x, float y, double r, double g, double b,
            float rotate, const float* vertices, const char* tex=nullptr, float alpha = 1.0f);

void draw_text(const char* text, float x, float y, const char* fontPath, int fontSize, float r, float g, float b, float a=1);


#endif