// The one translation unit that holds the bodies of the stb headers Core uses. Core includes the
// headers only, so their warnings stay out of its fatal-warning build.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#include <stb_image.h>

// stb_image_resize.h is the version stb moved under deprecated/ when it split the resizer into its
// own repository; it is the last one carried here and has stbir_resize_uint8_srgb.
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize.h>
