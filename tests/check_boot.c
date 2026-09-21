// SPDX-License-Identifier: Apache-2.0 OR MIT
/* Check characteristic workspace colors in QEMU's P6 screenshot. A console or
 * bootloader screen must not count as a successful graphical boot. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    FILE *input = fopen(argv[1], "rb");
    if (!input) return 2;
    char magic[3]; unsigned width, height, maximum;
    if (fscanf(input, "%2s %u %u %u", magic, &width, &height, &maximum) != 4 ||
        strcmp(magic, "P6") || maximum != 255 || width < 640 || height < 480 ||
        width > 8192 || height > 8192) { fclose(input); return 2; }
    (void)fgetc(input);
    size_t slate = 0, teal = 0, white = 0;
    for (size_t i = 0; i < (size_t)width * height; ++i) {
        unsigned char pixel[3];
        if (fread(pixel, 1, 3, input) != 3) { fclose(input); return 2; }
        if (pixel[0] == 237 && pixel[1] == 242 && pixel[2] == 246) ++slate;
        if (pixel[0] == 8 && pixel[1] == 127 && pixel[2] == 117) ++teal;
        if (pixel[0] == 255 && pixel[1] == 255 && pixel[2] == 255) ++white;
    }
    fclose(input);
    return slate > 10000 && teal > 1000 && white > 10000 ? 0 : 1;
}
