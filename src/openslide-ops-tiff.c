/*
 *  OpenSlide, a library for reading whole slide image files
 *
 *  Copyright (c) 2007-2010 Carnegie Mellon University
 *  All rights reserved.
 *
 *  OpenSlide is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation, version 2.1.
 *
 *  OpenSlide is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with OpenSlide. If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <config.h>

#include <glib.h>
#include <tiffio.h>
#include <inttypes.h>
#include <cairo.h>

#include "openslide-private.h"
#include "openslide-tilehelper.h"
#include "openslide-hash.h"

struct _openslide_tiffopsdata {
  TIFF *tiff;

  GMutex *tiff_mutex;

  int32_t overlap_count;
  int32_t *overlaps;
  int32_t *layers;

  _openslide_tiff_tilereader_fn tileread;
};


static char *serialize_double(double d) {
  char buf[G_ASCII_DTOSTR_BUF_SIZE];
  g_ascii_dtostr (buf, sizeof buf, d);

  return g_strdup(buf);
}

static const char *store_string_property(TIFF *tiff, GHashTable *ht,
					 const char *name, ttag_t tag) {
  char *value;
  if (TIFFGetFieldDefaulted(tiff, tag, &value)) {
    value = g_strdup(value);
    g_hash_table_insert(ht, g_strdup(name), value);
    return value;
  }
  return NULL;
}

static void store_and_hash_string_property(TIFF *tiff, GHashTable *ht,
					   GChecksum *quickhash1,
					   const char *name, ttag_t tag) {
  _openslide_hash_string(quickhash1, name);
  _openslide_hash_string(quickhash1, store_string_property(tiff, ht, name, tag));
}

static void store_float_property(TIFF *tiff, GHashTable *ht,
				 const char *name, ttag_t tag) {
  float value;
  if (TIFFGetFieldDefaulted(tiff, tag, &value)) {
    g_hash_table_insert(ht, g_strdup(name), serialize_double(value));
  }
}

static void store_and_hash_properties(TIFF *tiff, GHashTable *ht,
				      GChecksum *quickhash1) {
  // strings
  store_string_property(tiff, ht, OPENSLIDE_PROPERTY_NAME_COMMENT,
			TIFFTAG_IMAGEDESCRIPTION);

  // strings to store and hash
  store_and_hash_string_property(tiff, ht, quickhash1,
				 "tiff.ImageDescription", TIFFTAG_IMAGEDESCRIPTION);
  store_and_hash_string_property(tiff, ht, quickhash1,
				 "tiff.Make", TIFFTAG_MAKE);
  store_and_hash_string_property(tiff, ht, quickhash1,
				 "tiff.Model", TIFFTAG_MODEL);
  store_and_hash_string_property(tiff, ht, quickhash1,
				 "tiff.Software", TIFFTAG_SOFTWARE);
  store_and_hash_string_property(tiff, ht, quickhash1,
				 "tiff.DateTime", TIFFTAG_DATETIME);
  store_and_hash_string_property(tiff, ht, quickhash1,
				 "tiff.Artist", TIFFTAG_ARTIST);
  store_and_hash_string_property(tiff, ht, quickhash1,
				 "tiff.HostComputer", TIFFTAG_HOSTCOMPUTER);
  store_and_hash_string_property(tiff, ht, quickhash1,
				 "tiff.Copyright", TIFFTAG_COPYRIGHT);
  store_and_hash_string_property(tiff, ht, quickhash1,
				 "tiff.DocumentName", TIFFTAG_DOCUMENTNAME);


  // don't hash floats, they might be unstable over time
  store_float_property(tiff, ht, "tiff.XResolution", TIFFTAG_XRESOLUTION);
  store_float_property(tiff, ht, "tiff.YResolution", TIFFTAG_YRESOLUTION);
  store_float_property(tiff, ht, "tiff.XPosition", TIFFTAG_XPOSITION);
  store_float_property(tiff, ht, "tiff.YPosition", TIFFTAG_YPOSITION);

  // special
  uint16_t resolution_unit;
  if (TIFFGetFieldDefaulted(tiff, TIFFTAG_RESOLUTIONUNIT, &resolution_unit)) {
    const char *result;

    switch(resolution_unit) {
    case 1:
      result = "none";
      break;
    case 2:
      result = "inch";
      break;
    case 3:
      result = "centimeter";
      break;
    default:
      result = "unknown";
    }

    g_hash_table_insert(ht, g_strdup("tiff.ResolutionUnit"), g_strdup(result));
  }
}

static void destroy_data(struct _openslide_tiffopsdata *data) {
  TIFFClose(data->tiff);
  g_mutex_free(data->tiff_mutex);
  g_free(data->layers);
  g_free(data->overlaps);
  g_slice_free(struct _openslide_tiffopsdata, data);
}

static void destroy(openslide_t *osr) {
  struct _openslide_tiffopsdata *data = osr->data;
  destroy_data(data);
}


static void get_dimensions_unlocked(openslide_t *osr, int32_t layer,
				    int64_t *w, int64_t *h) {
  uint32_t tmp;

  // init
  *w = 0;
  *h = 0;

  struct _openslide_tiffopsdata *data = osr->data;
  TIFF *tiff = data->tiff;

  int32_t ox = 0;
  int32_t oy = 0;
  if (layer < data->overlap_count) {
    ox = data->overlaps[layer * 2];
    oy = data->overlaps[(layer * 2) + 1];
  }

  // get the layer
  g_return_if_fail(TIFFSetDirectory(tiff, data->layers[layer]));

  // figure out tile size
  int64_t tw, th;
  g_return_if_fail(TIFFGetField(tiff, TIFFTAG_TILEWIDTH, &tmp));
  tw = tmp;
  g_return_if_fail(TIFFGetField(tiff, TIFFTAG_TILELENGTH, &tmp));
  th = tmp;

  // get image size
  int64_t iw, ih;
  g_return_if_fail(TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &tmp));
  iw = tmp;
  g_return_if_fail(TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &tmp));
  ih = tmp;

  // num tiles in each dimension
  int64_t tiles_across = (iw / tw) + !!(iw % tw);   // integer ceiling
  int64_t tiles_down = (ih / th) + !!(ih % th);

  // subtract out the overlaps (there are tiles-1 overlaps in each dimension)
  int64_t iw_minus_o = iw;
  int64_t ih_minus_o = ih;
  if (iw >= tw) {
    iw_minus_o -= (tiles_across - 1) * ox;
  }
  if (ih >= th) {
    ih_minus_o -= (tiles_down - 1) * oy;
  }

  // commit
  *w = iw_minus_o;
  *h = ih_minus_o;
}

static void get_dimensions(openslide_t *osr, int32_t layer,
			   int64_t *w, int64_t *h) {
  struct _openslide_tiffopsdata *data = osr->data;

  g_mutex_lock(data->tiff_mutex);
  get_dimensions_unlocked(osr, layer, w, h);
  g_mutex_unlock(data->tiff_mutex);
}

static void read_tile(openslide_t *osr,
		      cairo_t *cr,
		      int32_t layer,
		      int64_t tile_x, int64_t tile_y,
		      double translate_x, double translate_y,
		      struct _openslide_cache *cache) {
  struct _openslide_tiffopsdata *data = osr->data;
  TIFF *tiff = data->tiff;

  uint32_t tmp;

  // set the layer
  g_return_if_fail(TIFFSetDirectory(tiff, data->layers[layer]));

  // figure out raw tile size
  int64_t tw, th;
  g_return_if_fail(TIFFGetField(tiff, TIFFTAG_TILEWIDTH, &tmp));
  tw = tmp;
  g_return_if_fail(TIFFGetField(tiff, TIFFTAG_TILELENGTH, &tmp));
  th = tmp;

  // get image size
  int64_t iw, ih;
  g_return_if_fail(TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &tmp));
  iw = tmp;
  g_return_if_fail(TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &tmp));
  ih = tmp;

  int64_t x = tile_x * tw;
  int64_t y = tile_y * th;

  if ((x >= iw) || (y >= ih)) {
    return;
  }

  // cache
  bool cachemiss;
  uint32_t *tiledata = _openslide_cache_get(cache,
					    x,
					    y,
					    layer);
  cachemiss = !tiledata;
  if (!tiledata) {
    tiledata = g_slice_alloc(tw * th * 4);
    data->tileread(data->tiff, tiledata, x, y, tw, th);
  }

  // draw it
  cairo_surface_t *surface = cairo_image_surface_create_for_data((unsigned char *) tiledata,
								 CAIRO_FORMAT_ARGB32,
								 tw, th,
								 tw * 4);
  cairo_save(cr);
  cairo_translate(cr, translate_x, translate_y);
  cairo_set_source_surface(cr, surface, 0, 0);
  cairo_surface_destroy(surface);
  cairo_paint(cr);

  /*
  int z = 4;
  int64_t tiles_across = (iw / tw) + !!(iw % tw);
  char *zz = g_strdup_printf("%" PRId64 ",%" PRId64 " (%" PRId64 ")",
			     tile_x, tile_y, tile_y * tiles_across + tile_x);
  cairo_set_source_rgb(cr, 0, 0, 0);
  cairo_move_to(cr, 0, 20);
  cairo_show_text(cr, zz);
  cairo_set_source_rgba(cr, 1.0, 0, 0, 0.2);
  cairo_rectangle(cr, 0, 0, z, z);
  cairo_fill(cr);
  cairo_rectangle(cr, tw-z, 0, z, z);
  cairo_fill(cr);
  cairo_rectangle(cr, 0, th-z, z, z);
  cairo_fill(cr);
  cairo_rectangle(cr, tw-z, th-z, z, z);
  cairo_fill(cr);
  g_free(zz);
  */

  cairo_restore(cr);

  // put into cache last, because the cache can free this tile
  if (cachemiss) {
    _openslide_cache_put(cache, x, y, layer,
			 tiledata, tw * th * 4);
  }
}

static void paint_region_unlocked(openslide_t *osr, cairo_t *cr,
				  int64_t x, int64_t y,
				  int32_t layer,
				  int32_t w, int32_t h) {
  struct _openslide_tiffopsdata *data = osr->data;
  TIFF *tiff = data->tiff;
  uint32_t tmp;

  // set the layer
  g_return_if_fail(TIFFSetDirectory(tiff, data->layers[layer]));

  // figure out raw tile size
  int64_t tw, th;
  g_return_if_fail(TIFFGetField(tiff, TIFFTAG_TILEWIDTH, &tmp));
  tw = tmp;
  g_return_if_fail(TIFFGetField(tiff, TIFFTAG_TILELENGTH, &tmp));
  th = tmp;

  // get image size
  int64_t iw, ih;
  g_return_if_fail(TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &tmp));
  iw = tmp;
  g_return_if_fail(TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &tmp));
  ih = tmp;

  // num tiles in each dimension
  int64_t tiles_across = (iw / tw) + !!(iw % tw);   // integer ceiling
  int64_t tiles_down = (ih / th) + !!(ih % th);

  // compute coordinates
  int32_t ox = 0;
  int32_t oy = 0;
  if (layer < data->overlap_count) {
    ox = data->overlaps[layer * 2];
    oy = data->overlaps[(layer * 2) + 1];
  }

  double ds = openslide_get_layer_downsample(osr, layer);
  int64_t ds_x = x / ds;
  int64_t ds_y = y / ds;
  int64_t start_tile_x = ds_x / (tw - ox);
  int64_t end_tile_x = ((ds_x + w) / (tw - ox)) + 1;
  int64_t start_tile_y = ds_y / (th - oy);
  int64_t end_tile_y = ((ds_y + h) / (th - oy)) + 1;

  int32_t offset_x = ds_x % (tw - ox);
  int32_t offset_y = ds_y % (th - oy);

  int32_t advance_x = tw - ox;
  int32_t advance_y = th - oy;

  // special cases for edge tiles
  if (start_tile_x >= tiles_across - 1) {
    start_tile_x = tiles_across - 1;
    offset_x = ds_x - (start_tile_x * (tw - ox));
    advance_x = tw;
    end_tile_x = start_tile_x + 1;
  }
  if (start_tile_y >= tiles_down - 1) {
    start_tile_y = tiles_down - 1;
    offset_y = ds_y - (start_tile_y * (th - oy));
    advance_y = th;
    end_tile_y = start_tile_y + 1;
  }

  _openslide_read_tiles(cr, layer,
			start_tile_x, start_tile_y,
			end_tile_x, end_tile_y,
			offset_x, offset_y,
			advance_x, advance_y,
			osr, osr->cache,
			read_tile);
}

static void paint_region(openslide_t *osr, cairo_t *cr,
			 int64_t x, int64_t y,
			 int32_t layer,
			 int32_t w, int32_t h) {
  struct _openslide_tiffopsdata *data = osr->data;

  g_mutex_lock(data->tiff_mutex);
  paint_region_unlocked(osr, cr, x, y, layer, w, h);
  g_mutex_unlock(data->tiff_mutex);
}


static const struct _openslide_ops _openslide_tiff_ops = {
  .get_dimensions = get_dimensions,
  .paint_region = paint_region,
  .destroy = destroy
};

void _openslide_add_tiff_ops(openslide_t *osr,
			     TIFF *tiff,
			     int32_t overlap_count,
			     int32_t *overlaps,
			     int32_t layer_count,
			     int32_t *layers,
			     _openslide_tiff_tilereader_fn tileread,
			     GChecksum *quickhash1) {
  // allocate private data
  struct _openslide_tiffopsdata *data =
    g_slice_new(struct _openslide_tiffopsdata);

  // store layer info
  data->layers = layers;

  // populate private data
  data->tiff = tiff;
  data->tiff_mutex = g_mutex_new();
  data->tileread = tileread;
  data->overlap_count = overlap_count;
  data->overlaps = overlaps;

  if (osr == NULL) {
    // free now and return
    destroy_data(data);
    return;
  }

  // generate hash of the smallest layer
  TIFFSetDirectory(data->tiff, layers[layer_count - 1]);
  _openslide_hash_tiff_tiles(quickhash1, tiff);

  // load TIFF properties
  TIFFSetDirectory(data->tiff, 0);    // ignoring return value, but nothing we can do if failed
  store_and_hash_properties(data->tiff, osr->properties, quickhash1);

  // store tiff-specific data into osr
  g_assert(osr->data == NULL);

  // general osr data
  osr->layer_count = layer_count;
  osr->data = data;
  osr->ops = &_openslide_tiff_ops;
}

void _openslide_generic_tiff_tilereader(TIFF *tiff,
					uint32_t *dest,
					int64_t x, int64_t y,
					int32_t w, int32_t h) {
  TIFFRGBAImage img;
  char emsg[1024] = "";

  // init
  g_return_if_fail(TIFFRGBAImageOK(tiff, emsg));
  g_return_if_fail(TIFFRGBAImageBegin(&img, tiff, 0, emsg));
  img.req_orientation = ORIENTATION_TOPLEFT;
  img.col_offset = x;
  img.row_offset = y;

  // draw it
  if (!TIFFRGBAImageGet(&img, dest, w, h)) {
    g_critical("TIFFRGBAImageGet failed");

    // can keep going, to do the cleanup below
  }

  // permute
  uint32_t *p = dest;
  uint32_t *end = dest + w * h;
  while (p < end) {
    uint32_t val = *p;
    *p++ = (val & 0xFF00FF00)
      | ((val << 16) & 0xFF0000)
      | ((val >> 16) & 0xFF);
  }

  // done
  TIFFRGBAImageEnd(&img);
}
