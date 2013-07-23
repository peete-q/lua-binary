
struct buffer;
struct buffer* buffer_new(size_t size);
void buffer_delete(struct buffer* self);
void buffer_checksize(struct buffer* self, size_t need);
void buffer_addchar(struct buffer* self, char ch);
void buffer_addarray(struct buffer* self, const char* data, size_t size);
size_t buffer_tell(struct buffer* self);
void buffer_seek(struct buffer* self, size_t pos);
size_t buffer_size(struct buffer* self);
char* buffer_pointer(struct buffer* self);

