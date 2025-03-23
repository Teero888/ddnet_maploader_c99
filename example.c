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
// compile with gcc example.c map_loader.c -lz -std=c99
