
#include <math.h>
#include <malloc.h>

#include "buffer.h"

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? x : y)
#endif
#ifndef MAX
#define MAX(x, y) ((x) > (y) ? x : y)
#endif

struct buffer {
	size_t pos;
	size_t size;
	char* ptr;
};

struct buffer* buffer_new(size_t size)
{
	struct buffer* self = (struct buffer*)malloc(sizeof(struct buffer));
	self->pos = 0;
	self->size = size;
	self->ptr = (char*)malloc(sizeof(char) * self->size);
	return self;
}

void buffer_delete(struct buffer* self)
{
	free(self->ptr);
	free(self);
}

void buffer_needsize(struct buffer* self, size_t size)
{
	if (self->pos + size > self->size)
	{
		self->size += MAX(size, self->size);
		self->ptr = (char*)realloc(self->ptr, sizeof(char)* self->size);
	}
}

void buffer_checksize(struct buffer* self, size_t size)
{
	if (size > self->size)
	{
		self->size += size;
		self->ptr = (char*)realloc(self->ptr, sizeof(char)* self->size);
	}
}

void buffer_addchar(struct buffer* self, char ch)
{
	buffer_needsize(self, 1);
	self->ptr[self->pos++] = ch;
}

void buffer_addarray(struct buffer* self, const char* ptr, size_t size)
{
	buffer_needsize(self, size);
	memcpy(self->ptr + self->pos, ptr, size);
	self->pos += size;
}

void buffer_write(struct buffer* self, size_t pos, const char* ptr, size_t size)
{
	buffer_checksize(self, pos + size);
	memcpy(self->ptr + pos, ptr, size);
}

size_t buffer_tell(struct buffer* self)
{
	return self->pos;
}

void buffer_seek(struct buffer* self, size_t pos)
{
	self->pos = pos;
}

void buffer_forward(struct buffer* self, size_t size)
{
	self->pos += size;
}

size_t buffer_size(struct buffer* self)
{
	return self->size;
}

char* buffer_pointer(struct buffer* self)
{
	return self->ptr;
}
