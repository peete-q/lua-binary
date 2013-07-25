
#include <math.h>
#include <malloc.h>

#include "buffer.h"

struct buffer {
	char* data;
	size_t pos;
	size_t size;
};

struct buffer* buffer_new(size_t size)
{
	struct buffer* self = (struct buffer*)malloc(sizeof(struct buffer));
	self->pos = 0;
	self->size = size;
	self->data = (char*)malloc(sizeof(char) * self->size);
	return self;
}

void buffer_delete(struct buffer* self)
{
	free(self->data);
	free(self);
}

void buffer_checksize(struct buffer* self, size_t need)
{
	if (self->pos + need > self->size)
	{
		self->size += need > self->size ? need : self->size;
		self->data = (char*)realloc(self->data, sizeof(char)* self->size);
	}
}

void buffer_addchar(struct buffer* self, char ch)
{
	buffer_checksize(self, 1);
	self->data[self->pos++] = ch;
}

void buffer_addarray(struct buffer* self, const char* data, size_t size)
{
	buffer_checksize(self, size);
	memcpy(self->data + self->pos, data, size);
	self->pos += size;
}

size_t buffer_tell(struct buffer* self)
{
	return self->pos;
}

void buffer_seek(struct buffer* self, size_t pos)
{
	self->pos = pos;
}

size_t buffer_size(struct buffer* self)
{
	return self->size;
}

char* buffer_pointer(struct buffer* self)
{
	return self->data;
}
