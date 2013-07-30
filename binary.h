
#include "buffer.h"

int binary_pack (lua_State *L, struct buffer* buf, size_t idx, size_t num);
int binary_unpack (lua_State *L, const char* data, size_t len);