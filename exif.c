//#define JLS_LUA_MOD_TRACE 1
#include "luamod.h"

#include <libexif/exif-loader.h>

#include <stdlib.h>
#include <string.h>

/*
********************************************************************************
* EXIF Structures
********************************************************************************
*/

typedef struct LuaExifLoaderStruct {
  ExifLoader *exifLoader;
} LuaExifLoader;

typedef struct LuaExifDataStruct {
  ExifData *exifData;
} LuaExifData;

#define COMPONENTS_PROPERTY_NAME "components"
#define FORMAT_PROPERTY_NAME     "format"
#define SIZE_PROPERTY_NAME       "size"
#define TAG_PROPERTY_NAME        "tag"
#define VALUE_PROPERTY_NAME      "value"
#define DATA_PROPERTY_NAME       "data"

/*
********************************************************************************
* Helper functions
********************************************************************************
*/

static const char *toBuffer(lua_State *l, int i, size_t *len) {
  if (lua_isstring(l, i)) {
    return lua_tolstring(l, i, len);
  } else if (lua_isuserdata(l, i) && !lua_islightuserdata(l, i)) {
    *len = lua_rawlen(l, i);
    return (const char *)lua_touserdata(l, i);
  }
  return NULL;
}

static const char *checkBuffer(lua_State *l, int i, size_t *len) {
  const char *buffer = toBuffer(l, i, len);
  if (buffer == NULL) {
    luaL_argerror(l, i, "string or userdata expected");
  }
  return buffer;
}

/*
********************************************************************************
* EXIF Data functions
********************************************************************************
*/

static int luaexif_data_new(lua_State *l) {
  trace("luaexif_data_new()\n");
  LuaExifData *data = (LuaExifData *)lua_newuserdata(l, sizeof(LuaExifData));
  data->exifData = exif_data_new();
  luaL_getmetatable(l, "exif_data");
  lua_setmetatable(l, -2);
  return 1;
}

static int luaexif_data_load(lua_State *l) {
  trace("luaexif_data_load()\n");
  LuaExifData *data = (LuaExifData *)luaL_checkudata(l, 1, "exif_data");
  size_t length;
  const char *buffer = checkBuffer(l, 2, &length);
  exif_data_load_data(data->exifData, (unsigned char *)buffer, length);
  return 0;
}

static int luaexif_data_save(lua_State *l) {
  trace("luaexif_data_save()\n");
  LuaExifData *data = (LuaExifData *)luaL_checkudata(l, 1, "exif_data");
  unsigned char *d = NULL;
  unsigned int ds = 0;
  exif_data_save_data(data->exifData, &d, &ds);
  if (d == NULL) {
    return 0;
  }
  lua_pushlstring(l, (const char *)d, (size_t)ds);
  free(d);
  return 1;
}

static int luaexif_data_get_byte_order(lua_State *l) {
  trace("luaexif_data_get_byte_order()\n");
  LuaExifData *data = (LuaExifData *)luaL_checkudata(l, 1, "exif_data");
  ExifByteOrder bo = exif_data_get_byte_order(data->exifData);
  lua_pushinteger(l, (lua_Integer) bo);
  return 1;
}

static int luaexif_data_set_byte_order(lua_State *l) {
  trace("luaexif_data_set_byte_order()\n");
  LuaExifData *data = (LuaExifData *)luaL_checkudata(l, 1, "exif_data");
  ExifByteOrder bo = (ExifByteOrder) luaL_checkinteger(l, 2);
  exif_data_set_byte_order(data->exifData, bo);
  return 0;
}

static int luaexif_data_fix(lua_State *l) {
  trace("luaexif_data_fix()\n");
  LuaExifData *data = (LuaExifData *)luaL_checkudata(l, 1, "exif_data");
  exif_data_fix(data->exifData);
  return 0;
}

static int luaexif_data_from_table(lua_State *l) {
  trace("luaexif_data_from_table()\n");
  LuaExifData *data = (LuaExifData *)luaL_checkudata(l, 1, "exif_data");

  luaL_checktype(l, 2, LUA_TTABLE);

  ExifByteOrder bo = exif_data_get_byte_order(data->exifData);
  ExifIfd ifd;
  for (ifd = EXIF_IFD_0; ifd < EXIF_IFD_COUNT; ifd++) {
    trace("look for %s (%d)\n", exif_ifd_get_name(ifd), ifd);
    lua_getfield(l, 2, exif_ifd_get_name(ifd));
    if (lua_istable(l, -1)) {
      trace("found \"%s\"\n", exif_ifd_get_name(ifd));
      lua_pushnil(l);
      while (lua_next(l, -2)) {
        if (lua_isstring(l, -2) && lua_istable(l, -1)) {
          ExifTag entryTag = 0;
          ExifFormat entryFormat = 0;
          long entryComponents = 0;
          //unsigned char *data;
          unsigned int entrySize = 0;
          const char *entryValue = "";
          int entryDataLength = -1;
          trace("entry name \"%s\"\n", lua_tostring(l, -2));
          SET_OPT_INTEGER_FIELD(l, -1, entryComponents, COMPONENTS_PROPERTY_NAME);
          SET_OPT_INTEGER_FIELD(l, -1, entryFormat, FORMAT_PROPERTY_NAME);
          SET_OPT_INTEGER_FIELD(l, -1, entrySize, SIZE_PROPERTY_NAME);
          SET_OPT_INTEGER_FIELD(l, -1, entryTag, TAG_PROPERTY_NAME);
          SET_OPT_STRING_FIELD(l, -1, entryValue, VALUE_PROPERTY_NAME);
          lua_getfield(l, -1, DATA_PROPERTY_NAME);
          if (lua_istable(l, -1)) { \
            lua_len(l, -1);
            entryDataLength = lua_tointeger(l, -1);
            lua_pop(l, 1);
          }
          lua_pop(l, 1);
          trace(" - tag: %d, format: %d, components: %d (#%d), size: %d value: \"%s\"\n",
            entryTag, entryFormat, entryComponents, entryDataLength, entrySize, entryValue);
          if ((entryTag == 0) || (entryFormat == 0) || ((entryDataLength >= 0) && (entryDataLength >= entryComponents))) {
            trace("skipping invalid entry\n");
            lua_pop(l, 1);
            continue;
          }
          void *buf;
          ExifEntry *entry;
          ExifMem *mem = exif_mem_new_default();
          entry = exif_entry_new_mem(mem);
          buf = exif_mem_alloc(mem, entrySize);
          entry->data = buf;
          entry->size = entrySize;
          entry->tag = entryTag;
          entry->components = entryComponents;
          entry->format = entryFormat;
          exif_content_add_entry(data->exifData->ifd[ifd], entry);
          exif_mem_unref(mem);
          exif_entry_unref(entry);
          if (entryFormat == EXIF_FORMAT_ASCII) {
            memcpy(entry->data, entryValue, strlen(entryValue));
          } else if (entryDataLength > 0) {
            lua_getfield(l, -1, DATA_PROPERTY_NAME);
            trace("process %d component(s)\n", entryComponents);
            int fs = exif_format_get_size(entryFormat);
            int componentIndex, offset = 0;
            for (componentIndex = 1; componentIndex <= entryComponents; componentIndex++) {
              ExifRational r;
              ExifSRational sr;
              lua_Integer num, denominator;
              lua_geti(l, -1, componentIndex);
              num = lua_tointeger(l, -1);
              lua_pop(l, 1);
              trace("data[%d] = %d\n", componentIndex, num);
              if ((entryFormat == EXIF_FORMAT_RATIONAL) || (entryFormat == EXIF_FORMAT_SRATIONAL)) {
                lua_geti(l, -1, entryComponents + componentIndex);
                denominator = lua_tointeger(l, -1);
                lua_pop(l, 1);
              }
              switch (entryFormat) {
              case EXIF_FORMAT_BYTE:
                entry->data[offset] = (ExifByte) num;
                break;
              case EXIF_FORMAT_SBYTE:
                entry->data[offset] = (ExifSByte) num;
                break;
              case EXIF_FORMAT_SHORT:
                exif_set_short(entry->data + offset, bo, (ExifShort) num);
                break;
              case EXIF_FORMAT_SSHORT:
                exif_set_sshort(entry->data + offset, bo, (ExifSShort) num);
                break;
              case EXIF_FORMAT_LONG:
                exif_set_long(entry->data + offset, bo, (ExifLong) num);
                break;
              case EXIF_FORMAT_SLONG:
                exif_set_slong(entry->data + offset, bo, (ExifSLong) num);
                break;
              case EXIF_FORMAT_RATIONAL:
                r.numerator = num;
                r.denominator = denominator;
                exif_set_rational(entry->data + offset, bo, r);
                break;
              case EXIF_FORMAT_SRATIONAL:
                sr.numerator = num;
                sr.denominator = denominator;
                exif_set_srational(entry->data + offset, bo, sr);
                break;
              case EXIF_FORMAT_DOUBLE:
              case EXIF_FORMAT_FLOAT:
              case EXIF_FORMAT_UNDEFINED:
              default:
                break;
              }
              offset += fs;
            }
            lua_pop(l, 1);
          }

        }
        lua_pop(l, 1);
      }
    }
    lua_pop(l, 1);
  }

  return 0;
}

static int luaexif_data_to_table(lua_State *l) {
  trace("luaexif_data_to_table()\n");
  LuaExifData *data = (LuaExifData *)luaL_checkudata(l, 1, "exif_data");
  // table map with an item per EXIF IFD
  lua_newtable(l);
  ExifByteOrder bo = exif_data_get_byte_order(data->exifData);
  ExifIfd ifd;
  for (ifd = EXIF_IFD_0; ifd < EXIF_IFD_COUNT; ifd++) {
    ExifContent *content = data->exifData->ifd[ifd];
    trace("addAllEntries(%d, \"%s\"): %d entries to add\n", ifd, exif_ifd_get_name(ifd), content->count);
    lua_pushstring(l, exif_ifd_get_name(ifd));
    // table map with an item per content
    lua_newtable(l);
    unsigned int entryIndex;
    for (entryIndex = 0; entryIndex < content->count; entryIndex++) {
      ExifEntry *entry = content->entries[entryIndex];
      char value[1024];
      /* Get the contents of the tag in human-readable form */
      exif_entry_get_value(entry, value, sizeof(value));
      //trimSpaces(value);
      if (*value) {
        const char *name = exif_tag_get_name_in_ifd(entry->tag, ifd);
        trace("- entry \"%s\" tag: %d, format: %d, c: %d, s: %d value: \"%s\"\n", name, entry->tag, entry->format, entry->components, entry->size, value);
        lua_pushstring(l, name);
        // table map with an item per entry
        lua_newtable(l);
        SET_TABLE_KEY_INTEGER(l, COMPONENTS_PROPERTY_NAME, entry->components);
        SET_TABLE_KEY_INTEGER(l, FORMAT_PROPERTY_NAME, entry->format);
        SET_TABLE_KEY_INTEGER(l, SIZE_PROPERTY_NAME, entry->size);
        SET_TABLE_KEY_INTEGER(l, TAG_PROPERTY_NAME, entry->tag);
        SET_TABLE_KEY_STRING(l, VALUE_PROPERTY_NAME, value);
        if (entry->format != EXIF_FORMAT_ASCII) {
          lua_pushstring(l, DATA_PROPERTY_NAME);
          // table list with an item per component
          lua_newtable(l);
          int fs = exif_format_get_size(entry->format);
          unsigned int componentIndex, offset = 0;
          for (componentIndex = 1; componentIndex <= entry->components; componentIndex++) {
            ExifRational r;
            ExifSRational sr;
            lua_Integer num = 0;
            int hasDenominator = 0;
            lua_Integer denominator = 0;
            switch (entry->format) {
            case EXIF_FORMAT_BYTE:
              num = (ExifByte) entry->data[offset];
              break;
            case EXIF_FORMAT_SBYTE:
              num = (ExifSByte) entry->data[offset];
              break;
            case EXIF_FORMAT_SHORT:
              num = exif_get_short(entry->data + offset, bo);
              break;
            case EXIF_FORMAT_SSHORT:
              num = exif_get_sshort(entry->data + offset, bo);
              break;
            case EXIF_FORMAT_LONG:
              num = exif_get_long(entry->data + offset, bo);
              break;
            case EXIF_FORMAT_SLONG:
              num = exif_get_slong(entry->data + offset, bo);
              break;
            case EXIF_FORMAT_RATIONAL: // struct with numerator denominator
              r = exif_get_rational(entry->data + offset, bo);
              num = r.numerator;
              denominator = r.denominator;
              hasDenominator = 1;
              break;
            case EXIF_FORMAT_SRATIONAL: // signed
              sr = exif_get_srational(entry->data + offset, bo);
              num = sr.numerator;
              denominator = sr.denominator;
              hasDenominator = 1;
              break;
            case EXIF_FORMAT_DOUBLE:
            case EXIF_FORMAT_FLOAT:
            case EXIF_FORMAT_UNDEFINED:
            default:
              break;
            }
            trace("data[%d] = %d\n", componentIndex, num);
            lua_pushinteger(l, num);
            lua_rawseti(l, -2, componentIndex); // add value in data table list
            if (hasDenominator) {
              lua_pushinteger(l, denominator);
              lua_rawseti(l, -2, entry->components + componentIndex);
            }
            offset += fs;
          }
          lua_rawset(l, -3); // add the data list
        }
        lua_rawset(l, -3); // add the entry map
      }
    }
    lua_rawset(l, -3); // add the ifd map
  }
  return 1;
}

static int luaexif_data_gc(lua_State *l) {
  trace("luaexif_data_gc()\n");
  LuaExifData *data = (LuaExifData *)luaL_testudata(l, 1, "exif_data");
  if ((data != NULL) && (data->exifData != NULL)) {
    exif_data_unref(data->exifData);
  }
  return 0;
}


/*
********************************************************************************
* EXIF Loader functions
********************************************************************************
*/

static int luaexif_loader_new(lua_State *l) {
  trace("luaexif_loader_new()\n");
  LuaExifLoader *loader = (LuaExifLoader *)lua_newuserdata(l, sizeof(LuaExifLoader));
  loader->exifLoader = exif_loader_new();
  luaL_getmetatable(l, "exif_loader");
  lua_setmetatable(l, -2);
  return 1;
}

static int luaexif_loader_write(lua_State *l) {
  trace("luaexif_loader_write()\n");
  size_t bufferLength = 0;
  unsigned char *bufferData = NULL;
  LuaExifLoader *loader = (LuaExifLoader *)luaL_checkudata(l, 1, "exif_loader");

  if (lua_isstring(l, 2)) {
    bufferData = (unsigned char *)luaL_checklstring(l, 2, &bufferLength);
  } else {
    luaL_checktype(l, 2, LUA_TUSERDATA);
    bufferLength = lua_rawlen(l, 2);
    bufferData = (unsigned char *)lua_touserdata(l, 2);
  }
  // return 1 while EXIF data is read (or while there is still hope that there will be EXIF data later on), 0 otherwise.
  int result = exif_loader_write(loader->exifLoader, bufferData, bufferLength);

  lua_pushboolean(l, result);
  return 1;
}

static int luaexif_loader_get_data(lua_State *l) {
  trace("luaexif_loader_get_data()\n");
  LuaExifLoader *loader = (LuaExifLoader *)luaL_checkudata(l, 1, "exif_loader");

  ExifData *exifData = exif_loader_get_data(loader->exifLoader);
  if (exifData == NULL) {
    return 0;
  }
  LuaExifData *data = (LuaExifData *)lua_newuserdata(l, sizeof(LuaExifData));
  data->exifData = exifData;
  luaL_getmetatable(l, "exif_data");
  lua_setmetatable(l, -2);
  return 1;
}

static int luaexif_loader_gc(lua_State *l) {
  trace("luaexif_loader_gc()\n");
  LuaExifLoader *loader = (LuaExifLoader *)luaL_testudata(l, 1, "exif_loader");
  if ((loader != NULL) && (loader->exifLoader != NULL)) {
    exif_loader_unref(loader->exifLoader);
  }
  return 0;
}


/*
********************************************************************************
* Module open function
********************************************************************************
*/

#define LUA_EXIF_VERSION "0.1"
#include <config.h>

LUALIB_API int luaopen_exif(lua_State *l) {
  trace("luaopen_exif()\n");

  luaL_newmetatable(l, "exif_data");
  lua_pushstring(l, "__gc");
  lua_pushcfunction(l, luaexif_data_gc);
  lua_settable(l, -3);

  luaL_newmetatable(l, "exif_loader");
  lua_pushstring(l, "__gc");
  lua_pushcfunction(l, luaexif_loader_gc);
  lua_settable(l, -3);

  luaL_Reg reg[] = {
    // Data
    { "data_new", luaexif_data_new },
    { "data_load", luaexif_data_load },
    { "data_save", luaexif_data_save },
    { "data_get_byte_order", luaexif_data_get_byte_order },
    { "data_set_byte_order", luaexif_data_set_byte_order },
    { "data_from_table", luaexif_data_from_table },
    { "data_to_table", luaexif_data_to_table },
    { "data_fix", luaexif_data_fix },
    // Loader
    { "loader_new", luaexif_loader_new },
    { "loader_write", luaexif_loader_write },
    { "loader_get_data", luaexif_loader_get_data },
    { NULL, NULL }
  };
  lua_newtable(l);
  luaL_setfuncs(l, reg, 0);
  lua_pushliteral(l, "Lua exif");
  lua_setfield(l, -2, "_NAME");
  lua_pushfstring (l, "%s libexif %s", LUA_EXIF_VERSION, VERSION);
  lua_setfield(l, -2, "_VERSION");
  trace("luaopen_exif() done\n");
  return 1;
}
