#include "map_loader.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

struct {
  int m_Type;
  int m_Start;
  int m_Num;
} typedef CDatafileItemType;

struct {
  int m_TypeAndId;
  int m_Size;
} typedef CDatafileItem;

struct {
  char m_aId[4];
  int m_Version;
  int m_Size;
  int m_Swaplen;
  int m_NumItemTypes;
  int m_NumItems;
  int m_NumRawData;
  int m_ItemSize;
  int m_DataSize;
} typedef CDatafileHeader;

struct {
  CDatafileItemType *m_pItemTypes;
  int *m_pItemOffsets;
  int *m_pDataOffsets;
  int *m_pDataSizes;

  char *m_pItemStart;
  char *m_pDataStart;
} typedef CDatafileInfo;

struct {
  FILE *m_pFile;
  CDatafileInfo m_Info;
  CDatafileHeader m_Header;
  int m_DataStartOffset;
  char **m_ppDataPtrs;
  int *m_pDataSizes;
  char *m_pData;
} typedef CDatafile;

struct {
  unsigned char m_Index;
  unsigned char m_Flags;
  unsigned char m_Skip;
  unsigned char m_Reserved;
} typedef CTile;

struct {
  int m_Version;
  int m_OffsetX;
  int m_OffsetY;
  int m_ParallaxX;
  int m_ParallaxY;

  int m_StartLayer;
  int m_NumLayers;
} typedef CMapItemGroup;

struct {
  int m_Version;
  int m_Type;
  int m_Flags;
} typedef CMapItemLayer;

enum {
  CURRENT_VERSION = 3,
  TILE_SKIP_MIN_VERSION = 4, // supported for loading but not saving
};
struct {

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
} typedef CMapItemLayerTilemap;

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

int GetFileDataSize(CDatafile *pDataFile, int Index) {
  if (!pDataFile) {
    return 0;
  }

  if (Index == pDataFile->m_Header.m_NumRawData - 1)
    return pDataFile->m_Header.m_DataSize -
           pDataFile->m_Info.m_pDataOffsets[Index];

  return pDataFile->m_Info.m_pDataOffsets[Index + 1] -
         pDataFile->m_Info.m_pDataOffsets[Index];
}

void *GetData(CDatafile *pDataFile, int Index) {
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
    unsigned DataSize = GetFileDataSize(pDataFile, Index);
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

void GetType(CDatafile *pDataFile, int Type, int *pStart, int *pNum) {
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

void *GetItem(CDatafile *pDataFile, int Index, int *pType, int *pId) {
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

GameTiles LoadMap(const char *pName) {
  FILE *pMapFile;
  pMapFile = fopen(pName, "r");
  if (!pMapFile) {
    printf("Could not load map: %s\n", pName);
    return (GameTiles){};
  }

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
    return (GameTiles){};
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
    return (GameTiles){};
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
  GetType(pTmpDataFile, MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
  GetType(pTmpDataFile, MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);

  GameTiles Tiles;
  for (int g = 0; g < GroupsNum; ++g) {
    CMapItemGroup *pGroup = GetItem(pTmpDataFile, GroupsStart + g, NULL, NULL);
    for (int l = 0; l < pGroup->m_NumLayers; l++) {
      CMapItemLayer *pLayer = GetItem(
          pTmpDataFile, LayersStart + pGroup->m_StartLayer + l, NULL, NULL);
      if (pLayer->m_Type == 2) // LAYERTYPE_TILES
      {
        CMapItemLayerTilemap *pTilemap = (CMapItemLayerTilemap *)pLayer;
        if (pTilemap->m_Flags & TILESLAYERFLAG_GAME) {
          // printf("found game layer! yay!\n");
          CTile *pTiles = GetData(pTmpDataFile, pTilemap->m_Data);
          unsigned char *pNewData =
              malloc(pTilemap->m_Width * pTilemap->m_Height);
          unsigned char *pNewFlags =
              malloc(pTilemap->m_Width * pTilemap->m_Height);

          for (int i = 0; i < pTilemap->m_Width * pTilemap->m_Height; ++i) {
            pNewData[i] = pTiles[i].m_Index;
            pNewFlags[i] = pTiles[i].m_Flags;
          }

          Tiles = (GameTiles){.m_pData = pNewData,
                              .m_pFlags = pNewFlags,
                              .m_Width = pTilemap->m_Width,
                              .m_Height = pTilemap->m_Height};
          break;
        }
      }
    }
  }

  // free the data that is loaded
  for (int i = 0; i < pTmpDataFile->m_Header.m_NumRawData; i++) {
    free(pTmpDataFile->m_ppDataPtrs[i]);
    pTmpDataFile->m_ppDataPtrs[i] = NULL;
    pTmpDataFile->m_pDataSizes[i] = 0;
  }
  fclose(pMapFile);
  free(pTmpDataFile);
  return Tiles;
}
