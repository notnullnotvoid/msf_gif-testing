#include "../msf_gif/msf_gif.h"
#include "trace.hpp"
#include "List.hpp"
#include "common.hpp"

bool write_entire_file(const char * filepath, const void * data, size_t bytes) {
    assert(filepath && data);
    FILE * f = fopen(filepath, "wb");
    if (!f) return false;
    if (!fwrite(data, bytes, 1, f)) return false;
    if (fclose(f)) return false;
    return true;
}



struct RawBlob {
    int width, height, frames, centiSeconds;
    int pixels[1];
};

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

void rawframes_to_png_sequence(const char * name) {
    RawBlob * blob = (RawBlob *) read_entire_file(dsprintf(nullptr, "in/%s.rawframes", name));

    //write metadata files
    FILE * out = fopen(dsprintf(nullptr, "in/%s/meta.txt", name), "wb");
    assert(out);
    fprintf(out, "%d %d %d %d", blob->width, blob->height, blob->frames, blob->centiSeconds);
    fclose(out);

    //write PNGs
    for (int i = 0; i < blob->frames; ++i) {
        assert(stbi_write_png(dsprintf(nullptr, "in/%s/%03d.png", name, i), blob->width, abs(blob->height), 4, &blob->pixels[blob->width * abs(blob->height) * i], 4 * blob->width));
    }
}

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

struct Pixel { uint8_t r, g, b, a; };
struct Image { Pixel * p; int w, h; inline Pixel * operator[] (int y) { return &p[y * w]; } };
void png_sequence_to_rawframes(const char * name) {
    //load metadata from metadata file
    FILE * in = fopen(dsprintf(nullptr, "in/%s/meta.txt", name), "rb");
    assert(in);
    int width, height, frames, centiSeconds;
    assert(fscanf(in, "%d %d %d %d", &width, &height, &frames, &centiSeconds) == 4);
    fclose(in);

    //write blob header
    FILE * out = fopen(dsprintf(nullptr, "in/%s.rawframes", name), "wb");
    assert(out);
    assert(fwrite(&width, sizeof(int), 1, out));
    assert(fwrite(&height, sizeof(int), 1, out));
    assert(fwrite(&frames, sizeof(int), 1, out));
    assert(fwrite(&centiSeconds, sizeof(int), 1, out));

    //load images into memory and write to blob
    for (int i = 0; i < frames; ++i) {
        char * path = dsprintf(nullptr, "in/%s/%03d.png", name, i);
        assert(file_exists(path));

        //load image into memory
        int w, h, c;
        unsigned char * data = stbi_load(path, &w, &h, &c, 4);
        assert(data && w == width && h == abs(height));

        //write to blob
        assert(fwrite(data, w * h * 4, 1, out)); //NOTE: important we use w,h because height can be negaitve!
    }
    fclose(out);
}

// extern "C" {
//     void init_prefaulted_malloc();
//     void reset_prefaulted_malloc();
// }

int main() {
    const char * names[] = {
        "bouncy", "diwide-large", "diwide", "floor", "increase", "keyhole", "odd", "tiles",
        "anchor", "always-in-front", "flip",
        "sky",
        "transparent",
        // "keyhole",
    };
    // const char * names[] = { "bouncy", "diwide", "increase" };

    for (const char * name : names) {
        if (!file_exists(dsprintf(nullptr, "in/%s.rawframes", name))) {
            printf("regenerating rawframes: %s\n", name); fflush(stdout);
            png_sequence_to_rawframes(name);
        }
    }

    List<RawBlob *> blobs = {};
    for (const char * name : names) {
        char * path = dsprintf(nullptr, "in/%s.rawframes", name);
        printf("loading %s...\n", path); fflush(stdout);
        blobs.add((RawBlob *) read_entire_file(path));
    }

    struct TimerInfo {
        double time;
        size_t in, out;
    };
    List<TimerInfo> timers = {};
    init_profiling_trace();

    msf_gif_alpha_threshold = 128;

    // init_prefaulted_malloc();

    //TODO: automatically regression-test against known good versions of the GIFs?
    for (int i = 0; i < blobs.len; ++i) {
        // reset_prefaulted_malloc();

        RawBlob * blob = blobs[i];
        bool flipped = blob->height < 0;
        if (flipped) blob->height *= -1;

        msf_gif_bgra_flag = true;
        if (msf_gif_bgra_flag) {
            for (int i = 0; i < blob->width * blob->height * blob->frames; ++i) {
                Pixel * p = (Pixel *) &blob->pixels[i];
                swap(p->r, p->b);
            }
        }

        char * path = dsprintf(nullptr, "new/%s.gif", names[i]);
        TimeScope(path);
        printf("writing %24s      width: %d   height: %d   frames: %d   centiSeconds: %d\n",
            path, blob->width, blob->height, blob->frames, blob->centiSeconds); fflush(stdout);
        FILE * fp = fopen(path, "wb");
        assert(fp);
        double pre = get_time();
        MsfGifState handle = {};
        handle.customAllocatorContext = path;
        #if 1 //to-memory
        assert(msf_gif_begin(&handle, blob->width, blob->height));
        for (int j = 0; j < blob->frames; ++j) {
            int pitch = flipped? -blob->width * 4 : blob->width * 4;
            assert(msf_gif_frame(&handle,
                (uint8_t *) &blob->pixels[blob->width * blob->height * j], blob->centiSeconds, 16, pitch));
        }
        MsfGifResult result = msf_gif_end(&handle);
        assert(result.data);
        timers.add({ get_time() - pre, (size_t)(blob->frames * blob->width * blob->height * 4), result.dataSize });
        assert(fwrite(result.data, result.dataSize, 1, fp));
        fclose(fp);
        msf_gif_free(result);
        #else //to-file
        assert(msf_gif_begin_to_file(&handle, blob->width, blob->height, (MsfGifFileWriteFunc) fwrite, (void *) fp));
        for (int j = 0; j < blob->frames; ++j) {
            int pitch = flipped? -blob->width * 4 : blob->width * 4;
            assert(msf_gif_frame_to_file(&handle,
                (uint8_t *) &blob->pixels[blob->width * blob->height * j], blob->centiSeconds, 16, pitch));
        }
        assert(msf_gif_end_to_file(&handle));
        fclose(fp);
        #endif
    }

    printf("\n");
    TimerInfo totals = {};
    for (int i = 0; i < timers.len; ++i) {
        totals.time += timers[i].time;
        totals.in += timers[i].in;
        totals.out += timers[i].out;
        printf("%16s      time: %5.2f s   in: %4.2f GB/s   out: %5.2f MB/s\n",
            names[i], timers[i].time,
            timers[i].in / timers[i].time / 1024 / 1024 / 1024, timers[i].out / timers[i].time / 1024 / 1024);
    }
    printf("\n%16s      time: %5.2f s  in: %4.2f GB/s   out: %5.2f MB/s\n",
        "totals", totals.time, totals.in / totals.time / 1024 / 1024 / 1024, totals.out / totals.time / 1024 / 1024);

    trace_end_event("main");
    print_profiling_trace();
    return 0;
}
