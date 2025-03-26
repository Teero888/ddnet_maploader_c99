# DDNet Map Loader
This is a minimalistic C99-compatible map loader, inspired by the map loading code from [DDNet](https://github.com/ddnet/ddnet).

## Features
- Loads map files in a simple and isolated manner.
- Minimal dependencies. (zlib, libc)

## Usage
To use the map loader in your project, simply include the `map_loader.h` file and compile the `map_loader.c` file along with your project.

### Example
```c
#include "map_loader.h"
#include <stdio.h>

int main(void) {
  SMapData map_data = load_map("/path/to/your/map.map");

  if (!map_data.m_GameLayer.m_pData)
    return 1;

  printf("Map loaded successfully!\n");

  // Get tile at x: 24, y: 10
  printf("Tile: %d\n",
         map_data.m_GameLayer.m_pData[10 * map_data.m_Width + 24]);

  // Print all map settings
  printf("Settings: \n");
  for (int i = 0; i < map_data.m_NumSettings; ++i)
    printf("\t%s\n", map_data.m_ppSettings[i]);

  free_map_data(&map_data);
  return 0;
}
```
`gcc example.c map_loader.c -lz -std=c99`
