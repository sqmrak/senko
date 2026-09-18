#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zbar.h>

static const char *const qr_modules[] = {
    "111111100010101111111",
    "100000101100001000001",
    "101110100001001011101",
    "101110100111001011101",
    "101110101100101011101",
    "100000100010101000001",
    "111111101010101111111",
    "000000000101100000000",
    "101010100111000010010",
    "000001000010001000011",
    "111101101010100011111",
    "001010001100001000001",
    "011101111100101010001",
    "000000001011010100101",
    "111111100101011100011",
    "100000100111110110000",
    "101110101101011100011",
    "101110100110001101110",
    "101110101110100011001",
    "100000100100001010010",
    "111111101100101100011"
};

static void assert_qr(uint8_t *pixels, unsigned width, unsigned height,
                      const char *expected)
{
    size_t expected_length = strlen(expected);
    assert(width && height && width <= SIZE_MAX / height);
    zbar_image_scanner_t *scanner = zbar_image_scanner_create();
    zbar_image_t *image = zbar_image_create();
    assert(scanner && image);
    zbar_image_scanner_enable_cache(scanner, 0);
    zbar_image_set_format(image, zbar_fourcc('Y', '8', '0', '0'));
    zbar_image_set_size(image, width, height);
    zbar_image_set_data(image, pixels, (unsigned long)((size_t)width * height),
                        zbar_image_free_data);

    assert(zbar_scan_image(scanner, image) == 1);
    const zbar_symbol_t *symbol = zbar_image_first_symbol(image);
    assert(symbol);
    assert(zbar_symbol_get_type(symbol) == ZBAR_QRCODE);
    assert(zbar_symbol_get_data_length(symbol) == expected_length);
    assert(!memcmp(zbar_symbol_get_data(symbol), expected, expected_length));

    zbar_image_destroy(image);
    zbar_image_scanner_destroy(scanner);
}

static void test_generated_qr(void)
{
    const unsigned modules = 21;
    const unsigned border = 4;
    const unsigned scale = 8;
    const unsigned side = (modules + border * 2) * scale;
    uint8_t *pixels = malloc((size_t)side * side);
    assert(pixels);
    memset(pixels, 255, (size_t)side * side);
    for (unsigned y = 0; y < modules; ++y) {
        for (unsigned x = 0; x < modules; ++x) {
            if (qr_modules[y][x] != '1') continue;
            for (unsigned py = 0; py < scale; ++py)
                memset(pixels + ((size_t)(y + border) * scale + py) * side +
                       (x + border) * scale, 0, scale);
        }
    }

    assert_qr(pixels, side, side, "senko");
}

static void test_pgm(const char *path, const char *expected)
{
    char magic[3] = { 0 };
    unsigned width = 0;
    unsigned height = 0;
    unsigned maximum = 0;
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fscanf(file, "%2s %u %u %u", magic, &width, &height, &maximum) == 4);
    assert(!strcmp(magic, "P5"));
    assert(maximum == 255);
    assert(width && height && width <= SIZE_MAX / height);
    assert(fgetc(file) == '\n');
    size_t length = (size_t)width * height;
    uint8_t *pixels = malloc(length);
    assert(pixels);
    assert(fread(pixels, 1, length, file) == length);
    assert(fclose(file) == 0);
    assert_qr(pixels, width, height, expected);
}

int main(int argc, char **argv)
{
    if (argc == 1)
        test_generated_qr();
    else {
        assert(argc == 3);
        test_pgm(argv[1], argv[2]);
    }
    return 0;
}
