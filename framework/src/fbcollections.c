#include <stdlib.h>
#include <string.h>

#include "fbcollections.h"

/* For now, the map really is just a list of key-value pairs.
 * The map head never holds any payload, it only points to the first actual entry.
 */

fbcollections_map_entry_t* fbcollections_map_create() {
    // NOTE: this is also used to allocate an entry in put function
    fbcollections_map_entry_t *map = malloc(sizeof(fbcollections_map_entry_t));
  
    if (map != NULL) {
        memset(map, 0, sizeof(fbcollections_map_entry_t));
    }
  
    return map;
}

static fbcollections_map_entry_t* fbcollections_map_head(fbcollections_map_entry_t *entry) {
    while (entry->_prev != NULL) {
        entry = (fbcollections_map_entry_t*) entry->_prev;
    }
    return entry;
}

static fbcollections_map_entry_t* fbcollections_map_tail(fbcollections_map_entry_t *entry) {
    while (entry->_next != NULL) {
        entry = (fbcollections_map_entry_t*) entry->_next;
    }
    return entry;
}

fbcollections_map_entry_t* fbcollections_map_copy_with_payloads(fbcollections_map_entry_t *entry) {
    fbcollections_map_entry_t *out = fbcollections_map_create();
  
    entry = (fbcollections_map_entry_t*) fbcollections_map_head(entry)->_next;
    while (entry != NULL) {
        if (fbcollections_map_find_key(out, entry->key, entry->key_size)) {
            // Key already exists - this should not happen as long as only this library's functions
            // were used to work on the map structure and callers did ensure we were thread-safe all the time.
            // Nevertheless, in case we had a duplicate entry on the data structure, find_key will always return
            // the entry closest to map head which would be different on the copied map as compared to the original
            // one if we put a later entry onto the copied map.
            continue;
        }
    
        void *copied_key = NULL;
        unsigned long key_size = entry->key_size;
        if (key_size > 0) {
            copied_key = malloc(key_size);
            memcpy(copied_key, entry->key, key_size);
        }

        void *copied_value = NULL;
        unsigned long value_size = entry->value_size;
        if (value_size > 0) {
            copied_value = malloc(value_size);
            memcpy(copied_value, entry->value, value_size);
        }

        // ... just in case we *still* encounter a duplicate key we need to deallocate memory correctly
        // as the reference would otherwise simply be lost and we end up with a memory leak
        fbcollections_map_put_and_free_old_entry_with_payloads(out, copied_key, key_size, copied_value, value_size);

        entry = (fbcollections_map_entry_t*) entry->_next;
    }

    return out;
}

fbcollections_map_entry_t* fbcollections_map_find_key(fbcollections_map_entry_t *entry, void *key, unsigned long key_size) {
    if (key_size == 0) {
        // memcpy would always return 0 and in general we have nothing to compare
        return NULL;
    }
  
    entry = (fbcollections_map_entry_t*) fbcollections_map_head(entry)->_next;
    while (entry != NULL) {
        if (key_size == entry->key_size && memcmp(key, entry->key, key_size) == 0) {
            return entry;
        }
    
        entry = (fbcollections_map_entry_t*) entry->_next;
    }

    return NULL;
}

int fbcollections_map_put(fbcollections_map_entry_t *entry, fbcollections_map_entry_t **old_entry, void *key, unsigned long key_size, void *value, unsigned long value_size) {
    // NOTE: we actually need to accept zero-length keys as we need to free the value later

    *old_entry = NULL;

    fbcollections_map_entry_t* new = fbcollections_map_create();
    if (new == NULL) {
        // memory allocation failed, we need to indicate an error and must not continue
        return -1;
    }
  
    *old_entry = fbcollections_map_find_key(entry, key, key_size);
    if (*old_entry != NULL) {
        // remove from map by relinking surrounding entries
        ((fbcollections_map_entry_t*) (*old_entry)->_prev)->_next = (*old_entry)->_next;
    
        if ((*old_entry)->_next != NULL) {
            ((fbcollections_map_entry_t*) (*old_entry)->_next)->_prev = (*old_entry)->_prev;
        }

        (*old_entry)->_prev = NULL;
        (*old_entry)->_next = NULL;
    }

    new->key = key;
    new->key_size = key_size;
    new->value = value;
    new->value_size = value_size;
  
    fbcollections_map_entry_t* prev = fbcollections_map_tail(entry);
    new->_prev = prev;
    prev->_next = new;

    return 0;
}

int fbcollections_map_put_and_free_old_entry_with_payloads(fbcollections_map_entry_t *entry, void *key, unsigned long key_size, void *value, unsigned long value_size) {
    fbcollections_map_entry_t *old_entry = NULL;
    if (fbcollections_map_put(entry, &old_entry, key, key_size, value, value_size) < 0) {
        // memory allocation failed, we need to indicate an error
        return -1;
    }
  
    if (old_entry != NULL) {
        fbcollections_map_free_entry_and_payloads(old_entry);
        return 1;
    }
  
    return 0;
}

void fbcollections_map_free_entry_and_payloads(fbcollections_map_entry_t *entry) {
    if (entry == NULL) {
        return;
    }

    if (entry->key != NULL) {
        free(entry->key);
    }

    if (entry->value != NULL) {
        free(entry->value);
    }
  
    free(entry);
}

void fbcollections_map_free_map_and_payloads(fbcollections_map_entry_t *entry) {
    fbcollections_map_entry_t *prev = NULL;

    if (entry == NULL) {
        return;
    }
  
    entry = fbcollections_map_tail(entry);
    while (entry != NULL) {
        prev = entry->_prev;
        fbcollections_map_free_entry_and_payloads(entry);
        entry = prev;
    }
}

fbcollections_map_entry_t* fbcollections_map_remove(fbcollections_map_entry_t *entry, void *key, unsigned long key_size) {
    fbcollections_map_entry_t *remove_entry = fbcollections_map_find_key(entry, key, key_size);

    if (remove_entry) {
        fbcollections_map_entry_t *prev = remove_entry->_prev;
        fbcollections_map_entry_t *next = remove_entry->_next;

        if (prev) {
            prev->_next = next;
        }
        
        if (next) {
            next->_prev = prev;
        }

        remove_entry->_prev = NULL;
        remove_entry->_next = NULL;
    }
    
    return remove_entry;
}
