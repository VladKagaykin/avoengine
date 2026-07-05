#include"avoengine.h"
#include"2d_primitives.h"
#include "shaders.h"
#include "textures.h"

#include <mutex>
#include <fstream>

GLuint current2DShader = 0;

static const char* simple2DVertexShader = R"(
#version 120
attribute vec4 aVertex;
attribute vec4 aColor;
attribute vec2 aTexCoord;

uniform mat4 u_projection;
uniform mat4 u_modelView;

varying vec4 vColor;
varying vec2 vTexCoord;

void main() {
    vColor = aColor;
    vTexCoord = aTexCoord;
    gl_Position = u_projection * u_modelView * aVertex;
}
)";

static const char* simple2DFragmentShader = R"(
#version 120
varying vec4 vColor;
varying vec2 vTexCoord;
uniform sampler2D tex;
void main() {
    gl_FragColor = texture2D(tex, vTexCoord) * vColor;
}
)";

GLint loc_tex_2d = -1;
GLint loc_u_projection_2d = -1;
GLint loc_u_modelView_2d = -1;

void init2DShader() {
    if (current2DShader) return;
    current2DShader = createShaderProgram(simple2DVertexShader, simple2DFragmentShader);
    loc_tex_2d = glGetUniformLocation(current2DShader, "tex");
    loc_u_projection_2d = glGetUniformLocation(current2DShader, "u_projection");
    loc_u_modelView_2d = glGetUniformLocation(current2DShader, "u_modelView");
}

//              простые 2д фигуры
// отрезок
void draw_line_2d(float x, float y, float x1, float y1, float x2, float y2, float r, float g, float b, float a, float thickness) {
    DrawCommand cmd;
    cmd.type = CMD_LINE_2D;
    cmd.verts[0] = x + x1; cmd.verts[1] = y + y1;
    cmd.verts[2] = x + x2; cmd.verts[3] = y + y2;
    cmd.r = r; cmd.g = g; cmd.b = b; cmd.a = a;
    cmd.scale = thickness;
    std::lock_guard<std::mutex> lock(drawQueueMutex);
    drawQueue.push_back(cmd);
}
// квадрат
void square(float local_size, float x, float y, double r, double g, double b,
            float rotate, const float* vertices, const char* tex, float alpha){
    DrawCommand cmd;
    cmd.type = CMD_SQUARE;
    cmd.scale = local_size;
    cmd.cx = x;
    cmd.cy = y;
    cmd.r = (float)r;
    cmd.g = (float)g;
    cmd.b = (float)b;
    cmd.a = alpha;
    cmd.rotate = rotate;
    cmd.vertCount = 4;
    for (int i = 0; i < 8; i++) cmd.verts[i] = vertices[i];
    if (tex) cmd.tex = tex;
    cmd.shaderID = current2DShader;
    drawQueue.push_back(cmd);
}
// рисовка текста
#define STB_TRUETYPE_IMPLEMENTATION
#include "src/stb_truetype.h"

void draw_text(const char* text, float x, float y, const char* fontPath, int fontSize,
               float r, float g, float b, float a) {
    static std::unordered_map<std::string, std::pair<std::vector<unsigned char>, stbtt_fontinfo>> fontCache;

    auto fontIt = fontCache.find(fontPath);
    if (fontIt == fontCache.end()) {
        std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
        if (!file) return;
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<unsigned char> buffer(size);
        file.read((char*)buffer.data(), size);
        stbtt_fontinfo info;
        if (!stbtt_InitFont(&info, buffer.data(), 0)) return;
        fontCache[fontPath] = {std::move(buffer), info};
        fontIt = fontCache.find(fontPath);
    }
    stbtt_fontinfo& font = fontIt->second.second;

    float scale = stbtt_ScaleForPixelHeight(&font, (float)fontSize);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    float baseline = y;  

    float internalScale = stbtt_ScaleForPixelHeight(&font, (float)fontSize * Engine_settings.TEXT_SAMPLE);
    float sx = scale / internalScale;     

    float curX = x;
    for (const char* c = text; *c; ++c) {
        int glyphIdx = stbtt_FindGlyphIndex(&font, *c);
        int advance, lsb;
        stbtt_GetGlyphHMetrics(&font, glyphIdx, &advance, &lsb);

        int ix0, iy0, ix1, iy1;
        stbtt_GetGlyphBitmapBox(&font, glyphIdx, internalScale, internalScale, &ix0, &iy0, &ix1, &iy1);
        int gw = ix1 - ix0;
        int gh = iy1 - iy0;
        if (gw <= 0 || gh <= 0) {
            curX += advance * scale;
            if (*(c + 1))
                curX += stbtt_GetGlyphKernAdvance(&font, glyphIdx,
                                    stbtt_FindGlyphIndex(&font, *(c + 1))) * scale;
            continue;
        }

        char key[256];
        snprintf(key, sizeof(key), "__glyph_%s_%d_%d", fontPath, (int)(*c), fontSize);
        GLuint glyphTex = 0;
        int texW = 0, texH = 0, offsetX = 0, offsetY = 0;

        {
            std::lock_guard<std::mutex> lock(textureCacheMutex);
            auto it = textureCache.find(key);
            if (it != textureCache.end()) {
                glyphTex = it->second;
            }
        }

        if (glyphTex == 0) {
            std::vector<unsigned char> glyphBitmap(gw * gh);
            stbtt_MakeGlyphBitmap(&font, glyphBitmap.data(), gw, gh, gw,
                                  internalScale, internalScale, glyphIdx);

            const int PADDING = Engine_settings.TEXT_SAMPLE * 2;
            texW = gw + 2 * PADDING;
            texH = gh + 2 * PADDING;
            offsetX = PADDING;
            offsetY = PADDING;

            std::vector<unsigned char> rgbaBitmap(texW * texH * 4, 0);
            for (int row = 0; row < gh; ++row) {
                for (int col = 0; col < gw; ++col) {
                    unsigned char val = glyphBitmap[row * gw + col];
                    int idx = ((offsetY + row) * texW + (offsetX + col)) * 4;
                    rgbaBitmap[idx + 0] = 255;
                    rgbaBitmap[idx + 1] = 255;
                    rgbaBitmap[idx + 2] = 255;
                    rgbaBitmap[idx + 3] = val;
                }
            }

            glGenTextures(1, &glyphTex);
            glBindTexture(GL_TEXTURE_2D, glyphTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaBitmap.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glBindTexture(GL_TEXTURE_2D, 0);

            {
                std::lock_guard<std::mutex> lock(textureCacheMutex);
                textureCache[key] = glyphTex;
            }
        } else {
            const int PADDING = Engine_settings.TEXT_SAMPLE * 2;
            texW = gw + 2 * PADDING;
            texH = gh + 2 * PADDING;
            offsetX = PADDING;
            offsetY = PADDING;
        }

        float left   = curX + ix0 * sx;
        float right  = curX + ix1 * sx;
        float top    = baseline - iy0 * sx;  
        float bottom = baseline - iy1 * sx;   

        float qx = left - offsetX * sx;
        float qy = bottom - (texH - (offsetY + gh)) * sx;  
        float qw = texW * sx;
        float qh = texH * sx;

        float tx0 = (float)offsetX / texW;
        float tx1 = (float)(offsetX + gw) / texW;
        float v_top_stb    = (float)offsetY / texH;       
        float v_bottom_stb = (float)(offsetY + gh) / texH;   
       
        float v_top_gl    = 1.0f - v_top_stb;
        float v_bottom_gl = 1.0f - v_bottom_stb;

        DrawCommand cmd;
        cmd.type = CMD_SQUARE;
        cmd.scale = 1.0f;
        cmd.cx = 0; cmd.cy = 0;
        cmd.r = r; cmd.g = g; cmd.b = b; cmd.a = a;
        cmd.rotate = 0;
        cmd.vertCount = 4;

        cmd.verts[0] = qx;          cmd.verts[1] = qy;
        cmd.verts[2] = qx + qw;     cmd.verts[3] = qy;
        cmd.verts[4] = qx + qw;     cmd.verts[5] = qy + qh;
        cmd.verts[6] = qx;          cmd.verts[7] = qy + qh;

        cmd.obj_texcoords = {
            tx0, v_bottom_gl,
            tx1, v_bottom_gl,
            tx1, v_top_gl,
            tx0, v_top_gl
        };

        cmd.shaderID = current2DShader;
        cmd.tex = key;

        {
            std::lock_guard<std::mutex> lock(drawQueueMutex);
            drawQueue.push_back(cmd);
        }

        curX += advance * scale;
        if (*(c + 1))
            curX += stbtt_GetGlyphKernAdvance(&font, glyphIdx,
                                stbtt_FindGlyphIndex(&font, *(c + 1))) * scale;
    }
}

//              оверлей
// сколько заполнено оперативки/процессора
void draw_performance_hud(int win_w, int win_h, const char* font_path){
    static int frame_cnt = 0;
    static double fps = 0.0;
    static auto prev_time = std::chrono::steady_clock::now();
    static std::vector<float> cpu_per_core;
    static long ram_usage_mb = 0;
    static float gpu_usage = -1.0f;

    ++frame_cnt;
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - prev_time).count();

    if (elapsed >= 1.0) {
        fps = frame_cnt / elapsed;
        frame_cnt = 0;

        #ifdef _WIN32
                cpu_per_core = getProcessCPUUsage_Win();
                ram_usage_mb = getProcessRAMUsage_Win();
                gpu_usage = getGPUUsage_Win();
        #else
                cpu_per_core = getProcessCPUUsage_Linux();
                ram_usage_mb = getProcessRAMUsage_Linux();
                gpu_usage = getGPUUsage_Linux();
        #endif
        prev_time = now;
    }

    char buf[256];
    snprintf(buf, sizeof(buf), "FPS: %.0f  RAM: %ld MB  GPU: ", fps, ram_usage_mb);
    if (gpu_usage >= 0.0f) {
        char gpu_str[32];
        snprintf(gpu_str, sizeof(gpu_str), "%.1f%%", gpu_usage);
        strcat(buf, gpu_str);
    } else {
        strcat(buf, "N/A");
    }
    draw_text(buf, 10.0f, float(win_h) - 20.0f, font_path, 12, 1.0f, 1.0f, 1.0f);

    std::string cpu_line;
    if (!cpu_per_core.empty()) {
        cpu_line = "CPU:";
        for (size_t i = 0; i < cpu_per_core.size(); ++i) {
            char core_buf[16];
            snprintf(core_buf, sizeof(core_buf), "%.1f", cpu_per_core[i]);
            cpu_line += " " + std::to_string(i) + ":" + std::string(core_buf) + "%";
        }
    } else {
        cpu_line = "CPU: N/A";
    }
    draw_text(cpu_line.c_str(), 10.0f, float(win_h) - 32.0f, font_path, 12, 1.0f, 1.0f, 1.0f);

    snprintf(buf, sizeof(buf), "X: %.10f  Y: %.10f  Z: %.10f P: %.10f  Y: %.10f  R: %.10f",
             camera.eye_x, camera.eye_y, camera.eye_z, camera.pitch, camera.yaw, camera.roll);
    draw_text(buf, 10.0f, float(win_h) - 44.0f, font_path, 12, 1.0f, 1.0f, 1.0f);

    snprintf(buf, sizeof(buf), "CPU: %s  RAM: %s  GPU: %s",
             cpu_name.c_str(), ram_v.c_str(), gpu_name.c_str());
    draw_text(buf, 10.0f, float(win_h) - 56.0f, font_path, 12, 1.0f, 1.0f, 1.0f);
}