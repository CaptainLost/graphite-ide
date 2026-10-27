// The one translation unit that holds the bodies of the stb headers Core uses. Core includes the
// headers only, so their warnings stay out of its fatal-warning build.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#include <stb_image.h>

// stb keeps stb_image_resize2.h under deprecated/ in this tree.
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>
