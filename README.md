# DDNet Map Loader

A compact C99 ddnet map loader inspired by [DDNet](https://github.com/ddnet/ddnet).

## Features

- Loads game layers of map files simply and efficiently.
- Minimal dependencies: zlib, libc.

## Usage

Include `ddnet_map_loader.h` and link against the library.

### Example

```c
#include "ddnet_map_loader.h"
#include <stdio.h>

int main(void) {
    map_data_t map_data = load_map("path/to/map.map");
    if (!map_data.game_layer.data) return 1;

    printf("Map loaded!\n");
    printf("Tile at (24,10): %d\n", map_data.game_layer.data[10 * map_data.width + 24]);

    for (int i = 0; i < map_data.num_settings; ++i)
        printf("\t%s\n", map_data.settings[i]);

    free_map_data(&map_data);
    return 0;
}
```
Compile: `cc example.c -lddnet_map_loader -lz -std=c99`

## Integration

1. Add as a Git submodule:

   ```bash
   git submodule add https://github.com/Teero888/ddnet_maploader_c99 external/ddnet_map_loader
   git submodule update --init
   ```

2. In your `CMakeLists.txt`:

   ```cmake
   add_subdirectory(external/ddnet_map_loader)
   target_link_libraries(your_target PRIVATE ddnet_map_loader)
   ```

3. Build with CMake, ensuring zlib is installed.
