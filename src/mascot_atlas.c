/*
    mascot_atlas.c - wl_shimeji's mascot texture atlas system

    Copyright (C) 2024  CluelessCatBurger <github.com/CluelessCatBurger>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, see <https://www.gnu.org/licenses/>.
*/


#define _GNU_SOURCE

#include <sys/mman.h>
#include <sys/stat.h>

#include "mascot_atlas.h"
#include <spng.h>
#include <errno.h>

void _find_ireg(const uint8_t* buffer, uint32_t width, uint32_t height, uint32_t* out_x, uint32_t* out_y, uint32_t* out_width, uint32_t* out_height) {
    uint32_t minX = UINT32_MAX;
    uint32_t minY = UINT32_MAX;
    uint32_t maxX = 0;
    uint32_t maxY = 0;

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            // Calculate the index for the alpha component of the current pixel
            uint32_t alphaIndex = (y * width + x) * 4 + 3; // 4 components per pixel, alpha is the 4th
            uint8_t alpha = buffer[alphaIndex];

            if (alpha > 0) { // Pixel is not fully transparent
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }

    // Calculate the output values
    if (minX <= maxX && minY <= maxY) { // Ensure we found at least one non-transparent pixel
        *out_x = minX;
        *out_y = minY;
        *out_width = maxX - minX + 1;
        *out_height = maxY - minY + 1;
    } else { // No non-transparent pixels found, return an empty region
        *out_x = 0;
        *out_y = 0;
        *out_width = 0;
        *out_height = 0;
    }
}

void _write_memfd(int fd, uint64_t* memfd_pos, const uint8_t* buffer, size_t buffer_len, uint32_t width, uint32_t height) {
    uint8_t* buffers = calloc(2, buffer_len);

    if(!buffers) ERROR("Failed to write buffers to memfd: Allocation failed for buffer with size %d", buffer_len*2);

    // Convert RGBA to ARGB and fill the direct and mirrored buffers
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            size_t rgbaIndex = (y * width + x) * 4;
            size_t argbIndex = rgbaIndex; // Direct mapping for ARGB buffer
            size_t mirrorIndex = (y * width + (width - 1 - x)) * 4; // Mirrored mapping

            *(buffers+argbIndex) = buffer[rgbaIndex + 3] ? buffer[rgbaIndex + 2] : 0; // B
            *(buffers+argbIndex+1) = buffer[rgbaIndex + 3] ? buffer[rgbaIndex + 1] : 0; // G
            *(buffers+argbIndex+2) = buffer[rgbaIndex + 3] ? buffer[rgbaIndex + 0] : 0; // R
            *(buffers+argbIndex+3) = buffer[rgbaIndex + 3]; // A

            // Fill mirrored ARGB buffer
            *(buffers+buffer_len+mirrorIndex) = buffer[rgbaIndex + 3] ? buffer[rgbaIndex + 2] : 0; // B
            *(buffers+buffer_len+mirrorIndex+1) = buffer[rgbaIndex + 3] ? buffer[rgbaIndex + 1] : 0; // G
            *(buffers+buffer_len+mirrorIndex+2) = buffer[rgbaIndex + 3] ? buffer[rgbaIndex + 0] : 0; // R
            *(buffers+buffer_len+mirrorIndex+3) = buffer[rgbaIndex + 3]; // A
        }
    }

    *memfd_pos += write(fd, buffers, buffer_len);
    *memfd_pos += write(fd, buffers+buffer_len, buffer_len);

    free(buffers);
}


struct mascot_atlas* mascot_atlas_new(struct wl_compositor* compositor, struct wl_shm* shm_global, const char* dirname)
{

    if (!compositor || !shm_global || !strlen(dirname)) return NULL;

    struct dirent* direntry = NULL;
    spng_ctx** png_ctxs = NULL;
    uint16_t sprite_count = 0;
    struct mascot_atlas* atlas = NULL;

    DEBUG("Creating mascot atlas from dir \"%s\"", dirname);

    DIR* dir;
    if (!(dir = opendir(dirname))) {
        WARN("Could not create atlas from dir \"%s\": %s", dirname, strerrordesc_np(errno));
        return NULL;
    }

    while ((direntry = readdir(dir)))
    {
        if (direntry->d_type != DT_REG) continue;
        int namelen = strnlen(direntry->d_name, 32);
        if (namelen < 5) continue;
        if (!strcasecmp(direntry->d_name + namelen -4, ".png")) sprite_count ++;
    }
    rewinddir(dir);

    if (!sprite_count) {
        closedir(dir);
        WARN("Could not create atlas from dir \"%s\": No sprites found", dirname);
        return NULL;
    }

    png_ctxs = calloc(sprite_count, sizeof(spng_ctx*));
    char ** namelist = calloc(sprite_count, sizeof(char*));
    FILE ** filelist = calloc(sprite_count, sizeof(FILE*));

    if (!png_ctxs) {
        ERROR("Could not create atlas from dir \"%s\": Allocation failed for png_ctxs", dirname);
    }
    if (!namelist) {
        ERROR("Could not create atlas from dir \"%s\":Allocation failed for namelists", dirname);
    }
    if (!filelist) {
        ERROR("Could not create atlas from dir \"%s\": Allocation failed for filelists", dirname);
    }

    uint16_t local_ecount = 0;
    while ((direntry = readdir(dir))) {
        if (direntry->d_type != DT_REG) continue;
        int namelen = strnlen(direntry->d_name, 32);
        if (namelen < 5) continue;
        if (!strcasecmp(direntry->d_name + namelen -4, ".png")) {
            if (local_ecount > sprite_count) break;
            else local_ecount ++;
            png_ctxs[local_ecount-1] = spng_ctx_new(0);
            if (!png_ctxs[local_ecount-1])
            {
                ERROR("Could not create atlas from dir \"%s\": Allocation failed for spng_ctx", dirname);
            }
            char filepath[256] = {0};
            snprintf(filepath, 256, "%s/%s", dirname, direntry->d_name);
            filelist[local_ecount-1] = fopen(filepath, "r");
            if (!filelist[local_ecount-1]) {
                WARN("Could not create atlas from dir \"%s\": Some error prevented sprites from opening: %s", dirname, strerrordesc_np(errno));
                closedir(dir);
                goto fail_free_png_ctxs;
            }
            spng_set_png_file(png_ctxs[local_ecount-1], filelist[local_ecount-1]);
            namelist[local_ecount-1] = strdup(direntry->d_name);
        }
    }
    closedir(dir);

    int memfd = memfd_create(dirname, 0);
    uint64_t memfd_pos = 0;
    if (memfd < 0) {
        ERROR("Could not create atlas from dir \"%s\": memfd_create failed: %s", dirname, strerrordesc_np(errno));
    }

    atlas = calloc(1, sizeof(struct mascot_atlas));
    if (!atlas) ERROR("Could not create atlas from dir \"%s\": Allocation failed for atlas struct", dirname);

    atlas->sprites = calloc(2*sprite_count, sizeof(struct mascot_sprite));
    if (!atlas->sprites) ERROR("Could not create atlas from dir \"%s\": Allocation failed for atlas sprites array", dirname);

    for (uint16_t sprite_i = 0; sprite_i < sprite_count; sprite_i ++) {
        size_t bufsize;
        uint32_t width, height;
        struct spng_ihdr image_header = {0};
        spng_decoded_image_size(png_ctxs[sprite_i], SPNG_FMT_RGBA8, &bufsize);
        spng_get_ihdr(png_ctxs[sprite_i], &image_header);
        width = image_header.width;
        height = image_header.height;

        uint8_t* buffer = calloc(1, bufsize);
        if (!buffer) ERROR("Could not create atlas from dir \"%s\": Allocation failed for decoded buffer (%d bytes) for sprite %s", dirname, bufsize, namelist[sprite_i]);

        spng_decode_image(png_ctxs[sprite_i], buffer, bufsize, SPNG_FMT_RGBA8, 0);

        spng_ctx_free(png_ctxs[sprite_i]);
        fclose(filelist[sprite_i]);

        uint64_t memfd_offset = memfd_pos;
        uint32_t input_x, input_y, input_width, input_height;

        _find_ireg(buffer, width, height, &input_x, &input_y, &input_width, &input_height);

        atlas->sprites[(sprite_i*2)] = (struct mascot_sprite) {
            .height = height,
            .width = width,
            .memfd_offset = memfd_offset,
            .input_region = wl_compositor_create_region(compositor)
        };

        atlas->sprites[(sprite_i*2)+1] = (struct mascot_sprite) {
            .height = height,
            .width = width,
            .memfd_offset = memfd_offset + bufsize,
            .input_region = wl_compositor_create_region(compositor)
        };

        wl_region_add(atlas->sprites[(sprite_i*2)].input_region, input_x, input_y, input_width, input_height);
        wl_region_add(atlas->sprites[(sprite_i*2)+1].input_region, width - input_x - input_width, input_y, input_width, input_height);

        _write_memfd(memfd, &memfd_pos, buffer, bufsize, width, height);

    }

    struct wl_shm_pool* shm_pool = wl_shm_create_pool(shm_global, memfd, memfd_pos);
    for (uint16_t mascot_sprite = 0; mascot_sprite < sprite_count*2; mascot_sprite ++) {
        atlas->sprites[mascot_sprite].buffer = wl_shm_pool_create_buffer(
            shm_pool, atlas->sprites[mascot_sprite].memfd_offset,
            atlas->sprites[mascot_sprite].width, atlas->sprites[mascot_sprite].height,
            atlas->sprites[mascot_sprite].width*4, WL_SHM_FORMAT_ARGB8888
        );
        if (mascot_sprite%2 == 0) DEBUG("Atlas \"%s\": Created sprite \"%s\"", dirname, namelist[mascot_sprite/2]);
        else DEBUG("Atlas \"%s\": Created sprite \"%s\" (Right)", dirname, namelist[mascot_sprite/2]);
    }

    wl_shm_pool_destroy(shm_pool);

    atlas->name_order = namelist;
    atlas->sprite_count = sprite_count;
    free(png_ctxs);
    free(filelist);

    close(memfd);

    DEBUG("Sucessfully created atlas for directory \"%s\", located at %p", dirname, atlas);

    return atlas;

fail_free_png_ctxs:
    free(atlas);
    for (uint16_t i = 0; i < sprite_count; i++) spng_ctx_free(png_ctxs[i]);
    for (uint16_t i = 0; i < sprite_count; i++) free(namelist[i]);
    for (uint16_t i = 0; i < sprite_count; i++) fclose(filelist[i]);
    free(png_ctxs);
    free(namelist);
    return NULL;
}

void mascot_atlas_destroy(struct mascot_atlas* atlas)
{
    if (!atlas) return;

    for (uint16_t i = 0; i < atlas->sprite_count*2; i++) {
        if (i % 2 == 0) free(atlas->name_order[i/2]);
        if (atlas->sprites[i].buffer) wl_buffer_destroy(atlas->sprites[i].buffer);
        if (atlas->sprites[i].input_region) wl_region_destroy(atlas->sprites[i].input_region);
    }

    free(atlas->name_order);
    free(atlas->sprites);
    free(atlas);
}


struct mascot_sprite* mascot_atlas_get(const struct mascot_atlas* atlas, uint16_t index, bool right)
{
    if (!atlas) return NULL;
    if (index >= atlas->sprite_count) return NULL;

    return &atlas->sprites[(index*2)+right];
}

uint16_t mascot_atlas_get_name_index(const struct mascot_atlas* atlas, const char* name)
{
    if (!atlas) return UINT16_MAX;

    for (uint16_t i = 0; i < atlas->sprite_count; i++) {
        if (strcmp(atlas->name_order[i], name) == 0) {
            return i;
        }
    }

    return UINT16_MAX;
}
