#ifndef MAP_LOADER_H
#define MAP_LOADER_H

enum {
  TILE_AIR = 0,
  TILE_SOLID,
  TILE_DEATH,
  TILE_NOHOOK,
  TILE_NOLASER,
  TILE_THROUGH_CUT,
  TILE_THROUGH,
  TILE_JUMP,
  TILE_FREEZE = 9,
  TILE_TELEINEVIL,
  TILE_UNFREEZE,
  TILE_DFREEZE,
  TILE_DUNFREEZE,
  TILE_TELEINWEAPON,
  TILE_TELEINHOOK,
  TILE_WALLJUMP = 16,
  TILE_EHOOK_ENABLE,
  TILE_EHOOK_DISABLE,
  TILE_HIT_ENABLE,
  TILE_HIT_DISABLE,
  TILE_SOLO_ENABLE,
  TILE_SOLO_DISABLE,
};

struct GameTiles {
  unsigned char *m_pData;
  unsigned char *m_pFlags;
  int m_Width;
  int m_Height;
} typedef GameTiles;

GameTiles LoadMap(const char *pName);

#endif // MAP_LOADER_H
