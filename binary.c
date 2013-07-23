#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>

#include "lua.h"
#include "lauxlib.h"
#include "buffer.h"


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

static int push(lua_State *L, struct buffer* buf, int idx)
{
	int top = lua_gettop(L);
	int type = lua_type(L, idx);
	switch(type)
	{
		case LUA_TNIL:
			buffer_addchar(buf, OP_NIL);
			break;
		case LUA_TBOOLEAN:
			buffer_addchar(buf, OP_BOOLEAN);
			buffer_addchar(buf, lua_toboolean(L, idx));
			break;
		case LUA_TNUMBER:
		{
			lua_Number n = lua_tonumber(L, idx);
			buffer_addchar(buf, OP_NUMBER);
			buffer_addarray(buf, (char *)&n, sizeof(n));
			break;
		}
		case LUA_TSTRING:
		{
			size_t len;
			const char* str = lua_tolstring(L, idx, &len);
			buffer_addchar(buf, OP_STRING);
			buffer_addarray(buf, str, len);
			buffer_addchar(buf, '\0');
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
					buffer_addchar(buf, OP_TABLE_REF);
					buffer_addarray(buf, (char *)&_wrefs[i].pos, sizeof(size_t));
					goto end;
				}
			}
			luaL_check(_wsize < REF_SIZE, "table refs overflow %d", REF_SIZE);
			_wrefs[_wsize].ptr = ptr;
			_wrefs[_wsize++].pos = buffer_tell(buf);
			
			buffer_addchar(buf, OP_TABLE);
			lua_pushnil(L);
			i = 1;
			while (lua_next(L, idx))
			{
				if (lua_isnumber(L, -2) && lua_tonumber(L, -2) == i++)
				{
					push(L, buf, lua_gettop(L));
					lua_pop(L, 1);
				}
				else break;
			}
			buffer_addchar(buf, OP_TABLE_DELIMITER);
			if (lua_gettop(L) > top)
			{
				do
				{
					push(L, buf, lua_gettop(L) - 1);
					push(L, buf, lua_gettop(L));
					lua_pop(L, 1);
				}
				while (lua_next(L, idx));
			}
			buffer_addchar(buf, OP_TABLE_END);
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
	int op = data[pos++];
	switch(op)
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
				luaL_check(pos < size, "bad data, when read index %d:%d", pos, size);
				pos = pop(L, data, pos, size);
				lua_rawseti(L, -2, i);
			}
			pos += 1;
			while (data[pos] != OP_TABLE_END)
			{
				luaL_check(pos < size, "bad data, when read key %d:%d", pos, size);
				pos = pop(L, data, pos, size);
				
				luaL_check(pos < size, "bad data, when read value %d:%d", pos, size);
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
			luaL_error(L, "bad ref, %d", where);
			return 0;
		}
		default:
			luaL_error(L, "bad opecode, %d", op);
			return 0;
	}
	return pos;
}

struct buffer* binary_pack (lua_State *L)
{
	struct buffer* buf = buffer_new(256);
	int i, args = lua_gettop(L);
	int ok = 1;
	buffer_addchar(buf, args);
	_wsize = 0;
	for (i = 1; ok && i <= args; ++i)
		ok = push(L, buf, i);
	return buf;
}

int binary_unpack (lua_State *L, const char* data, size_t len)
{
	size_t pos = 1;
	int i, top = lua_gettop(L), args = data[0];
	_rsize = 0;
	for (i = 0; pos && i < args; ++i)
		pos = pop(L, data, pos, len);
	for (i = 0; i < _rsize; ++i)
		luaL_unref(L, LUA_REGISTRYINDEX, _rrefs[i].idx);
	lua_settop(L, top + args);
	return args;
}

static int lib_pack (lua_State *L)
{
	struct buffer* buf = binary_pack(L);
	lua_pushlstring(L, buffer_pointer(buf), buffer_tell(buf));
	buffer_delete(buf);
	return 1;
}


static int lib_unpack (lua_State *L)
{
	size_t len;
	const char *data = luaL_checklstring(L, 1, &len);
	return binary_unpack(L, data, len);
}

static int lib_tostring (lua_State *L)
{
	long n = luaL_checkinteger(L, 1);
	lua_pushlstring(L, &n, sizeof(long));
	return 1;
}

static int lib_tonumber (lua_State *L)
{
	size_t len;
	const char *s = luaL_checklstring(L, 1, &len);
	luaL_check (len == sizeof(long), "bad length %d", len);
	lua_pushnumber(L, *(long*)s);
	return 1;
}

static const struct luaL_Reg lib[] = {
	{"pack", lib_pack},
	{"unpack", lib_unpack},
	{"tostring", lib_tostring},
	{"tonumber", lib_tonumber},
	{NULL, NULL}
};


LUALIB_API int luaopen_binary (lua_State *L) {
	luaL_register(L, "binary", lib);
	
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

