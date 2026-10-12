// The image decoder the city's renderer reads its photos and sky with
// (third_party/stb, fetched by quest/tools/fetch_assets.py).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_HDR
#define STBI_NO_STDIO
#include <stb_image.h>
