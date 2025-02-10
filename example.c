#include "map_loader.h"
#include <stdio.h>

int main() {
  GameTiles map_data = load_map("/path/to/your/map.map");

  if (!map_data.m_pData)
    return 1;

  printf("Map loaded successfully!\n");
  // Get tile at x: 24, y: 10
  printf("Tile: %d\n", map_data.m_pData[10 * map_data.m_Width + 24]);

  free_map_data(&map_data);
  return 0;
}
// compile with gcc example.c map_loader.c -lz