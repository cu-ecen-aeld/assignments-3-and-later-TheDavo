/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary
 * locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing
 * the zero referenced character index if all buffer strings were concatenated
 * end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the
 * byte of the returned aesd_buffer_entry buffptr member corresponding to
 * char_offset.  This value is only set when a matching char_offset is found in
 * aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position
 * described by char_offset, or NULL if this position is not available in the
 * buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(
    struct aesd_circular_buffer *buffer, size_t char_offset,
    size_t *entry_offset_byte_rtn) {

  // handle empty buffer case
  if (!buffer->full && buffer->in_offs == buffer->out_offs) {
    return NULL;
  }

  struct aesd_buffer_entry *current_entry;
  uint8_t index;
  uint8_t buffer_index;
  size_t sum_offsets = 0;
  for (index = 0; index < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; index++) {
    // if the sum offsets and the current entry size is greater than the
    // char_offset, then the current entry is the correct one
    // to look into and return
    //
    // for example
    // entry1: str1, size:4
    // entry2: nextstr, size:7
    // if char_offset is 6
    // sum_offsets + current_entry->size is 4, which is lte to char_offset
    // if statement is false, sum_offsets is set to 4, go to next iter
    // next iter, sum_offsets + current_entry->size is 11, > 6
    // found the entry to return
    // update the entry_offset_byte_rtn and return return_entry
    //
    // < 4  > < 7    >
    //
    // str1\n|ne|x|str\n
    // 0123   45|6|
    //          |^|
    //          |||
    //        01|2| <-- char_offset - sum_offsets


    // use buffer_index so that all calls are relative to buffer->out_offs
    buffer_index =
        (index + buffer->out_offs) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    current_entry = &(buffer->entry[buffer_index]);
    // handle zero reference with - 1
    if (sum_offsets + current_entry->size - 1 >= char_offset) {
      (*entry_offset_byte_rtn) = char_offset - sum_offsets;
      return current_entry;
    }
    sum_offsets += current_entry->size;
  }

  return NULL;
}

/**
 * Adds entry @param add_entry to @param buffer in the location specified in
 * buffer->in_offs. If the buffer was already full, overwrites the oldest entry
 * and advances buffer->out_offs to the new start location. Any necessary
 * locking must be handled by the caller Any memory referenced in @param
 * add_entry must be allocated by and/or must have a lifetime managed by the
 * caller.
 * @return the address/pointer to the entry that was replaced, this can be used
 * to free the buffptr inside the entry by the caller
 */
const char *aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer,
                                    const struct aesd_buffer_entry *add_entry) {
  const char *replaced = NULL;
  // the function brief says that the @param add_entry memory and lifetime
  // will be managed by the caller, this function will not free or handle
  // any memory management

  // update the in_offs and out_offs accordingly

  // if the buffer if full, incremement the out_offs value
  // this was mentioned in the lecture video that this buffer will only
  // read the oldest value, so this function may be the only location
  // that the out_offs is modified
  if (buffer->full) {
    replaced = buffer->entry[buffer->out_offs].buffptr;
    buffer->out_offs =
        ((buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);
  }

  buffer->entry[buffer->in_offs] = *add_entry;

  buffer->in_offs =
      ((buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED);

  // if the offsets are equal to each other, consider the buffer full
  // this works because even if the initial offset values are both zero
  // because before this check the in_offs is always incrememted
  if (buffer->in_offs == buffer->out_offs) {
    buffer->full = true;
  }

  return replaced;
}

/**
 * Initializes the circular buffer described by @param buffer to an empty struct
 */
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer) {
  memset(buffer, 0, sizeof(struct aesd_circular_buffer));
}
