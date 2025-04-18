#include "ddnet_map_loader.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct datafile_item_type_t {
  int type;
  int start;
  int num;
} datafile_item_type_t;

typedef struct datafile_item_t {
  int type_and_id;
  int size;
} datafile_item_t;

typedef struct datafile_header_t {
  char id[4];
  int version;
  int size;
  int swaplen;
  int num_item_types;
  int num_items;
  int num_raw_data;
  int item_size;
  int data_size;
} datafile_header_t;

typedef struct datafile_info_t {
  datafile_item_type_t *item_types;
  int *item_offsets;
  int *data_offsets;
  int *data_sizes;
  char *item_start;
  char *data_start;
} datafile_info_t;

typedef struct datafile_t {
  FILE *file;
  datafile_info_t info;
  datafile_header_t header;
  int data_start_offset;
  char **data_ptrs;
  int *data_sizes;
  char *data;
} datafile_t;

typedef struct tile_t {
  unsigned char index;
  unsigned char flags;
  unsigned char skip;
  unsigned char reserved;
} tile_t;

typedef struct tele_tile_t {
  unsigned char number;
  unsigned char type;
} tele_tile_t;

typedef struct speedup_tile_t {
  unsigned char force;
  unsigned char max_speed;
  unsigned char type;
  short angle;
} speedup_tile_t;

typedef struct switch_tile_t {
  unsigned char number;
  unsigned char type;
  unsigned char flags;
  unsigned char delay;
} switch_tile_t;

typedef struct door_tile_t {
  unsigned char index;
  unsigned char flags;
  int number;
} door_tile_t;

typedef struct tune_tile_t {
  unsigned char number;
  unsigned char type;
} tune_tile_t;

typedef struct map_item_group_t {
  int version;
  int offset_x;
  int offset_y;
  int parallax_x;
  int parallax_y;
  int start_layer;
  int num_layers;
} map_item_group_t;

typedef struct map_item_layer_t {
  int version;
  int type;
  int flags;
} map_item_layer_t;

typedef struct map_item_info_settings_t {
  int version;
  int author;
  int map_version;
  int credits;
  int license;
  int settings;
} map_item_info_settings_t;

typedef struct map_item_layer_tilemap_t {
  map_item_layer_t layer;
  int version;
  int width;
  int height;
  int flags;
  int color[4];
  int color_env;
  int color_env_offset;
  int image;
  int data;
  int name[3];
  int tele;
  int speedup;
  int front;
  int switch_;
  int tune;
} map_item_layer_tilemap_t;

enum {
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

int get_file_data_size(datafile_t *data_file, int index) {
  if (!data_file) {
    return 0;
  }
  if (index == data_file->header.num_raw_data - 1)
    return data_file->header.data_size - data_file->info.data_offsets[index];
  return data_file->info.data_offsets[index + 1] - data_file->info.data_offsets[index];
}

static void swap_endian(void *data, size_t size, size_t num) {
  uint32_t *int_ptr = (uint32_t *)data;
  for (size_t i = 0; i < num; i++)
    int_ptr[i] = ((int_ptr[i] >> 24) & 0x000000FF) | ((int_ptr[i] >> 8) & 0x0000FF00) |
                 ((int_ptr[i] << 8) & 0x00FF0000) | ((int_ptr[i] << 24) & 0xFF000000);
}

void *get_data(datafile_t *data_file, int index) {
  if (!data_file) {
    return NULL;
  }
  if (index < 0 || index >= data_file->header.num_raw_data) {
    return NULL;
  }
  if (!data_file->data_ptrs[index]) {
    if (data_file->data_sizes[index] < 0) {
      return NULL;
    }
    unsigned data_size = get_file_data_size(data_file, index);
#if defined(CONF_ARCH_ENDIAN_BIG)
    unsigned swap_size = data_size;
#endif
    if (data_file->header.version == 4) {
      const unsigned original_uncompressed_size = data_file->info.data_sizes[index];
      unsigned long uncompressed_size = original_uncompressed_size;
      void *compressed_data = malloc(data_size);
      unsigned actual_data_size = 0;
      if (fseek(data_file->file, data_file->data_start_offset + data_file->info.data_offsets[index],
                SEEK_SET) == 0) {
        actual_data_size = fread(compressed_data, 1, data_size, data_file->file);
      }
      if (data_size != actual_data_size) {
        free(compressed_data);
        data_file->data_ptrs[index] = NULL;
        data_file->data_sizes[index] = -1;
        return NULL;
      }
      data_file->data_ptrs[index] = (char *)malloc(uncompressed_size);
      data_file->data_sizes[index] = uncompressed_size;
      const int result = uncompress((Bytef *)data_file->data_ptrs[index], &uncompressed_size,
                                    (Bytef *)compressed_data, data_size);
      free(compressed_data);
      if (result != Z_OK || uncompressed_size != original_uncompressed_size) {
        free(data_file->data_ptrs[index]);
        data_file->data_ptrs[index] = NULL;
        data_file->data_sizes[index] = -1;
        return NULL;
      }
#if defined(CONF_ARCH_ENDIAN_BIG)
      swap_size = uncompressed_size;
#endif
    } else {
      data_file->data_ptrs[index] = (char *)malloc(data_size);
      data_file->data_sizes[index] = data_size;
      unsigned actual_data_size = 0;
      if (fseek(data_file->file, data_file->data_start_offset + data_file->info.data_offsets[index],
                SEEK_SET) == 0) {
        actual_data_size = fread(data_file->data_ptrs[index], 1, data_size, data_file->file);
      }
      if (data_size != actual_data_size) {
        free(data_file->data_ptrs[index]);
        data_file->data_ptrs[index] = NULL;
        data_file->data_sizes[index] = -1;
        return NULL;
      }
    }
#if defined(CONF_ARCH_ENDIAN_BIG)
    if (swap_size) {
      swap_endian(data_file->data_ptrs[index], sizeof(int), swap_size / sizeof(int));
    }
#endif
  }
  return data_file->data_ptrs[index];
}

void get_type(datafile_t *data_file, int type, int *start, int *num) {
  *start = 0;
  *num = 0;
  if (!data_file)
    return;
  for (int i = 0; i < data_file->header.num_item_types; i++) {
    if (data_file->info.item_types[i].type == type) {
      *start = data_file->info.item_types[i].start;
      *num = data_file->info.item_types[i].num;
      return;
    }
  }
}

int get_item_size(datafile_t *data_file, int index) {
  if (index == data_file->header.num_items - 1)
    return data_file->header.item_size - data_file->info.item_offsets[index] - sizeof(datafile_item_t);
  return data_file->info.item_offsets[index + 1] - data_file->info.item_offsets[index] -
         sizeof(datafile_item_t);
}

void *get_item(datafile_t *data_file, int index, int *type, int *id) {
  if (!data_file) {
    if (type)
      *type = 0;
    if (id)
      *id = 0;
    return NULL;
  }
  datafile_item_t *item =
      (datafile_item_t *)(data_file->info.item_start + data_file->info.item_offsets[index]);
  const int item_type = (item->type_and_id >> 16) & 0xffff;
  if (type) {
    *type = item_type;
  }
  if (id) {
    *id = item->type_and_id & 0xffff;
  }
  return (void *)(item + 1);
}

int get_data_size(datafile_t *data_file, int index) {
  if (index < 0 || index >= data_file->header.num_raw_data) {
    return 0;
  }
  if (!data_file->data_ptrs[index]) {
    if (data_file->header.version >= 4) {
      return data_file->info.data_sizes[index];
    } else {
      return get_file_data_size(data_file, index);
    }
  }
  const int size = data_file->data_sizes[index];
  if (size < 0)
    return 0;
  return size;
}

map_data_t load_map(const char *name) {
  FILE *map_file = fopen(name, "r");
  if (!map_file) {
    printf("Could not load map: %s\n", name);
    return (map_data_t){};
  }
  map_data_t map_data = {0};
  datafile_header_t file_header;
  fread(&file_header, sizeof(datafile_header_t), 1, map_file);
  unsigned size = 0;
  size += file_header.num_item_types * sizeof(datafile_item_type_t);
  size += (file_header.num_items + file_header.num_raw_data) * sizeof(int);
  if (file_header.version == 4)
    size += file_header.num_raw_data * sizeof(int);
  size += file_header.item_size;
  unsigned alloc_size = size;
  alloc_size += sizeof(datafile_t);
  alloc_size += file_header.num_raw_data * sizeof(void *);
  alloc_size += file_header.num_raw_data * sizeof(int);
  if (size > (((int64_t)1) << 31) || file_header.num_item_types < 0 || file_header.num_items < 0 ||
      file_header.num_raw_data < 0 || file_header.item_size < 0) {
    fclose(map_file);
    printf("Invalid map signature\n");
    return map_data;
  }
  datafile_t *tmp_data_file = (datafile_t *)malloc(alloc_size);
  tmp_data_file->file = map_file;
  tmp_data_file->header = file_header;
  tmp_data_file->data_start_offset = sizeof(datafile_header_t) + size;
  tmp_data_file->data_ptrs = (char **)(tmp_data_file + 1);
  tmp_data_file->data_sizes = (int *)(tmp_data_file->data_ptrs + file_header.num_raw_data);
  tmp_data_file->data = (char *)(tmp_data_file->data_sizes + file_header.num_raw_data);
  memset(tmp_data_file->data_ptrs, 0, file_header.num_raw_data * sizeof(void *));
  memset(tmp_data_file->data_sizes, 0, file_header.num_raw_data * sizeof(int));
  unsigned read_size = fread(tmp_data_file->data, 1, size, map_file);
  if (read_size != size) {
    free(tmp_data_file);
    fclose(map_file);
    printf("Could not load whole map. got %d expected %d\n", read_size, size);
    return map_data;
  }
  tmp_data_file->info.item_types = (datafile_item_type_t *)tmp_data_file->data;
  tmp_data_file->info.item_offsets =
      (int *)&tmp_data_file->info.item_types[tmp_data_file->header.num_item_types];
  tmp_data_file->info.data_offsets = &tmp_data_file->info.item_offsets[tmp_data_file->header.num_items];
  tmp_data_file->info.data_sizes = &tmp_data_file->info.data_offsets[tmp_data_file->header.num_raw_data];
  if (file_header.version == 4)
    tmp_data_file->info.item_start =
        (char *)&tmp_data_file->info.data_sizes[tmp_data_file->header.num_raw_data];
  else
    tmp_data_file->info.item_start =
        (char *)&tmp_data_file->info.data_offsets[tmp_data_file->header.num_raw_data];
  tmp_data_file->info.data_start = tmp_data_file->info.item_start + tmp_data_file->header.item_size;
  int groups_num, groups_start, layers_num, layers_start;
  get_type(tmp_data_file, MAPITEMTYPE_GROUP, &groups_start, &groups_num);
  get_type(tmp_data_file, MAPITEMTYPE_LAYER, &layers_start, &layers_num);
  for (int g = 0; g < groups_num; ++g) {
    map_item_group_t *group = get_item(tmp_data_file, groups_start + g, NULL, NULL);
    for (int l = 0; l < group->num_layers; l++) {
      map_item_layer_t *layer = get_item(tmp_data_file, layers_start + group->start_layer + l, NULL, NULL);
      if (layer->type != 2)
        continue;
      map_item_layer_tilemap_t *tilemap = (map_item_layer_tilemap_t *)layer;
      int size = tilemap->width * tilemap->height;
      if (tilemap->flags & TILESLAYERFLAG_GAME) {
        tile_t *tiles = get_data(tmp_data_file, tilemap->data);
        if (tiles) {
          unsigned char *new_data = malloc(size);
          unsigned char *new_flags = malloc(size);
          for (int i = 0; i < size; ++i) {
            new_data[i] = tiles[i].index;
            new_flags[i] = tiles[i].flags;
          }
          map_data.game_layer.data = new_data;
          map_data.game_layer.flags = new_flags;
          map_data.width = tilemap->width;
          map_data.height = tilemap->height;
        }
        continue;
      }
      if (tilemap->flags & TILESLAYERFLAG_FRONT) {
        tile_t *tiles = get_data(tmp_data_file, tilemap->front);
        if (tiles) {
          unsigned char *new_data = malloc(size);
          unsigned char *new_flags = malloc(size);
          for (int i = 0; i < size; ++i) {
            new_data[i] = tiles[i].index;
            new_flags[i] = tiles[i].flags;
          }
          map_data.front_layer.data = new_data;
          map_data.front_layer.flags = new_flags;
        }
        continue;
      }
      if (tilemap->flags & TILESLAYERFLAG_TELE) {
        tele_tile_t *tiles = get_data(tmp_data_file, tilemap->tele);
        if (tiles) {
          unsigned char *new_type = malloc(size);
          unsigned char *new_number = malloc(size);
          for (int i = 0; i < size; ++i) {
            new_type[i] = tiles[i].type;
            new_number[i] = tiles[i].number;
          }
          map_data.tele_layer.type = new_type;
          map_data.tele_layer.number = new_number;
        }
        continue;
      }
      if (tilemap->flags & TILESLAYERFLAG_SPEEDUP) {
        speedup_tile_t *tiles = get_data(tmp_data_file, tilemap->speedup);
        if (tiles) {
          unsigned char *new_force = malloc(size);
          unsigned char *new_max_speed = malloc(size);
          unsigned char *new_type = malloc(size);
          short *new_angle = malloc(size);
          for (int i = 0; i < size; ++i) {
            new_force[i] = tiles[i].force;
            new_max_speed[i] = tiles[i].max_speed;
            new_type[i] = tiles[i].type;
            new_angle[i] = tiles[i].angle;
          }
          map_data.speedup_layer.force = new_force;
          map_data.speedup_layer.max_speed = new_max_speed;
          map_data.speedup_layer.type = new_type;
          map_data.speedup_layer.angle = new_angle;
        }
        continue;
      }
      if (tilemap->flags & TILESLAYERFLAG_SWITCH) {
        switch_tile_t *tiles = get_data(tmp_data_file, tilemap->switch_);
        if (tiles) {
          unsigned char *new_type = malloc(size);
          unsigned char *new_number = malloc(size);
          unsigned char *new_flags = malloc(size);
          unsigned char *new_delay = malloc(size);
          for (int i = 0; i < size; ++i) {
            new_type[i] = tiles[i].type;
            new_number[i] = tiles[i].number;
            new_flags[i] = tiles[i].flags;
            new_delay[i] = tiles[i].delay;
          }
          map_data.switch_layer.type = new_type;
          map_data.switch_layer.number = new_number;
          map_data.switch_layer.flags = new_flags;
          map_data.switch_layer.delay = new_delay;
        }
        continue;
      }
      if (tilemap->flags & TILESLAYERFLAG_TUNE) {
        tune_tile_t *tiles = get_data(tmp_data_file, tilemap->tune);
        if (tiles) {
          unsigned char *new_type = malloc(size);
          unsigned char *new_number = malloc(size);
          for (int i = 0; i < size; ++i) {
            new_type[i] = tiles[i].type;
            new_number[i] = tiles[i].number;
          }
          map_data.tune_layer.type = new_type;
          map_data.tune_layer.number = new_number;
        }
        continue;
      }
    }
  }
  int info_num, info_start;
  get_type(tmp_data_file, MAPITEMTYPE_INFO, &info_start, &info_num);
  for (int i = info_start; i < info_start + info_num; i++) {
    int item_id;
    map_item_info_settings_t *item = (map_item_info_settings_t *)get_item(tmp_data_file, i, NULL, &item_id);
    int item_size = get_item_size(tmp_data_file, i);
    if (!item || item_id != 0)
      continue;
    if (item_size < (int)sizeof(map_item_info_settings_t))
      break;
    if (!(item->settings > -1))
      break;
    int size = get_data_size(tmp_data_file, item->settings);
    char *settings = (char *)get_data(tmp_data_file, item->settings);
    char *next = settings;
    map_data.num_settings = 0;
    while (next < settings + size) {
      int str_size = strlen(next) + 1;
      next += str_size;
      ++map_data.num_settings;
    }
    map_data.settings = malloc(map_data.num_settings * sizeof(void *));
    next = settings;
    int a = 0;
    while (next < settings + size) {
      int str_size = strlen(next) + 1;
      map_data.settings[a] = malloc(str_size);
      strcpy(map_data.settings[a], next);
      ++a;
      next += str_size;
    }
    break;
  }
  for (int i = 0; i < tmp_data_file->header.num_raw_data; i++) {
    free(tmp_data_file->data_ptrs[i]);
    tmp_data_file->data_ptrs[i] = NULL;
    tmp_data_file->data_sizes[i] = 0;
  }
  fclose(map_file);
  free(tmp_data_file);
  return map_data;
}

void free_map_data(map_data_t *map_data) {
  if (map_data == NULL)
    return;
  free(map_data->game_layer.data);
  free(map_data->game_layer.flags);
  free(map_data->front_layer.data);
  free(map_data->front_layer.flags);
  free(map_data->tele_layer.number);
  free(map_data->tele_layer.type);
  free(map_data->speedup_layer.force);
  free(map_data->speedup_layer.max_speed);
  free(map_data->speedup_layer.type);
  free(map_data->speedup_layer.angle);
  free(map_data->switch_layer.number);
  free(map_data->switch_layer.type);
  free(map_data->switch_layer.flags);
  free(map_data->switch_layer.delay);
  free(map_data->door_layer.index);
  free(map_data->door_layer.flags);
  free(map_data->door_layer.number);
  free(map_data->tune_layer.number);
  free(map_data->tune_layer.type);
  for (int i = 0; i < map_data->num_settings; ++i)
    free(map_data->settings[i]);
  free(map_data->settings);
  memset(map_data, 0, sizeof(map_data_t));
}
