#ifndef WARP
#define WARP

class WarpPlane {
public:
    float originX, originY, originZ;
    float yaw, pitch, roll;
    float sizeU, sizeV;
    GLuint displacementTex;
    bool enabled;

    WarpPlane();
    void setDisplacementTexture(const char* filename);
    void setDisplacementFromData(int w, int h, const float* data);
    void enable();
    void disable();
};

extern WarpPlane* activeWarpPlane;
void set_active_warp_plane(WarpPlane* wp);

#endif