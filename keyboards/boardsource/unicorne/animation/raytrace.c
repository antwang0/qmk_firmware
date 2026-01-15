#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h> // For usleep

#define SCREEN_WIDTH 32
#define SCREEN_HEIGHT 128
#define NUM_FRAMES 32
#define NUM_SURFACES 2
#define U_DELTA 0.001f
#define V_DELTA 0.001f
#define PI 3.14159265359f
#define PHI_DELTA (PI / NUM_FRAMES)
#define K1 4.0f
#define K2 6.0f
#define WIDTH_SCALE 1.3f

typedef struct { float x, y, z; } vec3;
typedef struct { float x, y; } vec2;

vec3 L = {0, -2, 1.2f};

//========================================
// Basic math
//========================================

float dot(vec3 a, vec3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

float magnitude2(float a, float b) {
    return sqrtf(a*a + b*b);
}

vec3 normalize(vec3 v) {
    float n = sqrtf(dot(v,v));
    vec3 r = {v.x/n, v.y/n, v.z/n};
    return r;
}

float lightCurve(float x) {
    if (x > 0) return 0.3f + 0.7f*x;
    else return 0.1f - 0.4f*x;
}

int get2DIndex(int x, int y) {
    return y * SCREEN_WIDTH + x;
}

//========================================
// Dithering
//========================================

int dither(float l, int x, int y) {
    if (l <= 0.0f) return 0;
    if (l <= 0.11f) return (x%3==0 && y%3==0);
    if (l <= 0.25f) return (x%2==0 && y%2==0);
    if (l <= 0.33f) return ((x+y)%3==0);
    if (l <= 0.5f) return ((x+y)%2==0);
    if (l <= 0.66f) return ((x+y)%3!=0);
    if (l <= 0.75f) return (x%2==0 || y%2==0);
    if (l <= 0.88f) return (x%3!=0 || y%3!=0);
    return 1;
}

//========================================
// Geometry (example: default UV + normal)
//========================================

vec3 defaultUV(float u, float v, int i) {
    float stripWidth = PI / NUM_SURFACES;
    float alpha = 2 * stripWidth * i;
    float r = WIDTH_SCALE * (cosf(PI*v)+1)/2.0f;
    float theta = 2.0f*PI*v + stripWidth*u/2.0f + alpha;
    vec3 out = { r*cosf(theta), r*sinf(theta), v };
    return out;
}

vec3 defaultNormal(vec3 coords, int i) {
    if (coords.x==0 && coords.y==0) {
        vec3 z = {0,0,0}; return z;
    }
    float m = magnitude2(coords.x, coords.y) * (WIDTH_SCALE * PI/2.0f * sinf(PI*coords.z));
    vec3 n = {coords.x, coords.y, m};
    return normalize(n);
}

const float r0 = 1;
const float r1 = .5;

vec3 torusUV(float u, float v, int i) {
    const float r     = r0 + r1 * cosf(PI * v);
    const float theta = PI * u;
    const float y     = r * cosf(theta);
    const float z     = r * sin(theta);
    const float x     = r1 * sin(PI * v);

    vec3 uv = {x, y, z};
    return uv;
}

vec3 torusNormal(vec3 coords, int i) {
    const float y     = coords.y;
    const float z     = coords.z;
    const float theta = atan2(z, y);

    // return normalize( { coords[0] - r0 * cos( theta ), coords[1] - r0 * sin( theta ), coords[2] } );
    vec3 n = {coords.x, (y - r0 * cosf(theta)), (z - sinf(theta))};
    return normalize(n);
}

//========================================
// Frame generation
//========================================

void generateFrame(float phi, vec3 (*uv)(float,float,int), vec3 (*normal)(vec3,int), float* frameData) {
    float zBuffer[SCREEN_WIDTH*SCREEN_HEIGHT];
    for(int i=0;i<SCREEN_WIDTH*SCREEN_HEIGHT;i++) { frameData[i]=0; zBuffer[i]=1000; }

    float cosPhi = cosf(phi);
    float sinPhi = sinf(phi);

    for(int iSurf=0;iSurf<NUM_SURFACES;iSurf++){
        for(float v=-1;v<1;v+=V_DELTA){
            for(float u=-1;u<1;u+=U_DELTA){
                vec3 coordsObj = uv(u,v,iSurf);

                vec3 coordsRot;
                coordsRot.x = cosPhi*coordsObj.x - sinPhi*coordsObj.y;
                coordsRot.y = sinPhi*coordsObj.x + cosPhi*coordsObj.y;
                coordsRot.z = coordsObj.z;

                vec3 coordsTrans = { coordsRot.x+K2, coordsRot.y, coordsRot.z };
                float perspectiveScale = K1 / coordsTrans.x;
                vec2 screenCoords = { coordsTrans.y*perspectiveScale, coordsTrans.z*perspectiveScale };

                int px = (int)((screenCoords.x/2.0f + 0.5f)*SCREEN_WIDTH);
                int py = (int)((screenCoords.y/2.0f + 0.5f)*SCREEN_HEIGHT);

                if(px<0||px>=SCREEN_WIDTH||py<0||py>=SCREEN_HEIGHT) continue;

                int idx = get2DIndex(px,py);
                if(zBuffer[idx]<coordsTrans.x) continue;
                zBuffer[idx] = coordsTrans.x;

                vec3 VL = {L.x-coordsTrans.x, L.y-coordsTrans.y, L.z-coordsTrans.z};
                VL = normalize(VL);

                vec3 N = normal(coordsObj,iSurf);
                vec3 NR = { cosPhi*N.x - sinPhi*N.y, sinPhi*N.x + cosPhi*N.y, N.z };
                float brightness = lightCurve(dot(VL,NR));
                if(brightness<0) brightness=0;
                if(brightness>1) brightness=1;

                frameData[idx] = brightness;
            }
        }
    }
}

//========================================
// Printing ASCII frame
//========================================

void clearScreen() {
    printf("\x1b[2J\x1b[H");
}

void printFrameASCII(float* frameData) {
    static char shades[] = " ,-~:;=!*$@#";
    char buffer[(SCREEN_WIDTH+1)*SCREEN_HEIGHT];
    memset(buffer, ' ', sizeof(buffer));

    int idx=0;
    int bw = sizeof(buffer)-1;

    for(int y=0;y<SCREEN_HEIGHT;y++){
        buffer[bw]='\n'; bw--;
        for(int x=0;x<SCREEN_WIDTH;x++){
            int s = (int)(frameData[idx]*(sizeof(shades)-1));
            buffer[bw] = shades[s];
            bw--; idx++;
        }
    }

    printf("%s", buffer);
}

void printFrameOLED(char* frameData) {
    char buffer[(SCREEN_WIDTH+1)*SCREEN_HEIGHT];
    memset(buffer, ' ', sizeof(buffer));

    int idx=0;
    int bw = sizeof(buffer)-1;

    for(int y=0;y<SCREEN_HEIGHT;y++){
        buffer[bw]='\n'; bw--;
        for(int x=0;x<SCREEN_WIDTH;x++){
            buffer[bw] = frameData[idx];
            bw--; idx++;
        }
    }

    printf("%s", buffer);
}
//========================================
// OLED printing
//========================================

void printOLEDHex(char* buffer) {
    // 4 horizontal blocks (like your C++ code)
    for(int xBlock=0;xBlock<8;xBlock++){
        for(int y=0;y<128;y++){ // assuming 128 vertical pixels
            int byte=0;
            for(int bit=0;bit<8;bit++){
                int px = xBlock*8 + bit;
                int py = y;
                if(px>=SCREEN_WIDTH || py>=SCREEN_HEIGHT) continue;
                int idx = get2DIndex(px,py);
                if(buffer[idx]=='#') byte |= (1<<bit);
            }
            if (xBlock == 7 && y == 127) {
                printf("0x%02X", byte);
            } else {
                printf("0x%02X, ", byte);
            }
        }
        printf("\n");
    }
}



void asciiOledDataToConsole(vec3 (*uv)(float,float,int), vec3 (*normal)(vec3,int)) {
    float phi = 0;
    float frame[SCREEN_WIDTH*SCREEN_HEIGHT];

    for(int f=0;f<NUM_FRAMES;f++){
        phi += PHI_DELTA;
        generateFrame(phi, uv, normal, frame);
        printFrameASCII(frame);
        usleep(16000);
    }
}

//------------------------------
// Print OLED frames as hex
//------------------------------

void convertFrameToOLEDChars(float* frameData, char* buffer) {
    for(int i=0;i<SCREEN_WIDTH*SCREEN_HEIGHT;i++) {
        buffer[i] = dither(frameData[i], i%SCREEN_WIDTH, i/SCREEN_WIDTH) ? '#' : ' ';
    }
}

void renderOledDataToConsole(vec3 (*uv)(float,float,int), vec3 (*normal)(vec3,int)) {
    float phi = 0;
    float frame[SCREEN_WIDTH*SCREEN_HEIGHT];
    char oledBuffer[SCREEN_WIDTH*SCREEN_HEIGHT];

    for(int f=0;f<NUM_FRAMES;f++){
        phi += PHI_DELTA;
        generateFrame(phi, uv, normal, frame);
        convertFrameToOLEDChars(frame, oledBuffer);

        printFrameOLED(oledBuffer);
        usleep(16000);
    }
}

void printOledDataToHexConsole(vec3 (*uv)(float,float,int), vec3 (*normal)(vec3,int)) {
    float phi=0;
    float frame[SCREEN_WIDTH*SCREEN_HEIGHT];
    char oledBuffer[SCREEN_WIDTH*SCREEN_HEIGHT];

    for(int f=0;f<NUM_FRAMES;f++){
        phi += PHI_DELTA;
        generateFrame(phi, uv, normal, frame);
        convertFrameToOLEDChars(frame, oledBuffer);

        printf("static const char ANIM_FRAME%d [] PROGMEM = {\n", f);
        printOLEDHex(oledBuffer);
        printf("};\n");
    }
}

//========================================
// Main
//========================================

int main() {
    printOledDataToHexConsole(defaultUV, defaultNormal);
    // asciiOledDataToConsole(defaultUV, defaultNormal);
    return 0;
}
