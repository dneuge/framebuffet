#ifndef FRAMEBUFFET_COLLECTIONS_H
#define FRAMEBUFFET_COLLECTIONS_H

typedef struct {
    void *_prev;
    void *_next;
  
    void *key;
    unsigned long key_size;

    void *value;
    unsigned long value_size;
} fbcollections_map_entry_t;

#ifdef __cplusplus
extern "C" {
#endif

fbcollections_map_entry_t* fbcollections_map_create();
fbcollections_map_entry_t* fbcollections_map_copy_with_payloads(fbcollections_map_entry_t *entry);
fbcollections_map_entry_t* fbcollections_map_find_key(fbcollections_map_entry_t *entry, void *key, unsigned long key_size);
int fbcollections_map_put(fbcollections_map_entry_t *entry, fbcollections_map_entry_t **old_entry, void *key, unsigned long key_size, void *value, unsigned long value_size);
int fbcollections_map_put_and_free_old_entry_with_payloads(fbcollections_map_entry_t *entry, void *key, unsigned long key_size, void *value, unsigned long value_size);
void fbcollections_map_free_entry_and_payloads(fbcollections_map_entry_t *entry);
void fbcollections_map_free_map_and_payloads(fbcollections_map_entry_t *entry);

#ifdef __cplusplus
}
#endif

#endif
