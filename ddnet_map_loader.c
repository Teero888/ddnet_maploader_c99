#include "ddnet_map_loader.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#ifdef _WIN32
#include <wchar.h>
#include <windows.h>
static FILE *local_fopen(const char *path, const char *mode) {
  wchar_t wpath[1024];
  wchar_t wmode[16];
  if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024) <= 0)
    return NULL;
  if (MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 16) <= 0)
    return NULL;
  return _wfopen(wpath, wmode);
}
#define fopen local_fopen
#endif

/* C99 datafile reader. Visual formats follow DDNet's mapitems.h and
 * engine/shared/map.cpp, including legacy tile items and v4 tile runs.
 * Decode integers explicitly: raw pixels/tiles must never be endian-swapped. */
#include <limits.h>

typedef struct {
  const unsigned char *bytes;
  size_t size, item_start, raw_start;
  int version, num_types, num_items, num_raw, item_size, raw_size;
  const unsigned char *types, *items, *offsets, *sizes;
  unsigned char **raw;
  size_t *raw_lengths;
} map_reader_t;

static int32_t read_int(const void *ptr) {
  const unsigned char *p = ptr;
  uint32_t u = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
  int32_t result;
  memcpy(&result, &u, sizeof(result));
  return result;
}

static int word(const unsigned char *p, int i) { return read_int(p + (size_t)i * 4); }

static void reader_close(map_reader_t *r) {
  if (r->raw)
    for (int i = 0; i < r->num_raw; ++i)
      free(r->raw[i]);
  free(r->raw);
  free(r->raw_lengths);
}

static int reader_open(map_reader_t *r, const unsigned char *data, size_t size) {
  if (!data || size < 36 || (memcmp(data, "DATA", 4) && memcmp(data, "ATAD", 4)))
    return 0;
  r->bytes = data;
  r->size = size;
  r->version = word(data, 1);
  r->num_types = word(data, 4);
  r->num_items = word(data, 5);
  r->num_raw = word(data, 6);
  r->item_size = word(data, 7);
  r->raw_size = word(data, 8);
  if ((r->version != 3 && r->version != 4) || r->num_types < 0 || r->num_items < 0 || r->num_raw < 0 || r->item_size < 0 || r->raw_size < 0)
    return 0;
  uint64_t meta = 36ull + (uint64_t)r->num_types * 12 + (uint64_t)r->num_items * 4 + (uint64_t)r->num_raw * (r->version == 4 ? 8 : 4);
  if (meta + r->item_size + r->raw_size > size)
    return 0;
  r->types = data + 36;
  r->items = r->types + (size_t)r->num_types * 12;
  r->offsets = r->items + (size_t)r->num_items * 4;
  r->sizes = r->offsets + (size_t)r->num_raw * 4;
  r->item_start = (size_t)meta;
  r->raw_start = r->item_start + r->item_size;
  for (int i = 0; i < r->num_types; ++i) {
    int start = word(r->types + (size_t)i * 12, 1), count = word(r->types + (size_t)i * 12, 2);
    if (start < 0 || count < 0 || start > r->num_items || count > r->num_items - start)
      return 0;
  }
  for (int i = 0; i < r->num_items; ++i) {
    int off = word(r->items, i), end = i + 1 < r->num_items ? word(r->items, i + 1) : r->item_size;
    if (off < 0 || (off & 3) || end < off || end > r->item_size || end - off < 8)
      return 0;
    int len = word(data + r->item_start + off, 1);
    if (len < 0 || (len & 3) || len > end - off - 8)
      return 0;
  }
  for (int i = 0; i < r->num_raw; ++i) {
    int off = word(r->offsets, i), end = i + 1 < r->num_raw ? word(r->offsets, i + 1) : r->raw_size;
    if (off < 0 || end < off || end > r->raw_size || (r->version == 4 && word(r->sizes, i) < 0))
      return 0;
  }
  r->raw = calloc((size_t)r->num_raw, sizeof(*r->raw));
  r->raw_lengths = calloc((size_t)r->num_raw, sizeof(*r->raw_lengths));
  return !r->num_raw || (r->raw && r->raw_lengths);
}

static int find_type(const map_reader_t *r, int type, int *count) {
  *count = 0;
  for (int i = 0; i < r->num_types; ++i) {
    const unsigned char *p = r->types + (size_t)i * 12;
    if (word(p, 0) == type) {
      *count = word(p, 2);
      return word(p, 1);
    }
  }
  return 0;
}

static const unsigned char *item(const map_reader_t *r, int index, size_t *size, int *id) {
  if (index < 0 || index >= r->num_items)
    return NULL;
  const unsigned char *p = r->bytes + r->item_start + word(r->items, index);
  if (size)
    *size = (size_t)word(p, 1);
  if (id)
    *id = word(p, 0) & 0xffff;
  return p + 8;
}

static const unsigned char *raw_data(map_reader_t *r, int index, size_t minimum) {
  if (index < 0 || index >= r->num_raw)
    return NULL;
  int off = word(r->offsets, index);
  size_t compressed = (size_t)(index + 1 < r->num_raw ? word(r->offsets, index + 1) : r->raw_size) - off;
  size_t length = r->version == 4 ? (size_t)word(r->sizes, index) : compressed;
  /* Bound allocations from untrusted compressed input to 512 MiB per block. */
  if (length < minimum || length > 512u * 1024u * 1024u)
    return NULL;
  if (!r->raw[index]) {
    unsigned char *p = malloc(length ? length : 1);
    if (!p)
      return NULL;
    if (r->version == 4) {
      uLongf dest = (uLongf)length;
      if (uncompress(p, &dest, r->bytes + r->raw_start + off, (uLong)compressed) != Z_OK || dest != length) {
        free(p);
        return NULL;
      }
    } else
      memcpy(p, r->bytes + r->raw_start + off, length);
    r->raw[index] = p;
    r->raw_lengths[index] = length;
  }
  return r->raw[index];
}

static char *raw_string(map_reader_t *r, int index) {
  const unsigned char *p = raw_data(r, index, 1);
  if (!p)
    return NULL;
  const unsigned char *end = memchr(p, 0, r->raw_lengths[index]);
  if (!end)
    return NULL;
  size_t len = (size_t)(end - p) + 1;
  char *s = malloc(len);
  if (s)
    memcpy(s, p, len);
  return s;
}

static map_tile_t *read_tiles(map_reader_t *r, int index, size_t count, int packed) {
  const unsigned char *p = raw_data(r, index, packed ? 4 : count * 4);
  if (!p)
    return NULL;
  map_tile_t *tiles = malloc(count * sizeof(*tiles));
  if (!tiles)
    return NULL;
  if (!packed)
    memcpy(tiles, p, count * 4);
  else {
    size_t dst = 0, length = r->raw_lengths[index];
    for (size_t src = 0; src + 4 <= length && dst < count; src += 4) {
      size_t run = (size_t)p[src + 2] + 1;
      if (run > count - dst)
        run = count - dst;
      for (size_t j = 0; j < run; ++j)
        tiles[dst++] = (map_tile_t){p[src], p[src + 1], 0, 0};
    }
    if (dst != count) {
      free(tiles);
      return NULL;
    }
  }
  return tiles;
}

static int parse_visuals(map_reader_t *r, map_data_t *m) {
  int start = find_type(r, 2, &m->num_images);
  m->images = calloc((size_t)m->num_images, sizeof(*m->images));
  if (m->num_images && !m->images)
    return 0;
  for (int i = 0; i < m->num_images; ++i) {
    size_t len = 0;
    const unsigned char *p = item(r, start + i, &len, NULL);
    if (!p || len < 24)
      return 0;
    map_image_t *im = &m->images[i];
    im->width = word(p, 1);
    im->height = word(p, 2);
    im->external = word(p, 3);
    im->name = raw_string(r, word(p, 4));
    if (word(p, 0) > 1 && (len < 28 || word(p, 6) != 1)) {
      im->external = 0;
      im->width = im->height = 0;
      continue;
    }
    if (!im->external && im->width > 0 && im->height > 0) {
      uint64_t bytes = (uint64_t)im->width * im->height * 4;
      if (bytes > SIZE_MAX)
        return 0;
      const unsigned char *pixels = raw_data(r, word(p, 5), (size_t)bytes);
      if (pixels) {
        im->pixels = malloc((size_t)bytes);
        if (!im->pixels)
          return 0;
        memcpy(im->pixels, pixels, (size_t)bytes);
      }
    }
  }
  start = find_type(r, 5, &m->num_layers);
  m->layers = calloc((size_t)m->num_layers, sizeof(*m->layers));
  if (m->num_layers && !m->layers)
    return 0;
  for (int i = 0; i < m->num_layers; ++i) {
    size_t len = 0;
    const unsigned char *p = item(r, start + i, &len, NULL);
    if (!p || len < 12)
      return 0;
    map_layer_t *l = &m->layers[i];
    l->type = word(p, 1);
    l->flags = word(p, 2);
    l->image = -1;
    l->color_env = -1;
    if (l->type == 2) {
      if (len < 60 || word(p, 3) < 2 || word(p, 3) > 4)
        return 0;
      l->width = word(p, 4);
      l->height = word(p, 5);
      l->tile_flags = word(p, 6);
      if (l->width <= 0 || l->height <= 0 || (uint64_t)l->width * l->height > INT_MAX / 4)
        return 0;
      for (int c = 0; c < 4; ++c)
        l->color[c] = word(p, 7 + c);
      l->color_env = word(p, 11);
      l->color_env_offset = word(p, 12);
      l->image = word(p, 13);
      if (!l->tile_flags) {
        l->tiles = read_tiles(r, word(p, 14), (size_t)l->width * l->height, word(p, 3) == 4);
        if (!l->tiles)
          return 0;
      }
    } else if (l->type == 3) {
      if (len < 28)
        return 0;
      l->num_quads = word(p, 4);
      l->image = word(p, 6);
      if (l->num_quads < 0 || (size_t)l->num_quads > SIZE_MAX / sizeof(map_quad_t))
        return 0;
      if (!l->num_quads)
        continue;
      const unsigned char *data = raw_data(r, word(p, 5), (size_t)l->num_quads * 152);
      if (!data)
        return 0;
      l->quads = calloc((size_t)l->num_quads, sizeof(*l->quads));
      if (!l->quads)
        return 0;
      for (int q = 0; q < l->num_quads; ++q) {
        map_quad_t *quad = &l->quads[q];
        const unsigned char *src = data + (size_t)q * 152;
        for (int j = 0; j < 10; ++j)
          quad->points[j / 2][j % 2] = word(src, j);
        for (int j = 0; j < 16; ++j)
          quad->colors[j / 4][j % 4] = word(src, 10 + j);
        for (int j = 0; j < 8; ++j)
          quad->texcoords[j / 2][j % 2] = word(src, 26 + j);
        quad->pos_env = word(src, 34);
        quad->pos_env_offset = word(src, 35);
        quad->color_env = word(src, 36);
        quad->color_env_offset = word(src, 37);
      }
    }
  }
  start = find_type(r, 4, &m->num_groups);
  m->groups = calloc((size_t)m->num_groups, sizeof(*m->groups));
  if (m->num_groups && !m->groups)
    return 0;
  for (int i = 0; i < m->num_groups; ++i) {
    size_t len = 0;
    const unsigned char *p = item(r, start + i, &len, NULL);
    if (!p || len < 28 || word(p, 0) < 1 || word(p, 0) > 3)
      return 0;
    map_group_t *g = &m->groups[i];
    g->offset_x = word(p, 1);
    g->offset_y = word(p, 2);
    g->parallax_x = word(p, 3);
    g->parallax_y = word(p, 4);
    g->start_layer = word(p, 5);
    g->num_layers = word(p, 6);
    if (g->start_layer < 0 || g->num_layers < 0 || g->start_layer > m->num_layers || g->num_layers > m->num_layers - g->start_layer)
      return 0;
    if (word(p, 0) >= 2) {
      if (len < 48)
        return 0;
      g->use_clipping = word(p, 7);
      g->clip_x = word(p, 8);
      g->clip_y = word(p, 9);
      g->clip_w = word(p, 10);
      g->clip_h = word(p, 11);
    }
    /* DDNet normalizes the game group before rendering any of its layers. */
    for (int j = 0; j < g->num_layers; ++j) {
      if (m->layers[g->start_layer + j].tile_flags & 1) {
        g->offset_x = g->offset_y = g->use_clipping = 0;
        g->parallax_x = g->parallax_y = 100;
      }
    }
  }
  return 1;
}

static int parse_envelopes(map_reader_t *r, map_data_t *m) {
  int start = find_type(r, 3, &m->num_envelopes), upstream = 0;
  m->envelopes = calloc((size_t)m->num_envelopes, sizeof(*m->envelopes));
  if (m->num_envelopes && !m->envelopes)
    return 0;
  for (int i = 0; i < m->num_envelopes; ++i) {
    size_t len = 0;
    const unsigned char *p = item(r, start + i, &len, NULL);
    if (!p || len < 16)
      return 0;
    map_envelope_t *e = &m->envelopes[i];
    e->channels = word(p, 1);
    e->start_point = word(p, 2);
    e->num_points = word(p, 3);
    e->synchronized = word(p, 0) < 2 || (len >= 52 && word(p, 12));
    if (word(p, 0) >= 3)
      upstream = 1;
  }
  int count, point_start = find_type(r, 6, &count);
  if (!count)
    return 1;
  size_t len = 0, stride = upstream ? 88 : 24;
  const unsigned char *points = item(r, point_start, &len, NULL);
  m->num_env_points = (int)(len / stride);
  m->env_points = calloc((size_t)m->num_env_points, sizeof(*m->env_points));
  if (m->num_env_points && !m->env_points)
    return 0;
  const unsigned char *bezier = NULL;
  if (!upstream) {
    int ex_count, ex_start = find_type(r, 0xffff, &ex_count);
    const uint32_t uuid[4] = {0x3ab4f84d, 0xd9cc3c78, 0xb5850d6c, 0xcdb2e25c};
    for (int i = 0; i < ex_count; ++i) {
      int id;
      size_t ex_len = 0;
      const unsigned char *p = item(r, ex_start + i, &ex_len, &id);
      if (ex_len < 16)
        continue;
      int match = 1;
      for (int j = 0; j < 4; ++j)
        if ((uint32_t)word(p, j) != uuid[j])
          match = 0;
      if (match) {
        int n, first = find_type(r, id, &n);
        size_t bytes = 0;
        if (n) {
          const unsigned char *b = item(r, first, &bytes, NULL);
          if (bytes / 64 == (size_t)m->num_env_points)
            bezier = b;
        }
        break;
      }
    }
  }
  m->env_bezier = upstream || bezier;
  for (int i = 0; i < m->num_env_points; ++i) {
    map_env_point_t *e = &m->env_points[i];
    const unsigned char *p = points + (size_t)i * stride;
    e->time = word(p, 0);
    e->curve = word(p, 1);
    for (int j = 0; j < 4; ++j)
      e->values[j] = word(p, j + 2);
    const unsigned char *b = upstream ? p + 24 : bezier ? bezier + (size_t)i * 64 : NULL;
    if (b)
      for (int j = 0; j < 4; ++j) {
        e->in_dx[j] = word(b, j);
        e->in_dy[j] = word(b, j + 4);
        e->out_dx[j] = word(b, j + 8);
        e->out_dy[j] = word(b, j + 12);
      }
  }
  for (int i = 0; i < m->num_envelopes; ++i) {
    map_envelope_t *e = &m->envelopes[i];
    if (e->start_point < 0 || e->num_points < 0 || e->start_point > m->num_env_points || e->num_points > m->num_env_points - e->start_point) {
      e->num_points = 0;
      continue;
    }
    for (int j = 1; j < e->num_points; ++j)
      if (m->env_points[e->start_point + j].time < m->env_points[e->start_point + j - 1].time) {
        e->num_points = 0;
        break;
      }
  }
  return 1;
}

static int copy_plane(unsigned char **dest, const unsigned char *src, size_t count, size_t stride, size_t offset) {
  unsigned char *p = malloc(count);
  if (!p)
    return 0;
  for (size_t i = 0; i < count; ++i)
    p[i] = src[i * stride + offset];
  free(*dest);
  *dest = p;
  return 1;
}

static int parse_physics(map_reader_t *r, map_data_t *m) {
  int unused, start = find_type(r, 5, &unused);
  /* Find dimensions first; collision indexes every physics plane by these. */
  for (int g = 0; g < m->num_groups; ++g)
    for (int i = 0; i < m->groups[g].num_layers; ++i) {
      map_layer_t *l = &m->layers[m->groups[g].start_layer + i];
      if (l->type == 2 && (l->tile_flags & 1)) {
        m->width = l->width;
        m->height = l->height;
      }
    }
  if (!m->width || !m->height)
    return 0;
  size_t count = (size_t)m->width * m->height;
  for (int g = 0; g < m->num_groups; ++g)
    for (int j = 0; j < m->groups[g].num_layers; ++j) {
      int i = m->groups[g].start_layer + j;
      map_layer_t *l = &m->layers[i];
      if (l->type != 2 || !l->tile_flags)
        continue;
      if ((size_t)l->width * l->height < count)
        return 0;
      size_t len = 0;
      const unsigned char *p = item(r, start + i, &len, NULL);
      int version = word(p, 3), field = -1, kind;
      if (l->tile_flags & 1) {
        kind = 0;
        field = 14;
      } else if (l->tile_flags & 8) {
        kind = 1;
        field = 2;
      } else if (l->tile_flags & 2) {
        kind = 2;
        field = 0;
      } else if (l->tile_flags & 4) {
        kind = 3;
        field = 1;
      } else if (l->tile_flags & 16) {
        kind = 4;
        field = 3;
      } else if (l->tile_flags & 32) {
        kind = 5;
        field = 4;
      } else
        continue;
      if (kind)
        field += version <= 2 ? 15 : 18;
      if (len < (size_t)(field + 1) * 4)
        return 0;
      int index = word(p, field);
      map_tile_t *tiles = NULL;
      const unsigned char *src;
      if (kind <= 1) {
        tiles = read_tiles(r, index, count, version == 4 && kind == 0);
        src = (const unsigned char *)tiles;
      } else
        src = raw_data(r, index, count * (kind == 3 ? 6 : kind == 4 ? 4 : 2));
      if (!src)
        return 0;
      int ok = 1;
#define PLANE(dst, stride, offset) (ok = copy_plane(&(dst), src, count, stride, offset) && ok)
      if (kind <= 1) {
        game_layer_t *plane = kind ? &m->front_layer : &m->game_layer;
        PLANE(plane->data, 4, 0);
        PLANE(plane->flags, 4, 1);
      } else if (kind == 2) {
        PLANE(m->tele_layer.number, 2, 0);
        PLANE(m->tele_layer.type, 2, 1);
      } else if (kind == 3) {
        PLANE(m->speedup_layer.force, 6, 0);
        PLANE(m->speedup_layer.max_speed, 6, 1);
        PLANE(m->speedup_layer.type, 6, 2);
        short *angles = malloc(count * sizeof(short));
        if (!angles)
          ok = 0;
        else {
          for (size_t k = 0; k < count; ++k) {
            uint16_t a = (uint16_t)src[k * 6 + 4] | (uint16_t)src[k * 6 + 5] << 8;
            angles[k] = (short)(a < 32768 ? (int)a : (int)a - 65536);
          }
          free(m->speedup_layer.angle);
          m->speedup_layer.angle = angles;
        }
      } else if (kind == 4) {
        PLANE(m->switch_layer.number, 4, 0);
        PLANE(m->switch_layer.type, 4, 1);
        PLANE(m->switch_layer.flags, 4, 2);
        PLANE(m->switch_layer.delay, 4, 3);
      } else {
        PLANE(m->tune_layer.number, 2, 0);
        PLANE(m->tune_layer.type, 2, 1);
      }
#undef PLANE
      free(tiles);
      if (!ok)
        return 0;
    }
  return m->game_layer.data != NULL;
}

static int parse_settings(map_reader_t *r, map_data_t *m) {
  int count, start = find_type(r, 1, &count);
  for (int i = 0; i < count; ++i) {
    int id;
    size_t len = 0;
    const unsigned char *p = item(r, start + i, &len, &id);
    if (id || len < 24 || word(p, 5) < 0)
      continue;
    int index = word(p, 5);
    const unsigned char *s = raw_data(r, index, 0);
    if (!s)
      return 0;
    size_t bytes = r->raw_lengths[index], pos = 0;
    while (pos < bytes) {
      const unsigned char *end = memchr(s + pos, 0, bytes - pos);
      if (!end)
        return 0;
      ++m->num_settings;
      pos = (size_t)(end - s) + 1;
    }
    m->settings = calloc((size_t)m->num_settings, sizeof(*m->settings));
    if (m->num_settings && !m->settings) {
      m->num_settings = 0;
      return 0;
    }
    pos = 0;
    for (int j = 0; j < m->num_settings; ++j) {
      size_t n = strlen((const char *)s + pos) + 1;
      m->settings[j] = malloc(n);
      if (!m->settings[j])
        return 0;
      memcpy(m->settings[j], s + pos, n);
      pos += n;
    }
    break;
  }
  return 1;
}

map_data_t load_map_from_memory(unsigned char *buffer, size_t size) {
  map_data_t m = {0};
  map_reader_t r = {0};
  int ok = reader_open(&r, buffer, size) && parse_visuals(&r, &m) && parse_envelopes(&r, &m) && parse_physics(&r, &m) && parse_settings(&r, &m);
  reader_close(&r);
  if (!ok) {
    free_map_data(&m);
    return m;
  }
  m._map_file_data = buffer;
  m._map_file_size = size;
  return m;
}

map_data_t load_map(const char *name) {
  map_data_t m = {0};
  FILE *f = fopen(name, "rb");
  if (!f)
    return m;
  if (fseek(f, 0, SEEK_END)) {
    fclose(f);
    return m;
  }
  long length = ftell(f);
  if (length <= 0 || fseek(f, 0, SEEK_SET)) {
    fclose(f);
    return m;
  }
  unsigned char *buffer = malloc((size_t)length);
  if (!buffer) {
    fclose(f);
    return m;
  }
  size_t got = fread(buffer, 1, (size_t)length, f);
  fclose(f);
  if (got == (size_t)length)
    m = load_map_from_memory(buffer, (size_t)length);
  if (!m.game_layer.data)
    free(buffer);
  return m;
}

void free_map_data(map_data_t *map_data) {
  if (map_data == NULL)
    return;
  if (map_data->images)
    for (int i = 0; i < map_data->num_images; ++i) {
      free(map_data->images[i].name);
      free(map_data->images[i].pixels);
    }
  if (map_data->layers)
    for (int i = 0; i < map_data->num_layers; ++i) {
      free(map_data->layers[i].tiles);
      free(map_data->layers[i].quads);
    }
  free(map_data->images);
  free(map_data->groups);
  free(map_data->layers);
  free(map_data->envelopes);
  free(map_data->env_points);
  // free the main map file buffer if it was loaded from a file
  if (map_data->_map_file_data) {
    free(map_data->_map_file_data);
  }
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
  for (int i = 0; map_data->settings && i < map_data->num_settings; ++i)
    free(map_data->settings[i]);
  free(map_data->settings);
  memset(map_data, 0, sizeof(map_data_t));
}