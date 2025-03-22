#include "map_loader.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct {
  int m_Type;
  int m_Start;
  int m_Num;
} CDatafileItemType;

typedef struct {
  int m_TypeAndId;
  int m_Size;
} CDatafileItem;

typedef struct {
  char m_aId[4];
  int m_Version;
  int m_Size;
  int m_Swaplen;
  int m_NumItemTypes;
  int m_NumItems;
  int m_NumRawData;
  int m_ItemSize;
  int m_DataSize;
} CDatafileHeader;

typedef struct {
  CDatafileItemType *m_pItemTypes;
  int *m_pItemOffsets;
  int *m_pDataOffsets;
  int *m_pDataSizes;

  char *m_pItemStart;
  char *m_pDataStart;
} CDatafileInfo;

typedef struct {
  FILE *m_pFile;
  CDatafileInfo m_Info;
  CDatafileHeader m_Header;
  int m_DataStartOffset;
  char **m_ppDataPtrs;
  int *m_pDataSizes;
  char *m_pData;
} CDatafile;

typedef struct {
  unsigned char m_Index;
  unsigned char m_Flags;
  unsigned char m_Skip;
  unsigned char m_Reserved;
} CTile;

typedef struct {
  unsigned char m_Number;
  unsigned char m_Type;
} CTeleTile;

typedef struct {
  unsigned char m_Force;
  unsigned char m_MaxSpeed;
  unsigned char m_Type;
  short m_Angle;
} CSpeedupTile;

typedef struct {
  unsigned char m_Number;
  unsigned char m_Type;
  unsigned char m_Flags;
  unsigned char m_Delay;
} CSwitchTile;

typedef struct {
  unsigned char m_Index;
  unsigned char m_Flags;
  int m_Number;
} CDoorTile;

typedef struct {
  unsigned char m_Number;
  unsigned char m_Type;
} CTuneTile;

typedef struct {
  int m_Version;
  int m_OffsetX;
  int m_OffsetY;
  int m_ParallaxX;
  int m_ParallaxY;

  int m_StartLayer;
  int m_NumLayers;
} CMapItemGroup;

typedef struct {
  int m_Version;
  int m_Type;
  int m_Flags;
} CMapItemLayer;

typedef struct {
  int m_Version;
  int m_Author;
  int m_MapVersion;
  int m_Credits;
  int m_License;
  int m_Settings;
} CMapItemInfoSettings;

typedef struct {
  CMapItemLayer m_Layer;
  int m_Version;

  int m_Width;
  int m_Height;
  int m_Flags;

  int m_aColor[4];
  int m_ColorEnv;
  int m_ColorEnvOffset;

  int m_Image;
  int m_Data;

  int m_aName[3];

  // DDRace

  int m_Tele;
  int m_Speedup;
  int m_Front;
  int m_Switch;
  int m_Tune;
} CMapItemLayerTilemap;

enum {
  LAYERFLAG_DETAIL = 1,
  TILESLAYERFLAG_GAME = 1,
  TILESLAYERFLAG_TELE = 2,
  TILESLAYERFLAG_SPEEDUP = 4,
  TILESLAYERFLAG_FRONT = 8,
  TILESLAYERFLAG_SWITCH = 16,
  TILESLAYERFLAG_TUNE = 32,
};

enum {
  MAPITEMTYPE_VERSION = 0,
  MAPITEMTYPE_INFO,
  MAPITEMTYPE_IMAGE,
  MAPITEMTYPE_ENVELOPE,
  MAPITEMTYPE_GROUP,
  MAPITEMTYPE_LAYER,
  MAPITEMTYPE_ENVPOINTS,
  MAPITEMTYPE_SOUND,
};

int get_file_data_size(CDatafile *pDataFile, int Index) {
  if (!pDataFile) {
    return 0;
  }

  if (Index == pDataFile->m_Header.m_NumRawData - 1)
    return pDataFile->m_Header.m_DataSize -
           pDataFile->m_Info.m_pDataOffsets[Index];

  return pDataFile->m_Info.m_pDataOffsets[Index + 1] -
         pDataFile->m_Info.m_pDataOffsets[Index];
}

void *get_data(CDatafile *pDataFile, int Index) {
  if (!pDataFile) {
    return NULL;
  }

  if (Index < 0 || Index >= pDataFile->m_Header.m_NumRawData)
    return NULL;

  // load it if needed
  if (!pDataFile->m_ppDataPtrs[Index]) {
    // don't try to load again if it previously failed
    if (pDataFile->m_pDataSizes[Index] < 0)
      return NULL;

    // fetch the data size
    unsigned DataSize = get_file_data_size(pDataFile, Index);
#if defined(CONF_ARCH_ENDIAN_BIG)
    unsigned SwapSize = DataSize;
#endif

    if (pDataFile->m_Header.m_Version == 4) {
      // v4 has compressed data
      const unsigned OriginalUncompressedSize =
          pDataFile->m_Info.m_pDataSizes[Index];
      unsigned long UncompressedSize = OriginalUncompressedSize;

      // read the compressed data
      void *pCompressedData = malloc(DataSize);
      unsigned ActualDataSize = 0;
      if (fseek(pDataFile->m_pFile,
                pDataFile->m_DataStartOffset +
                    pDataFile->m_Info.m_pDataOffsets[Index],
                SEEK_SET) == 0)
        ActualDataSize =
            fread(pCompressedData, 1, DataSize, pDataFile->m_pFile);
      if (DataSize != ActualDataSize) {
        free(pCompressedData);
        pDataFile->m_ppDataPtrs[Index] = NULL;
        pDataFile->m_pDataSizes[Index] = -1;
        return NULL;
      }

      // decompress the data
      pDataFile->m_ppDataPtrs[Index] = (char *)malloc(UncompressedSize);
      pDataFile->m_pDataSizes[Index] = UncompressedSize;
      const int Result =
          uncompress((Bytef *)pDataFile->m_ppDataPtrs[Index], &UncompressedSize,
                     (Bytef *)pCompressedData, DataSize);
      free(pCompressedData);
      if (Result != Z_OK || UncompressedSize != OriginalUncompressedSize) {
        free(pDataFile->m_ppDataPtrs[Index]);
        pDataFile->m_ppDataPtrs[Index] = NULL;
        pDataFile->m_pDataSizes[Index] = -1;
        return NULL;
      }

#if defined(CONF_ARCH_ENDIAN_BIG)
      SwapSize = UncompressedSize;
#endif
    } else {
      // load the data
      pDataFile->m_ppDataPtrs[Index] = (char *)(malloc(DataSize));
      pDataFile->m_pDataSizes[Index] = DataSize;
      unsigned ActualDataSize = 0;
      if (fseek(pDataFile->m_pFile,
                pDataFile->m_DataStartOffset +
                    pDataFile->m_Info.m_pDataOffsets[Index],
                SEEK_SET) == 0)
        ActualDataSize = fread(pDataFile->m_ppDataPtrs[Index], DataSize, 1,
                               pDataFile->m_pFile);
      if (DataSize != ActualDataSize) {
        free(pDataFile->m_ppDataPtrs[Index]);
        pDataFile->m_ppDataPtrs[Index] = NULL;
        pDataFile->m_pDataSizes[Index] = -1;
        return NULL;
      }
    }
  }
  return pDataFile->m_ppDataPtrs[Index];
}

void get_type(CDatafile *pDataFile, int Type, int *pStart, int *pNum) {
  *pStart = 0;
  *pNum = 0;

  if (!pDataFile)
    return;

  for (int i = 0; i < pDataFile->m_Header.m_NumItemTypes; i++) {
    if (pDataFile->m_Info.m_pItemTypes[i].m_Type == Type) {
      *pStart = pDataFile->m_Info.m_pItemTypes[i].m_Start;
      *pNum = pDataFile->m_Info.m_pItemTypes[i].m_Num;
      return;
    }
  }
}
int get_itemsize(CDatafile *pDataFile, int Index) {
  if (Index == pDataFile->m_Header.m_NumItems - 1)
    return pDataFile->m_Header.m_ItemSize -
           pDataFile->m_Info.m_pItemOffsets[Index] - sizeof(CDatafileItem);
  return pDataFile->m_Info.m_pItemOffsets[Index + 1] -
         pDataFile->m_Info.m_pItemOffsets[Index] - sizeof(CDatafileItem);
}

void *get_item(CDatafile *pDataFile, int Index, int *pType, int *pId) {
  if (!pDataFile) {
    if (pType)
      *pType = 0;
    if (pId)
      *pId = 0;
    return NULL;
  }

  CDatafileItem *pItem =
      (CDatafileItem *)(pDataFile->m_Info.m_pItemStart +
                        pDataFile->m_Info.m_pItemOffsets[Index]);

  // remove sign extension
  const int Type = (pItem->m_TypeAndId >> 16) & 0xffff;
  if (pType) {
    *pType = Type;
  }
  if (pId) {
    *pId = pItem->m_TypeAndId & 0xffff;
  }
  return (void *)(pItem + 1);
}

int get_data_size(CDatafile *pDataFile, int Index) {
  if (Index < 0 || Index >= pDataFile->m_Header.m_NumRawData) {
    return 0;
  }
  if (!pDataFile->m_ppDataPtrs[Index]) {
    if (pDataFile->m_Header.m_Version >= 4) {
      return pDataFile->m_Info.m_pDataSizes[Index];
    } else {
      return get_file_data_size(pDataFile, Index);
    }
  }
  const int Size = pDataFile->m_pDataSizes[Index];
  if (Size < 0)
    return 0;
  return Size;
}

SMapData load_map(const char *pName) {
  FILE *pMapFile;
  pMapFile = fopen(pName, "r");
  if (!pMapFile) {
    printf("Could not load map: %s\n", pName);
    return (SMapData){};
  }
  SMapData MapData;
  memset(&MapData, 0, sizeof(SMapData));

  // read in the header
  CDatafileHeader FileHeader;
  fread(&FileHeader, sizeof(CDatafileHeader), 1, pMapFile);

  // read in the rest except the data
  unsigned Size = 0;
  Size += FileHeader.m_NumItemTypes * sizeof(CDatafileItemType);
  Size += (FileHeader.m_NumItems + FileHeader.m_NumRawData) * sizeof(int);
  if (FileHeader.m_Version == 4)
    Size += FileHeader.m_NumRawData *
            sizeof(int); // v4 has uncompressed data sizes as well
  Size += FileHeader.m_ItemSize;

  unsigned AllocSize = Size;
  AllocSize += sizeof(CDatafile); // add space for info structure
  AllocSize +=
      FileHeader.m_NumRawData * sizeof(void *); // add space for data pointers
  AllocSize +=
      FileHeader.m_NumRawData * sizeof(int); // add space for data sizes
  if (Size > (((int64_t)1) << 31) || FileHeader.m_NumItemTypes < 0 ||
      FileHeader.m_NumItems < 0 || FileHeader.m_NumRawData < 0 ||
      FileHeader.m_ItemSize < 0) {
    fclose(pMapFile);
    printf("Invalid map signature\n");
    return MapData;
  }

  CDatafile *pTmpDataFile = (CDatafile *)malloc(AllocSize);
  pTmpDataFile->m_pFile = pMapFile;
  pTmpDataFile->m_Header = FileHeader;
  pTmpDataFile->m_DataStartOffset = sizeof(CDatafileHeader) + Size;
  pTmpDataFile->m_ppDataPtrs = (char **)(pTmpDataFile + 1);
  pTmpDataFile->m_pDataSizes =
      (int *)(pTmpDataFile->m_ppDataPtrs + FileHeader.m_NumRawData);
  pTmpDataFile->m_pData =
      (char *)(pTmpDataFile->m_pDataSizes + FileHeader.m_NumRawData);

  // clear the data pointers and sizes
  memset(pTmpDataFile->m_ppDataPtrs, 0,
         FileHeader.m_NumRawData * sizeof(void *));
  memset(pTmpDataFile->m_pDataSizes, 0, FileHeader.m_NumRawData * sizeof(int));

  unsigned ReadSize = fread(pTmpDataFile->m_pData, 1, Size, pMapFile);
  if (ReadSize != Size) {
    free(pTmpDataFile);
    fclose(pMapFile);
    printf("Could not load whole map. got %d expected %d\n", ReadSize, Size);
    return MapData;
  }

  pTmpDataFile->m_Info.m_pItemTypes =
      (CDatafileItemType *)pTmpDataFile->m_pData;
  pTmpDataFile->m_Info.m_pItemOffsets =
      (int *)&pTmpDataFile->m_Info
          .m_pItemTypes[pTmpDataFile->m_Header.m_NumItemTypes];
  pTmpDataFile->m_Info.m_pDataOffsets =
      &pTmpDataFile->m_Info.m_pItemOffsets[pTmpDataFile->m_Header.m_NumItems];
  pTmpDataFile->m_Info.m_pDataSizes =
      &pTmpDataFile->m_Info.m_pDataOffsets[pTmpDataFile->m_Header.m_NumRawData];

  if (FileHeader.m_Version == 4)
    pTmpDataFile->m_Info.m_pItemStart =
        (char *)&pTmpDataFile->m_Info
            .m_pDataSizes[pTmpDataFile->m_Header.m_NumRawData];
  else
    pTmpDataFile->m_Info.m_pItemStart =
        (char *)&pTmpDataFile->m_Info
            .m_pDataOffsets[pTmpDataFile->m_Header.m_NumRawData];
  pTmpDataFile->m_Info.m_pDataStart =
      pTmpDataFile->m_Info.m_pItemStart + pTmpDataFile->m_Header.m_ItemSize;

  // Init layers
  int GroupsNum;
  int GroupsStart;
  int LayersNum;
  int LayersStart;
  get_type(pTmpDataFile, MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
  get_type(pTmpDataFile, MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);

  for (int g = 0; g < GroupsNum; ++g) {
    CMapItemGroup *pGroup = get_item(pTmpDataFile, GroupsStart + g, NULL, NULL);
    for (int l = 0; l < pGroup->m_NumLayers; l++) {
      CMapItemLayer *pLayer = get_item(
          pTmpDataFile, LayersStart + pGroup->m_StartLayer + l, NULL, NULL);
      if (pLayer->m_Type != 2) // LAYERTYPE_TILES
        continue;

      CMapItemLayerTilemap *pTilemap = (CMapItemLayerTilemap *)pLayer;
      // its fine. no map is over 40'000x40'000 big
      int Size = pTilemap->m_Width * pTilemap->m_Height;
      if (pTilemap->m_Flags & TILESLAYERFLAG_GAME) {
        CTile *pTiles = get_data(pTmpDataFile, pTilemap->m_Data);
        unsigned char *pNewData = malloc(Size);
        unsigned char *pNewFlags = malloc(Size);
        for (int i = 0; i < Size; ++i) {
          pNewData[i] = pTiles[i].m_Index;
          pNewFlags[i] = pTiles[i].m_Flags;
        }
        MapData.m_GameLayer.m_pData = pNewData;
        MapData.m_GameLayer.m_pFlags = pNewFlags;
        MapData.m_Width = pTilemap->m_Width;
        MapData.m_Height = pTilemap->m_Height;
        continue;
      }
      if (pTilemap->m_Flags & TILESLAYERFLAG_FRONT) {
        CTile *pTiles = get_data(pTmpDataFile, pTilemap->m_Data);
        unsigned char *pNewData = malloc(Size);
        unsigned char *pNewFlags = malloc(Size);
        for (int i = 0; i < Size; ++i) {
          pNewData[i] = pTiles[i].m_Index;
          pNewFlags[i] = pTiles[i].m_Flags;
        }
        MapData.m_FrontLayer.m_pData = pNewData;
        MapData.m_FrontLayer.m_pFlags = pNewFlags;
        continue;
      }
      if (pTilemap->m_Flags & TILESLAYERFLAG_TELE) {
        CTeleTile *pTiles = get_data(pTmpDataFile, pTilemap->m_Data);
        unsigned char *pNewType = malloc(Size);
        unsigned char *pNewNumber = malloc(Size);
        for (int i = 0; i < Size; ++i) {
          pNewType[i] = pTiles[i].m_Type;
          pNewNumber[i] = pTiles[i].m_Number;
        }
        MapData.m_TeleLayer.m_pType = pNewType;
        MapData.m_TeleLayer.m_pNumber = pNewNumber;
        continue;
      }
      if (pTilemap->m_Flags & TILESLAYERFLAG_SPEEDUP) {
        CSpeedupTile *pTiles = get_data(pTmpDataFile, pTilemap->m_Data);
        unsigned char *pNewForce = malloc(Size);
        unsigned char *pNewMaxSpeed = malloc(Size);
        unsigned char *pNewType = malloc(Size);
        short *pNewAngle = malloc(Size);
        for (int i = 0; i < Size; ++i) {
          pNewForce[i] = pTiles[i].m_Force;
          pNewMaxSpeed[i] = pTiles[i].m_MaxSpeed;
          pNewType[i] = pTiles[i].m_Type;
          pNewAngle[i] = pTiles[i].m_Angle;
        }
        MapData.m_SpeedupLayer.m_pForce = pNewForce;
        MapData.m_SpeedupLayer.m_pMaxSpeed = pNewMaxSpeed;
        MapData.m_SpeedupLayer.m_pType = pNewType;
        MapData.m_SpeedupLayer.m_pAngle = pNewAngle;
        continue;
      }
      if (pTilemap->m_Flags & TILESLAYERFLAG_SWITCH) {
        CSwitchTile *pTiles = get_data(pTmpDataFile, pTilemap->m_Data);
        unsigned char *pNewType = malloc(Size);
        unsigned char *pNewNumber = malloc(Size);
        unsigned char *pNewFlags = malloc(Size);
        unsigned char *pNewDelay = malloc(Size);
        for (int i = 0; i < Size; ++i) {
          pNewType[i] = pTiles[i].m_Type;
          pNewNumber[i] = pTiles[i].m_Number;
          pNewFlags[i] = pTiles[i].m_Flags;
          pNewDelay[i] = pTiles[i].m_Delay;
        }
        MapData.m_SwitchLayer.m_pType = pNewType;
        MapData.m_SwitchLayer.m_pNumber = pNewNumber;
        MapData.m_SwitchLayer.m_pFlags = pNewFlags;
        MapData.m_SwitchLayer.m_pDelay = pNewDelay;
        continue;
      }
      if (pTilemap->m_Flags & TILESLAYERFLAG_TUNE) {
        CTuneTile *pTiles = get_data(pTmpDataFile, pTilemap->m_Data);
        unsigned char *pNewType = malloc(Size);
        unsigned char *pNewNumber = malloc(Size);
        for (int i = 0; i < Size; ++i) {
          pNewType[i] = pTiles[i].m_Type;
          pNewNumber[i] = pTiles[i].m_Number;
        }
        MapData.m_SwitchLayer.m_pType = pNewType;
        MapData.m_SwitchLayer.m_pNumber = pNewNumber;
        continue;
      }
    }
  }

  int InfoNum;
  int InfoStart;
  get_type(pTmpDataFile, MAPITEMTYPE_INFO, &InfoStart, &InfoNum);
  for (int i = InfoStart; i < InfoStart + InfoNum; i++) {
    int ItemId;
    CMapItemInfoSettings *pItem =
        (CMapItemInfoSettings *)get_item(pTmpDataFile, i, NULL, &ItemId);
    int ItemSize = get_itemsize(pTmpDataFile, i);
    if (!pItem || ItemId != 0)
      continue;

    if (ItemSize < (int)sizeof(CMapItemInfoSettings))
      break;
    if (!(pItem->m_Settings > -1))
      break;

    int Size = get_data_size(pTmpDataFile, pItem->m_Settings);
    char *pSettings = (char *)get_data(pTmpDataFile, pItem->m_Settings);
    char *pNext = pSettings;
    MapData.m_NumSettings = 0;
    while (pNext < pSettings + Size) {
      int StrSize = strlen(pNext) + 1;
      pNext += StrSize;
      ++MapData.m_NumSettings;
    }
    MapData.m_ppSettings = malloc(MapData.m_NumSettings * sizeof(void *));

    pNext = pSettings;
    int a = 0;
    while (pNext < pSettings + Size) {
      int StrSize = strlen(pNext) + 1;
      MapData.m_ppSettings[a] = malloc(StrSize);
      strcpy(MapData.m_ppSettings[a], pNext);
      ++a;
      pNext += StrSize;
    }
    break;
  }

  // free the data that is loaded
  for (int i = 0; i < pTmpDataFile->m_Header.m_NumRawData; i++) {
    free(pTmpDataFile->m_ppDataPtrs[i]);
    pTmpDataFile->m_ppDataPtrs[i] = NULL;
    pTmpDataFile->m_pDataSizes[i] = 0;
  }
  fclose(pMapFile);
  free(pTmpDataFile);

  // Figure out important things
  // Make lists of spawn points, tele outs and tele checkpoints outs
  for (int i = 0; i < MapData.m_Width * MapData.m_Height; ++i) {
    if (MapData.m_GameLayer.m_pData[i] == ENTITY_SPAWN ||
        MapData.m_GameLayer.m_pData[i] == ENTITY_SPAWN_RED ||
        MapData.m_GameLayer.m_pData[i] == ENTITY_SPAWN_BLUE)
      ++MapData.m_NumSpawnPoints;
    if (MapData.m_TeleLayer.m_pType[i] == TILE_TELEOUT)
      ++MapData.m_NumTeleOuts;
    if (MapData.m_TeleLayer.m_pType[i] == TILE_TELECHECKOUT)
      ++MapData.m_NumTeleCheckOuts;
  }
  MapData.m_pSpawnPoints = malloc(MapData.m_NumSpawnPoints * sizeof(v2));
  MapData.m_pTeleOuts = malloc(MapData.m_NumTeleOuts * sizeof(v2));
  MapData.m_pTeleCheckOuts = malloc(MapData.m_NumTeleCheckOuts * sizeof(v2));

  MapData.m_NumSpawnPoints = 0;
  MapData.m_NumTeleOuts = 0;
  MapData.m_NumTeleCheckOuts = 0;
  for (int y = 0; y < MapData.m_Height; ++y) {
    for (int x = 0; x < MapData.m_Width; ++x) {
      int Idx = y * MapData.m_Width + x;
      if (MapData.m_GameLayer.m_pData[Idx] == ENTITY_SPAWN ||
          MapData.m_GameLayer.m_pData[Idx] == ENTITY_SPAWN_RED ||
          MapData.m_GameLayer.m_pData[Idx] == ENTITY_SPAWN_BLUE)
        MapData.m_pSpawnPoints[MapData.m_NumSpawnPoints++] = (v2){x, y};
      if (MapData.m_TeleLayer.m_pType[Idx] == TILE_TELEOUT)
        MapData.m_pTeleOuts[MapData.m_NumTeleOuts++] = (v2){x, y};
      if (MapData.m_TeleLayer.m_pType[Idx] == TILE_TELECHECKOUT)
        MapData.m_pTeleCheckOuts[MapData.m_NumTeleCheckOuts++] = (v2){x, y};
    }
  }

  return MapData;
}

void free_map_data(SMapData *pMapData) {
  if (pMapData == NULL)
    return;

  // Free GameLayer data
  free(pMapData->m_GameLayer.m_pData);
  free(pMapData->m_GameLayer.m_pFlags);

  // Free FrontLayer data
  free(pMapData->m_FrontLayer.m_pData);
  free(pMapData->m_FrontLayer.m_pFlags);

  // Free TeleLayer data
  free(pMapData->m_TeleLayer.m_pNumber);
  free(pMapData->m_TeleLayer.m_pType);

  // Free SpeedupLayer data
  free(pMapData->m_SpeedupLayer.m_pForce);
  free(pMapData->m_SpeedupLayer.m_pMaxSpeed);
  free(pMapData->m_SpeedupLayer.m_pType);
  free(pMapData->m_SpeedupLayer.m_pAngle);

  // Free SwitchLayer data
  free(pMapData->m_SwitchLayer.m_pNumber);
  free(pMapData->m_SwitchLayer.m_pType);
  free(pMapData->m_SwitchLayer.m_pFlags);
  free(pMapData->m_SwitchLayer.m_pDelay);

  // Free DoorLayer data
  free(pMapData->m_DoorLayer.m_pIndex);
  free(pMapData->m_DoorLayer.m_pFlags);
  free(pMapData->m_DoorLayer.m_pNumber);

  // Free TuneLayer data
  free(pMapData->m_TuneLayer.m_pNumber);
  free(pMapData->m_TuneLayer.m_pType);

  // Free settings string
  for (int i = 0; i < pMapData->m_NumSettings; ++i)
    free(pMapData->m_ppSettings[i]);
  free(pMapData->m_ppSettings);

  // Reset all to 0
  memset(pMapData, 0, sizeof(SMapData));
}
