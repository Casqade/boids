# boids

Boids. Just boids.

Inspired by [Mike Acton's Unity boids demo at GDC](https://youtu.be/p65Yt20pw0g).


## Dependencies

`BUILD_VULKAN_LIBS `: Turn this CMake option ON to fetch & build Vulkan from sources (very long build time!)

Otherwise, you'll need to install the following dependencies manually:

```
vulkan-loader
vulkan-headers
vulkan-utility-libraries
vulkan-validation-layers
glslang
spirv-tools
spirv-headers
```

## Building

```
git clone https://github.com/Casqade/boids
cd boids
cmake .
cmake --build .
```

Executable will be in `bin` subdirectory.
