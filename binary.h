
#include "buffer.h"

struct buffer* binary_pack (lua_State *L);
int binary_unpack (lua_State *L, const char* data, size_t len);
