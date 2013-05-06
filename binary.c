
#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>

#include "lua.h"
#include "lauxlib.h"


#if (LUA_VERSION_NUM >= 502)
#define luaL_register(L,n,f)	luaL_newlib(L,f)
#endif

#define luaL_check(c, ...)		if (!(c)) luaL_error(L, __VA_ARGS__)

#define AUTHORS 	"Peter.Q"
#define VERSION		"LuaBinary 1.0"
#define RELEASE		"LuaBinary 1.0.1"

#define REF_SIZE	256

typedef enum {
	OP_NIL,
	OP_BOOLEAN,
	OP_NUMBER,
	OP_STRING,
	OP_TABLE,
	OP_TABLE_REF,
	OP_TABLE_DELIMITER,
	OP_TABLE_END,
} OP_TYPE;

typedef struct {
	const void* ptr;
	size_t pos;
} wref;

typedef struct {
	size_t pos;
	size_t idx;
} rref;

static size_t _wsize;
static wref _wrefs[REF_SIZE];

static size_t _rsize;
static rref _rrefs[REF_SIZE];

typedef struct {
	char* data;
	size_t pos;
	size_t size;
} Buffer;


static void Buffer_init(Buffer* buf)
{
	buf->pos = 0;
	buf->size = 256;
	buf->data = (char*)malloc(sizeof(char) * buf->size);
}

static void Buffer_checksize(Buffer* buf, size_t need)
{
	if (buf->pos + need > buf->size)
	{
		buf->size *= 2;
		buf->data = (char*)realloc(buf->data, sizeof(char)* buf->size);
	}
}

static void Buffer_addchar(Buffer* buf, char ch)
{
	Buffer_checksize(buf, 1);
	buf->data[buf->pos++] = ch;
}

static void Buffer_addarray(Buffer* buf, const char* data, size_t size)
{
	Buffer_checksize(buf, size);
	memcpy(buf->data + buf->pos, data, size);
	buf->pos += size;
}


static int push(lua_State *L, Buffer* buf, int idx)
{
	int top = lua_gettop(L);
	int type = lua_type(L, idx);
	switch(type)
	{
		case LUA_TNIL:
			Buffer_addchar(buf, OP_NIL);
			break;
		case LUA_TBOOLEAN:
			Buffer_addchar(buf, OP_BOOLEAN);
			Buffer_addchar(buf, lua_toboolean(L, idx));
			break;
		case LUA_TNUMBER:
		{
			lua_Number n = lua_tonumber(L, idx);
			Buffer_addchar(buf, OP_NUMBER);
			Buffer_addarray(buf, (char *)&n, sizeof(n));
			break;
		}
		case LUA_TSTRING:
		{
			size_t len;
			const char* str = lua_tolstring(L, idx, &len);
			Buffer_addchar(buf, OP_STRING);
			Buffer_addarray(buf, str, len);
			Buffer_addchar(buf, '\0');
			break;
		}
		case LUA_TTABLE:
		{
			const void *ptr = lua_topointer(L, idx);
			size_t i;
			for (i = 0; i < _wsize; ++i)
			{
				if (_wrefs[i].ptr == ptr)
				{
					Buffer_addchar(buf, OP_TABLE_REF);
					Buffer_addarray(buf, (char *)&_wrefs[i].pos, sizeof(size_t));
					goto end;
				}
			}
			luaL_check(_wsize < REF_SIZE, "table refs overflow %d", REF_SIZE);
			_wrefs[_wsize].ptr = ptr;
			_wrefs[_wsize++].pos = buf->pos;
			
			Buffer_addchar(buf, OP_TABLE);
			lua_pushnil(L);
			i = 1;
			while (lua_next(L, idx))
			{
				if (lua_isnumber(L, -2) && lua_tonumber(L, -2) == i)
				{
					++i;
					push(L, buf, lua_gettop(L));
					lua_pop(L, 1);
				}
				else
				{
					Buffer_addchar(buf, OP_TABLE_DELIMITER);
					push(L, buf, lua_gettop(L) - 1);
					push(L, buf, lua_gettop(L));
					lua_pop(L, 1);
					break;
				}
			}
			
			while (lua_next(L, idx))
			{
				push(L, buf, lua_gettop(L) - 1);
				push(L, buf, lua_gettop(L));
				lua_pop(L, 1);
			}
			Buffer_addchar(buf, OP_TABLE_END);
			break;
		}
		default:
			lua_settop(L, top);
			luaL_error(L, "unexpected type:%s", lua_typename(L, type));
			return 0;
	}
end:
	lua_settop(L, top);
	return 1;
}


static int pop(lua_State *L, const char *data, size_t pos, size_t size)
{
	switch(data[pos++])
	{
		case OP_NIL:
			lua_pushnil(L);
			break;
		case OP_BOOLEAN:
			lua_pushboolean(L, data[pos++]);
			break;
		case OP_NUMBER:
			lua_pushnumber(L, *(lua_Number*)(data + pos));
			pos += sizeof(lua_Number);
			break;
		case OP_STRING:
		{
			size_t n = strlen(data + pos);
			lua_pushlstring(L, data + pos, n);
			pos += n + 1;
			break;
		}
		case OP_TABLE:
		{
			size_t i;
			lua_newtable(L);
			lua_pushvalue(L, -1);
			luaL_check(_rsize < REF_SIZE, "table refs overflow %d", REF_SIZE);
			_rrefs[_rsize].pos = pos - 1;
			_rrefs[_rsize++].idx = luaL_ref(L, LUA_REGISTRYINDEX);
			for (i = 1; data[pos] != OP_TABLE_DELIMITER; ++i)
			{
				luaL_check(pos < size, "bad binary data");
				pos = pop(L, data, pos, size);
				lua_rawseti(L, -2, i);
			}
			pos += 1;
			while (data[pos] != OP_TABLE_END)
			{
				luaL_check(pos < size, "bad binary data");
				pos = pop(L, data, pos, size);
				
				luaL_check(pos < size, "bad binary data");
				pos = pop(L, data, pos, size);
				lua_settable(L, -3);
			}
			pos += 1;
			break;
		}
		case OP_TABLE_REF:
		{
			size_t i, where = *(size_t*)(data + pos);
			pos += sizeof(where);
			for (i = 0; i < _rsize; ++i)
			{
				if (_rrefs[i].pos == where)
				{
					lua_rawgeti(L, LUA_REGISTRYINDEX, _rrefs[i].idx);
					return pos;
				}
			}
			luaL_error(L, "bad binary data");
			return 0;
		}
		default:
			luaL_error(L, "bad binary data");
			return 0;
	}
	return pos;
}


static int api_pack (lua_State *L)
{
	Buffer buf;
	int i, args = lua_gettop(L);
	int ok = 1;
	Buffer_init(&buf);
	Buffer_addchar(&buf, args);
	_wsize = 0;
	for (i = 1; ok && i <= args; ++i)
		ok = push(L, &buf, i);
	lua_pushlstring(L, buf.data, buf.size);
	free(buf.data);
	return 1;
}


static int api_unpack (lua_State *L)
{
	size_t pos = 1, len;
	const char *data = luaL_checklstring(L, 1, &len);
	int top = lua_gettop(L), i, args = data[0];
	_rsize = 0;
	for (i = 0; pos && i < args; ++i)
		pos = pop(L, data, pos, len);
	for (i = 0; i < _rsize; ++i)
		luaL_unref(L, LUA_REGISTRYINDEX, _rrefs[i].idx);
	lua_settop(L, top + args);
	return args;
}


static const struct luaL_Reg thislib[] = {
	{"pack", api_pack},
	{"unpack", api_unpack},
	{NULL, NULL}
};


LUALIB_API int luaopen_binary (lua_State *L) {
	luaL_register(L, "binary", thislib);
	
	lua_pushstring(L, "VERSION");
	lua_pushstring(L, VERSION);
	lua_settable(L, -3);
	
	lua_pushstring(L, "RELEASE");
	lua_pushstring(L, RELEASE);
	lua_settable(L, -3);
	
	lua_pushstring(L, "AUTHORS");
	lua_pushstring(L, AUTHORS);
	lua_settable(L, -3);
	return 1;
}

