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

#define REFS_SIZE	256

static union {
	int dummy;
	char endian;
} const native = {1};

enum {
	OP_NIL,
	OP_TRUE,
	OP_FALSE,
	OP_ZERO,
	OP_FLOAT,
	OP_INT,
	OP_STRING,
	OP_TABLE,
	OP_TABLE_REF,
	OP_TABLE_DELIMITER,
	OP_TABLE_END,
};

struct userdata_t {
	struct {
		size_t idx;
		size_t pos;
	} rrefs[REFS_SIZE];
	
	struct {
		const void* ptr;
		size_t pos;
	} wrefs[REFS_SIZE];
	
	size_t rcount;
	size_t wcount;
	size_t endian;
};

static void correctbytes (char *b, int size, int endian)
{
	if (endian != native.endian)
	{
		int i = 0;
		while (i < --size)
		{
			char temp = b[i];
			b[i++] = b[size];
			b[size] = temp;
		}
	}
}

static size_t buffer_addint(struct buffer *buf, lua_Integer n)
{
	char *a = (char*)&n;
	size_t i;
	for (i = sizeof(n); i > 0 && a[i - 1] == 0; --i);
	buffer_addarray(buf, a, i);
	return i;
}

static size_t buffer_addfloat(struct buffer *buf, lua_Number n)
{
	float f = n;
	if (n == (lua_Number)f)
	{
		buffer_addarray(buf, &f, sizeof(f));
		return sizeof(f);
	}
	buffer_addarray(buf, &n, sizeof(n));
	return sizeof(n);
}

static struct userdata_t *getuserdata(lua_State *L)
{
	void *ptr;
	int top = lua_gettop(L);
	lua_getglobal(L, "binary");
	lua_getfield(L, -1, "USERDATA");
	ptr = lua_touserdata(L, -1);
	lua_settop(L, top);
	return (struct userdata_t*) ptr;
}

static int push(lua_State *L, struct buffer* buf, int idx, struct userdata_t *userdata)
{
	int top = lua_gettop(L);
	int type = lua_type(L, idx);
	switch(type)
	{
		case LUA_TNIL:
			buffer_addchar(buf, OP_NIL);
			break;
		case LUA_TBOOLEAN:
			buffer_addchar(buf, lua_toboolean(L, idx) ? OP_TRUE : OP_FALSE);
			break;
		case LUA_TNUMBER:
		{
			lua_Number n = lua_tonumber(L, idx);
			if (n == 0)
				buffer_addchar(buf, OP_ZERO);
			else if (floor(n) == n)
			{
				size_t size, pos = buffer_tell(buf);
				buffer_addchar(buf, OP_INT);
				size = buffer_addint(buf, n);
				*buffer_at(buf, pos) |= size << 4;
			}
			else
			{
				size_t size, pos = buffer_tell(buf);
				buffer_addchar(buf, OP_FLOAT);
				size = buffer_addfloat(buf, n);
				*buffer_at(buf, pos) |= size << 4;
			}
			break;
		}
		case LUA_TSTRING:
		{
			size_t len;
			const char* str = lua_tolstring(L, idx, &len);
			size_t size, pos = buffer_tell(buf);
			buffer_addchar(buf, OP_STRING);
			size = buffer_addint(buf, len);
			*buffer_at(buf, pos) |= size << 4;
			buffer_addarray(buf, str, len);
			break;
		}
		case LUA_TTABLE:
		{
			const void *ptr = lua_topointer(L, idx);
			size_t i;
			for (i = 0; i < userdata->wcount; ++i)
			{
				if (userdata->wrefs[i].ptr == ptr)
				{
					buffer_addchar(buf, OP_TABLE_REF);
					buffer_addchar(buf, userdata->wrefs[i].pos);
					goto end;
				}
			}
			luaL_check(userdata->wcount < REFS_SIZE, "table refs overflow %d", REFS_SIZE);
			userdata->wrefs[userdata->wcount].ptr = ptr;
			userdata->wrefs[userdata->wcount++].pos = buffer_tell(buf);
			
			buffer_addchar(buf, OP_TABLE);
			lua_pushnil(L);
			i = 1;
			while (lua_next(L, idx))
			{
				if (lua_isnumber(L, -2) && lua_tonumber(L, -2) == i++)
				{
					push(L, buf, lua_gettop(L), userdata);
					lua_pop(L, 1);
				}
				else break;
			}
			buffer_addchar(buf, OP_TABLE_DELIMITER);
			if (lua_gettop(L) > top)
			{
				do
				{
					push(L, buf, lua_gettop(L) - 1, userdata);
					push(L, buf, lua_gettop(L), userdata);
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

static int pop(lua_State *L, const char *data, size_t pos, size_t size, struct userdata_t *userdata)
{
	int op = data[pos++];
	switch(op & 0x0f)
	{
		case OP_NIL:
			lua_pushnil(L);
			break;
		case OP_TRUE:
			lua_pushboolean(L, 1);
			break;
		case OP_FALSE:
			lua_pushboolean(L, 0);
			break;
		case OP_ZERO:
			lua_pushnumber(L, 0);
			break;
		case OP_INT:
		{
			int len = (op & 0xf0) >> 4;
			lua_Integer n = 0;
			memcpy(&n, data + pos, len);
			correctbytes((char*)&n, sizeof(lua_Integer), userdata->endian);
			lua_pushnumber(L, n);
			pos += len;
			break;
		}
		case OP_FLOAT:
		{
			int len = (op & 0xf0) >> 4;
			lua_Number n = 0;
			memcpy(&n, data + pos, len);
			correctbytes((char*)&n, sizeof(lua_Number), userdata->endian);
			lua_pushnumber(L, n);
			pos += len;
			break;
		}
		case OP_STRING:
		{
			int len = (op & 0xf0) >> 4;
			lua_Integer n = 0;
			memcpy(&n, data + pos, len);
			correctbytes((char*)&n, sizeof(lua_Integer), userdata->endian);
			pos += len;
			lua_pushlstring(L, data + pos, n);
			pos += n;
			break;
		}
		case OP_TABLE:
		{
			size_t i;
			lua_newtable(L);
			lua_pushvalue(L, -1);
			luaL_check(userdata->rcount < REFS_SIZE, "table refs overflow %d", REFS_SIZE);
			userdata->rrefs[userdata->rcount].pos = pos - 1;
			userdata->rrefs[userdata->rcount++].idx = luaL_ref(L, LUA_REGISTRYINDEX);
			for (i = 1; data[pos] != OP_TABLE_DELIMITER; ++i)
			{
				luaL_check(pos < size, "bad data, when read index %d:%d", pos, size);
				pos = pop(L, data, pos, size, userdata);
				lua_rawseti(L, -2, i);
			}
			pos += 1;
			while (data[pos] != OP_TABLE_END)
			{
				luaL_check(pos < size, "bad data, when read key %d:%d", pos, size);
				pos = pop(L, data, pos, size, userdata);
				
				luaL_check(pos < size, "bad data, when read value %d:%d", pos, size);
				pos = pop(L, data, pos, size, userdata);
				lua_settable(L, -3);
			}
			pos += 1;
			break;
		}
		case OP_TABLE_REF:
		{
			size_t i, where = data[pos++];
			for (i = 0; i < userdata->rcount; ++i)
			{
				if (userdata->rrefs[i].pos == where)
				{
					lua_rawgeti(L, LUA_REGISTRYINDEX, userdata->rrefs[i].idx);
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

int binary_pack (lua_State *L, struct buffer* buf, size_t idx, size_t num)
{
	struct userdata_t *userdata = getuserdata(L);
	userdata->wcount = 0;
	buffer_addchar(buf, native.endian);
	buffer_addchar(buf, num);
	num += idx;
	for (; idx < num; ++idx)
		push(L, buf, idx, userdata);
	return 0;
}

int binary_unpack (lua_State *L, const char* data, size_t len)
{
	size_t pos = 2;
	int i, top = lua_gettop(L), args = data[1];
	struct userdata_t *userdata = getuserdata(L);
	userdata->rcount = 0;
	userdata->endian = data[0];
	for (i = 0; i < args; ++i)
		pos = pop(L, data, pos, len, userdata);
	for (i = 0; i < userdata->rcount; ++i)
		luaL_unref(L, LUA_REGISTRYINDEX, userdata->rrefs[i].idx);
	lua_settop(L, top + args);
	return args;
}

static int lib_pack (lua_State *L)
{
	struct buffer* buf = buffer_new(256);
	buffer_seek(buf, 0);
	binary_pack(L, buf, 1, lua_gettop(L));
	lua_pushlstring(L, buffer_pointer(buf), buffer_tell(buf));
	buffer_delete(buf);
	return 1;
}

static int lib_unpack (lua_State *L)
{
	size_t len;
	const char *s = luaL_checklstring(L, 1, &len);
	return binary_unpack(L, s, len);
}

static int lib_tostring (lua_State *L)
{
	size_t n = luaL_checkinteger(L, 1);
	lua_pushlstring(L, &n, sizeof(size_t));
	return 1;
}

static int lib_tonumber (lua_State *L)
{
	size_t len;
	const char *s = luaL_checklstring(L, 1, &len);
	luaL_check (len == sizeof(size_t), "bad length %d", len);
	lua_pushnumber(L, *(size_t*)s);
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
	
	lua_pushstring(L, "USERDATA");
	lua_newuserdata(L, sizeof(struct userdata_t));
	lua_settable(L, -3);
	
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

