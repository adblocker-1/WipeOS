/*
 * Image loading for the company logo (PNG / JPG) and the built-in WipeOS logo.
 * Uses stb_image (public domain).
 */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO_WARNINGS
#define STBI_FAILURE_USERMSG
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

extern const int wipeos_logo_48_size;
extern const uint8_t wipeos_logo_48[];
extern const int wipeos_logo_160_size;
extern const uint8_t wipeos_logo_160[];

void img_free(img_t * im)
{
    if(im && im->rgba) free(im->rgba);
    if(im) memset(im, 0, sizeof(*im));
}

/* area-average downscale (alpha-weighted), keeps aspect ratio */
static uint8_t * downscale(const uint8_t * src, int sw, int sh, int dw, int dh)
{
    uint8_t * dst = malloc((size_t)dw * dh * 4);
    if(!dst) return NULL;
    for(int y = 0; y < dh; y++) {
        int y0 = (int)((long long)y * sh / dh), y1 = (int)((long long)(y + 1) * sh / dh);
        if(y1 <= y0) y1 = y0 + 1;
        for(int x = 0; x < dw; x++) {
            int x0 = (int)((long long)x * sw / dw), x1 = (int)((long long)(x + 1) * sw / dw);
            if(x1 <= x0) x1 = x0 + 1;
            double r = 0, gg = 0, b = 0, a = 0;
            int cnt = 0;
            for(int yy = y0; yy < y1; yy++) {
                const uint8_t * p = src + ((size_t)yy * sw + x0) * 4;
                for(int xx = x0; xx < x1; xx++, p += 4) {
                    double al = p[3];
                    r += p[0] * al;
                    gg += p[1] * al;
                    b += p[2] * al;
                    a += al;
                    cnt++;
                }
            }
            uint8_t * d = dst + ((size_t)y * dw + x) * 4;
            if(a > 0) {
                d[0] = (uint8_t)(r / a + 0.5);
                d[1] = (uint8_t)(gg / a + 0.5);
                d[2] = (uint8_t)(b / a + 0.5);
            }
            else d[0] = d[1] = d[2] = 0;
            d[3] = (uint8_t)(a / cnt + 0.5);
        }
    }
    return dst;
}

int img_check_file(const char * path, int * w, int * h, char * err, size_t errlen)
{
    struct stat st;
    if(stat(path, &st) != 0) {
        snprintf(err, errlen, "File not found");
        return -1;
    }
    if(st.st_size > LOGO_MAX_BYTES) {
        snprintf(err, errlen, "File too large (%.1f MB, max. %d MB)", st.st_size / 1048576.0, LOGO_MAX_BYTES / 1048576);
        return -1;
    }
    int iw, ih, comp;
    if(!stbi_info(path, &iw, &ih, &comp)) {
        snprintf(err, errlen, "Not a readable PNG or JPG image");
        return -1;
    }
    if(iw > LOGO_MAX_PX || ih > LOGO_MAX_PX) {
        snprintf(err, errlen, "Image too large (%d x %d px, max. %d x %d)", iw, ih, LOGO_MAX_PX, LOGO_MAX_PX);
        return -1;
    }
    if(iw < 16 || ih < 16) {
        snprintf(err, errlen, "Image too small (%d x %d px)", iw, ih);
        return -1;
    }
    if(w) *w = iw;
    if(h) *h = ih;
    return 0;
}

int img_load(const char * path, int max_w, int max_h, img_t * out, char * err, size_t errlen)
{
    memset(out, 0, sizeof(*out));
    int iw, ih, comp;
    if(img_check_file(path, &iw, &ih, err, errlen) != 0) return -1;
    uint8_t * px = stbi_load(path, &iw, &ih, &comp, 4);
    if(!px) {
        snprintf(err, errlen, "Could not decode image (%s)", stbi_failure_reason());
        return -1;
    }
    /* crop fully transparent borders so the logo sits flush */
    int cx0 = iw, cy0 = ih, cx1 = -1, cy1 = -1;
    for(int y = 0; y < ih; y++)
        for(int x = 0; x < iw; x++)
            if(px[((size_t)y * iw + x) * 4 + 3] > 8) {
                if(x < cx0) cx0 = x;
                if(x > cx1) cx1 = x;
                if(y < cy0) cy0 = y;
                if(y > cy1) cy1 = y;
            }
    if(cx1 < 0) {
        stbi_image_free(px);
        snprintf(err, errlen, "Image is completely transparent");
        return -1;
    }
    int cw = cx1 - cx0 + 1, ch = cy1 - cy0 + 1;
    uint8_t * crop = malloc((size_t)cw * ch * 4);
    if(!crop) {
        stbi_image_free(px);
        snprintf(err, errlen, "Out of memory");
        return -1;
    }
    for(int y = 0; y < ch; y++) memcpy(crop + (size_t)y * cw * 4, px + ((size_t)(y + cy0) * iw + cx0) * 4, (size_t)cw * 4);
    stbi_image_free(px);

    int dw = cw, dh = ch;
    if(dw > max_w) {
        dh = (int)((long long)dh * max_w / dw);
        dw = max_w;
    }
    if(dh > max_h) {
        dw = (int)((long long)dw * max_h / dh);
        dh = max_h;
    }
    if(dw < 1) dw = 1;
    if(dh < 1) dh = 1;
    if(dw != cw || dh != ch) {
        uint8_t * s = downscale(crop, cw, ch, dw, dh);
        free(crop);
        if(!s) {
            snprintf(err, errlen, "Out of memory");
            return -1;
        }
        crop = s;
    }
    out->rgba = crop;
    out->w = dw;
    out->h = dh;
    return 0;
}

int img_wipeos_logo(int large, img_t * out)
{
    int sz = large ? wipeos_logo_160_size : wipeos_logo_48_size;
    const uint8_t * src = large ? wipeos_logo_160 : wipeos_logo_48;
    out->rgba = malloc((size_t)sz * sz * 4);
    if(!out->rgba) return -1;
    memcpy(out->rgba, src, (size_t)sz * sz * 4);
    out->w = out->h = sz;
    return 0;
}

uint8_t * img_to_rgb_on_white(const img_t * im)
{
    uint8_t * rgb = malloc((size_t)im->w * im->h * 3);
    if(!rgb) return NULL;
    for(size_t i = 0; i < (size_t)im->w * im->h; i++) {
        unsigned a = im->rgba[i * 4 + 3];
        for(int c = 0; c < 3; c++) rgb[i * 3 + c] = (uint8_t)((im->rgba[i * 4 + c] * a + 255 * (255 - a) + 127) / 255);
    }
    return rgb;
}
