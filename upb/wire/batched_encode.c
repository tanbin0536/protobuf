// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google LLC.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

// We encode backwards, to avoid pre-computing lengths (one-pass encode).


#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#include "upb/base/descriptor_constants.h"
#include "upb/mini_table/sub.h"
#include "upb/wire/batched.h"
#include "upb/wire/internal/constants.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.h"
#include "upb/message/array.h"
#include "upb/message/accessors.h"
#include "upb/message/internal/array.h"
#include "upb/message/internal/map_sorter.h"
#include "upb/message/message.h"
#include "upb/mini_table/field.h"
#include "upb/mini_table/internal/field.h"
#include "upb/mini_table/internal/message.h"
#include "upb/mini_table/message.h"
#include "upb/wire/encode.h"

// Must be last.
#include "upb/port/def.inc"

typedef struct {
  upb_EncodeStatus status;
  jmp_buf err;
  upb_Arena* arena;
  char* buf;
  char* end;
  int options;
  int depth;
  _upb_mapsorter sorter;
} upb_encstate;

UPB_NORETURN static void upb_BatchEncoder_Error(upb_encstate* e,
                                                upb_EncodeStatus s) {
  UPB_ASSERT(s != kUpb_EncodeStatus_Ok);
  e->status = s;
  UPB_LONGJMP(e->err, 1);
}

/*
static void encode_fixedarray(upb_encstate* e, const upb_Array* arr,
                              size_t elem_size, uint32_t tag) {
  size_t bytes = upb_Array_Size(arr) * elem_size;
  const char* data = upb_Array_DataPtr(arr);
  const char* ptr = data + bytes - elem_size;

  if (tag || !upb_IsLittleEndian()) {
    while (true) {
      if (elem_size == 4) {
        uint32_t val;
        memcpy(&val, ptr, sizeof(val));
        val = upb_BigEndian32(val);
        encode_bytes(e, &val, elem_size);
      } else {
        UPB_ASSERT(elem_size == 8);
        uint64_t val;
        memcpy(&val, ptr, sizeof(val));
        val = upb_BigEndian64(val);
        encode_bytes(e, &val, elem_size);
      }

      if (tag) encode_varint(e, tag);
      if (ptr == data) break;
      ptr -= elem_size;
    }
  } else {
    encode_bytes(e, data, bytes);
  }
}

static char* upb_BatchEncoder_EncodeMessage(upb_encstate* e, char* ptr, const upb_Message* msg,
                           const upb_MiniTable* m);

static char* encode_TaggedMessagePtr(upb_encstate* e, char* ptr,
                                    upb_TaggedMessagePtr tagged,
                                    const upb_MiniTable* m, size_t* size) {
  if (upb_TaggedMessagePtr_IsEmpty(tagged)) {
    m = UPB_PRIVATE(_upb_MiniTable_Empty)();
  }
  return upb_BatchEncoder_EncodeMessage(
      e, ptr, UPB_PRIVATE(_upb_TaggedMessagePtr_GetMessage)(tagged), m);
}

*/

static uint16_t* upb_BatchedEncoder_EncodeScalar(
    upb_encstate* e, const char* field_mem, const upb_MiniTableSub* subs,
    const upb_MiniTableField* f, uint16_t tag, uint16_t* tag_ptr,
    char** data_pp) {
  switch (f->UPB_PRIVATE(descriptortype)) {
    case kUpb_FieldType_Double:
    case kUpb_FieldType_Int64:
    case kUpb_FieldType_UInt64:
    case kUpb_FieldType_SFixed64:
    case kUpb_FieldType_Fixed64:
    case kUpb_FieldType_SInt64:
      return upb_BatchedEncoder_EncodeBytes(e, field_mem, 4, tag, tag_ptr,
                                            data_pp);
    case kUpb_FieldType_Float:
    case kUpb_FieldType_UInt32:
    case kUpb_FieldType_Int32:
    case kUpb_FieldType_Enum:
    case kUpb_FieldType_Fixed32:
    case kUpb_FieldType_SFixed32:
    case kUpb_FieldType_SInt32:
      return upb_BatchedEncoder_EncodeBytes(e, field_mem, 8, tag, tag_ptr,
                                            data_pp);
    case kUpb_FieldType_Bool:
      return upb_BatchedEncoder_EncodeBytes(e, field_mem, 1, tag, tag_ptr,
                                            data_pp);
    case kUpb_FieldType_String:
    case kUpb_FieldType_Bytes: {
      upb_StringView view = *(upb_StringView*)field_mem;
      return upb_BatchedEncoder_EncodeFixedBytes(e, view.data, view.size, tag,
                                                 tag_ptr, data_pp);
    }
    default:
      UPB_UNREACHABLE();
  }
#undef CASE

  encode_tag(e, upb_MiniTableField_Number(f), wire_type);
}
/*

static char* encode_array(upb_encstate* e, char* ptr, const upb_Message* msg,
                          const upb_MiniTableSub* subs,
                          const upb_MiniTableField* f) {
  const upb_Array* arr = *UPB_PTR_AT(msg, f->UPB_PRIVATE(offset), upb_Array*);
  bool packed = upb_MiniTableField_IsPacked(f);
  size_t pre_len = e->limit - e->ptr;

  if (arr == NULL || upb_Array_Size(arr) == 0) {
    return;
  }

#define VARINT_CASE(ctype, encode)                                         \
  {                                                                        \
    const ctype* start = upb_Array_DataPtr(arr);                           \
    const ctype* ptr = start + upb_Array_Size(arr);                        \
    uint32_t tag =                                                         \
        packed ? 0 : (f->UPB_PRIVATE(number) << 3) | kUpb_WireType_Varint; \
    do {                                                                   \
      ptr--;                                                               \
      encode_varint(e, encode);                                            \
      if (tag) encode_varint(e, tag);                                      \
    } while (ptr != start);                                                \
  }                                                                        \
  break;

#define TAG(wire_type) (packed ? 0 : (f->UPB_PRIVATE(number) << 3 | wire_type))

  switch (f->UPB_PRIVATE(descriptortype)) {
    case kUpb_FieldType_Double:
      encode_fixedarray(e, arr, sizeof(double), TAG(kUpb_WireType_64Bit));
      break;
    case kUpb_FieldType_Float:
      encode_fixedarray(e, arr, sizeof(float), TAG(kUpb_WireType_32Bit));
      break;
    case kUpb_FieldType_SFixed64:
    case kUpb_FieldType_Fixed64:
      encode_fixedarray(e, arr, sizeof(uint64_t), TAG(kUpb_WireType_64Bit));
      break;
    case kUpb_FieldType_Fixed32:
    case kUpb_FieldType_SFixed32:
      encode_fixedarray(e, arr, sizeof(uint32_t), TAG(kUpb_WireType_32Bit));
      break;
    case kUpb_FieldType_Int64:
    case kUpb_FieldType_UInt64:
      VARINT_CASE(uint64_t, *ptr);
    case kUpb_FieldType_UInt32:
      VARINT_CASE(uint32_t, *ptr);
    case kUpb_FieldType_Int32:
    case kUpb_FieldType_Enum:
      VARINT_CASE(int32_t, (int64_t)*ptr);
    case kUpb_FieldType_Bool:
      VARINT_CASE(bool, *ptr);
    case kUpb_FieldType_SInt32:
      VARINT_CASE(int32_t, encode_zz32(*ptr));
    case kUpb_FieldType_SInt64:
      VARINT_CASE(int64_t, encode_zz64(*ptr));
    case kUpb_FieldType_String:
    case kUpb_FieldType_Bytes: {
      const upb_StringView* start = upb_Array_DataPtr(arr);
      const upb_StringView* ptr = start + upb_Array_Size(arr);
      do {
        ptr--;
        encode_bytes(e, ptr->data, ptr->size);
        encode_varint(e, ptr->size);
        encode_tag(e, upb_MiniTableField_Number(f), kUpb_WireType_Delimited);
      } while (ptr != start);
      return;
    }
    case kUpb_FieldType_Group: {
      const upb_TaggedMessagePtr* start = upb_Array_DataPtr(arr);
      const upb_TaggedMessagePtr* ptr = start + upb_Array_Size(arr);
      const upb_MiniTable* subm =
          upb_MiniTableSub_Message(subs[f->UPB_PRIVATE(submsg_index)]);
      if (--e->depth == 0) encode_err(e, kUpb_EncodeStatus_MaxDepthExceeded);
      do {
        size_t size;
        ptr--;
        encode_tag(e, upb_MiniTableField_Number(f), kUpb_WireType_EndGroup);
        encode_TaggedMessagePtr(e, *ptr, subm, &size);
        encode_tag(e, upb_MiniTableField_Number(f), kUpb_WireType_StartGroup);
      } while (ptr != start);
      e->depth++;
      return;
    }
    case kUpb_FieldType_Message: {
      const upb_TaggedMessagePtr* start = upb_Array_DataPtr(arr);
      const upb_TaggedMessagePtr* ptr = start + upb_Array_Size(arr);
      const upb_MiniTable* subm =
          upb_MiniTableSub_Message(subs[f->UPB_PRIVATE(submsg_index)]);
      if (--e->depth == 0) encode_err(e, kUpb_EncodeStatus_MaxDepthExceeded);
      do {
        size_t size;
        ptr--;
        encode_TaggedMessagePtr(e, *ptr, subm, &size);
        encode_varint(e, size);
        encode_tag(e, upb_MiniTableField_Number(f), kUpb_WireType_Delimited);
      } while (ptr != start);
      e->depth++;
      return;
    }
  }
#undef VARINT_CASE

  if (packed) {
    encode_varint(e, e->limit - e->ptr - pre_len);
    encode_tag(e, upb_MiniTableField_Number(f), kUpb_WireType_Delimited);
  }
}

static void encode_mapentry(upb_encstate* e, uint32_t number,
                            const upb_MiniTable* layout,
                            const upb_MapEntry* ent) {
  const upb_MiniTableField* key_field = upb_MiniTable_MapKey(layout);
  const upb_MiniTableField* val_field = upb_MiniTable_MapValue(layout);
  size_t pre_len = e->limit - e->ptr;
  size_t size;
  encode_scalar(e, &ent->v, layout->UPB_PRIVATE(subs), val_field);
  encode_scalar(e, &ent->k, layout->UPB_PRIVATE(subs), key_field);
  size = (e->limit - e->ptr) - pre_len;
  encode_varint(e, size);
  encode_tag(e, number, kUpb_WireType_Delimited);
}

static void encode_map(upb_encstate* e, const upb_Message* msg,
                       const upb_MiniTableSub* subs,
                       const upb_MiniTableField* f) {
  const upb_Map* map = *UPB_PTR_AT(msg, f->UPB_PRIVATE(offset), const upb_Map*);
  const upb_MiniTable* layout =
      upb_MiniTableSub_Message(subs[f->UPB_PRIVATE(submsg_index)]);
  UPB_ASSERT(upb_MiniTable_FieldCount(layout) == 2);

  if (!map || !upb_Map_Size(map)) return;

  if (e->options & kUpb_EncodeOption_Deterministic) {
    _upb_sortedmap sorted;
    _upb_mapsorter_pushmap(
        &e->sorter, layout->UPB_PRIVATE(fields)[0].UPB_PRIVATE(descriptortype),
        map, &sorted);
    upb_MapEntry ent;
    while (_upb_sortedmap_next(&e->sorter, map, &sorted, &ent)) {
      encode_mapentry(e, upb_MiniTableField_Number(f), layout, &ent);
    }
    _upb_mapsorter_popmap(&e->sorter, &sorted);
  } else {
    intptr_t iter = UPB_STRTABLE_BEGIN;
    upb_StringView key;
    upb_value val;
    while (upb_strtable_next2(&map->table, &key, &val, &iter)) {
      upb_MapEntry ent;
      _upb_map_fromkey(key, &ent.k, map->key_size);
      _upb_map_fromvalue(val, &ent.v, map->val_size);
      encode_mapentry(e, upb_MiniTableField_Number(f), layout, &ent);
    }
  }
}
*/

/*
static bool encode_ShouldEncodeImplicitPresence(const upb_Message* msg,
                                                const upb_MiniTableField* f) {
  const void* mem = UPB_PTR_AT(msg, f->UPB_PRIVATE(offset), void);
  switch (UPB_PRIVATE(_upb_MiniTableField_GetRep)(f)) {
    case kUpb_FieldRep_1Byte: {
      char ch;
      memcpy(&ch, mem, 1);
      return ch != 0;
    }
    case kUpb_FieldRep_4Byte: {
      uint32_t u32;
      memcpy(&u32, mem, 4);
      return u32 != 0;
    }
    case kUpb_FieldRep_8Byte: {
      uint64_t u64;
      memcpy(&u64, mem, 8);
      return u64 != 0;
    }
    case kUpb_FieldRep_StringView: {
      const upb_StringView* str = (const upb_StringView*)mem;
      return str->size != 0;
    }
    default:
      UPB_UNREACHABLE();
  }
}
*/

/*
static void encode_field(upb_encstate* e, char* ptr, const upb_Message* msg,
                         const upb_MiniTableSub* subs,
                         const upb_MiniTableField* field) {
  switch (UPB_PRIVATE(_upb_MiniTableField_Mode)(field)) {
    case kUpb_FieldMode_Array:
      return encode_array(e, ptr, msg, subs, field);
    case kUpb_FieldMode_Map:
      return encode_map(e, ptr, msg, subs, field);
    case kUpb_FieldMode_Scalar:
      return encode_scalar(e, ptr,
                           UPB_PTR_AT(msg, field->UPB_PRIVATE(offset), void),
                           subs, field);
    default:
      UPB_UNREACHABLE();
  }
}


static char* encode_ext(upb_encstate* e, char* ptr, const upb_Extension* ext) {
  encode_field(e, ptr, (upb_Message*)&ext->data, &ext->ext->UPB_PRIVATE(sub),
               &ext->ext->UPB_PRIVATE(field));
}
*/

static char* upb_BatchEncoder_Reserve(upb_encstate* e, char* ptr, size_t size) {
  if (e->end - ptr >= size) return ptr;
  size_t need = ptr - e->buf + size;
  size_t old_size = e->end - e->buf;
  size_t new_size = UPB_MAX(128, old_size * 2);
  while (new_size < need) need *= 2;
  size_t ofs = ptr - e->buf;
  e->buf = upb_Arena_Realloc(e->arena, e->buf, old_size, new_size);
  if (e->buf == NULL) encode_err(e, kUpb_EncodeStatus_OutOfMemory);
  e->end = e->buf + new_size;
  return e->buf + ofs;
}

static uint16_t* upb_BatchedEncoder_EncodeField(upb_encstate* e,
                                           const upb_Message* msg,
                                           const upb_MiniTableSub* subs,
                                           const upb_MiniTableField* f,
                                           uint16_t tag, uint16_t* tag_ptr,
                                           char** data_pp) {
  switch (UPB_PRIVATE(_upb_MiniTableField_Mode)(f)) {
    case kUpb_FieldMode_Array:
      return upb_BatchedEncoder_EncodeArray(e, msg, subs, f, tag, tag_ptr,
                                            data_pp);
    case kUpb_FieldMode_Map:
      UPB_UNREACHABLE();
      // return upb_BatchedEncoder_EncodeMap(e, msg, subs, f, tag, tag_ptr, data_pp);
    case kUpb_FieldMode_Scalar:
      return upb_BatchedEncoder_EncodeScalar(
          e, UPB_PTR_AT(msg, f->UPB_PRIVATE(offset), void), subs, f, tag,
          tag_ptr, data_pp);
    default:
      UPB_UNREACHABLE();
  }
}

static uint16_t* upb_BatchEncoder_EncodeBatchPart(
    upb_encstate* e, const upb_Message* msg, const upb_MiniTable* m,
    const uint16_t* batch, int batch_size, uint16_t* tag_ptr, char** data_pp) {
  for (int i = 0; i < batch_size; i++) {
    const upb_MiniTableField* f = upb_MiniTable_GetFieldByIndex(m, batch[i]);
    uint16_t tag;
    uint32_t num = upb_MiniTableField_Number(f);
    if (UPB_UNLIKELY(num >= kUpb_BigFieldNumber)) {
      // Unusual: large field.
      tag = kUpb_BigFieldNumber << kUpb_FieldNumberShift;
      uint32_t num = upb_MiniTableField_Number(f);
      tag_ptr = upb_BatchEncoder_WriteData(e, tag_ptr, data_pp, &num, 4);
    } else {
      // Common: small field.
      tag = num << kUpb_FieldNumberShift;
    }

    upb_BatchedEncoder_EncodeField(e, msg, m, f, tag, tag_pp, data_pp);
  }
}

static char* upb_BatchEncoder_EncodeBatch(upb_encstate* e, char* ptr,
                                          const upb_Message* msg,
                                          const upb_MiniTable* m,
                                          ...) {
  va_list ap1, ap2;
  va_start(ap1, m);
  va_copy(ap2, ap1);
  int batch_size = 0;

  while (true) {
    const uint16_t* batch = va_arg(ap1, const uint16_t*);
    if (!batch) break;
    batch_size += va_arg(ap1, int);
  }

  if (batch_size == 0) return ptr;
  UPB_ASSERT(batch_size < kUpb_MaxBatch);

  ptr = upb_BatchEncoder_Reserve(e, ptr, batch_size * 8);

  uint16_t* tag_ptr = (uint16_t*)ptr;
  char* data_ptr = (char*)(tag_ptr + batch_size);

  while (true) {
    const uint16_t* batch = va_arg(ap1, const uint16_t*);
    if (!batch) break;
    int batch_size = va_arg(ap1, int);
    if (batch_size == 0) continue;
    tag_ptr = upb_BatchEncoder_EncodeBatchPart(e, msg, m, batch, batch_size,
                                              tag_ptr, &data_ptr);
  }

  return data_ptr;
}

static char* upb_BatchEncoder_EncodeBatchIfFull(upb_encstate* e, char* ptr,
                                                const upb_Message* msg,
                                                const upb_MiniTable* m,
                                                const uint16_t* batch,
                                                int* batch_size) {
  if (UPB_LIKELY(*batch_size != kUpb_MaxBatch)) return ptr;
  ptr = upb_BatchEncoder_EncodeBatch(e, ptr, msg, m, batch, *batch_size);
  *batch_size = 0;
  return ptr;
}

static bool encode_IsPresentFieldBranchless(const upb_Message* msg,
                                             const upb_MiniTableField* f) {
  int16_t presence = f->presence;
  uint16_t hasbit_word_offset = (uint16_t)f->presence / 32;
  uint16_t oneof_case_offset = ~presence;
  bool is_oneof = presence < 0;
  uint16_t read_from = is_oneof ? oneof_case_offset : hasbit_word_offset;
  uint32_t val;
  memcpy(&val, UPB_PTR_AT(msg, read_from, char*), sizeof(val));
  bool hasbit_present = val & ((uint16_t)presence % 32);
  return presence == 0
             ? false
             : (is_oneof ? val == f->UPB_PRIVATE(number) : hasbit_present);
}

static bool upb_IsPresentHasbitFieldBranchless(
    const upb_Message* msg, const upb_MiniTableField* f) {
  // This is like _upb_Message_GetHasbit() except it tolerates the
  //   f->presence == 0 case (no hasbit)
  // and returns false in that case.
  uint16_t hasbit_byte_offset = (uint16_t)f->presence / 8;
  uint32_t val;
  memcpy(&val, UPB_PTR_AT(msg, hasbit_byte_offset, char*), sizeof(val));
  return f->presence <= 0 ? false : val & ((uint16_t)f->presence % 8);
}

bool upb_IsNonZeroImplicitPresenceBranchless(const upb_Message* msg,
                                             const upb_MiniTableField* f) {
  bool is_implicit_presence =
      f->presence == 0 &&
      UPB_PRIVATE(_upb_MiniTableField_Mode)(f) == kUpb_FieldMode_Scalar;
  uint64_t val;
  memcpy(&val, UPB_PTR_AT(msg, f->UPB_PRIVATE(offset), char), 8);  // Overread.
  const widths = (8 << 24) | (
  uint64_t mask = ;
  return (val & mask) != 0;
}

static bool upb_IsNonEmptyArrayBranchless(const upb_Message* msg,
                                          const upb_MiniTableField* f) {
  bool is_array =
      UPB_PRIVATE(_upb_MiniTableField_Mode)(f) == kUpb_FieldMode_Array;
  const upb_Array* msg_arr_ptr;
  memcpy(&msg_arr_ptr, UPB_PTR_AT(msg, f->UPB_PRIVATE(offset), upb_Array*),
          sizeof(msg_arr_ptr));
  static const upb_Array empty_array = {0, 0, 0};
  const upb_Array* arr_ptr = is_array ? msg_arr_ptr : &empty_array;
  return upb_Array_Size(arr_ptr) > 0;
}

static char* upb_BatchEncoder_EncodeMessage(upb_encstate* e, char* ptr,
                                            const upb_Message* msg,
                                            const upb_MiniTable* m) {
  if (e->options & kUpb_EncodeOption_CheckRequired) {
    if (m->UPB_PRIVATE(required_count)) {
      if (!UPB_PRIVATE(_upb_Message_IsInitializedShallow)(msg, m)) {
        upb_BatchEncoder_Error(e, kUpb_EncodeStatus_MissingRequired);
      }
    }
  }

  /* NYI: Unknown Fields
  if ((e->options & kUpb_EncodeOption_SkipUnknown) == 0) {
    size_t unknown_size;
    const char* unknown = upb_Message_GetUnknown(msg, &unknown_size);

    if (unknown) {
      encode_bytes(e, unknown, unknown_size);
    }
  }
  */

  uint16_t primitive_batch[kUpb_MaxBatch];
  uint16_t string_batch[kUpb_MaxBatch];
  int primitive_batch_size = 0;
  int string_batch_size = 0;

  for (int i = 0, n = upb_MiniTable_FieldCount(m); i < n; i++) {
    const upb_MiniTableField* f = upb_MiniTable_GetFieldByIndex(m, i);
    bool is_present;
    if (upb_MiniTableField_IsInOneof(f)) {
      // Unusual: oneof
      is_present = upb_Message_HasBaseField(msg, f);
    } else {
      is_present = upb_IsPresentHasbitFieldBranchless(msg, f);
    }
    primitive_batch[primitive_batch_size] = (uint16_t)i;
    string_batch[string_batch_size] = (uint16_t)i;
    primitive_batch_size += is_present && upb_MiniTableField_IsPrimitive(f);
    string_batch_size += is_present && upb_MiniTableField_IsString(f);
    ptr = upb_BatchEncoder_EncodeBatchIfFull(e, ptr, msg, m, primitive_batch,
                                             &primitive_batch_size);
    ptr = upb_BatchEncoder_EncodeBatchIfFull(e, ptr, msg, m, string_batch,
                                             &string_batch_size);
  }

  uint16_t implicit_batch[kUpb_MaxBatch];
  uint16_t scalar_array_batch[kUpb_MaxBatch];
  int implicit_batch_size = 0;
  int scalar_array_batch_size = 0;

  for (int i = 0, n = upb_MiniTable_FieldCount(m); i < n; i++) {
    const upb_MiniTableField* f = upb_MiniTable_GetFieldByIndex(m, i);
    if (UPB_PRIVATE(_upb_MiniTableField_Mode)(f) == kUpb_FieldMode_Map) {
      // Unusual: map.
      //
      // TODO: unify map/array representations so that both have size at the
      // same offset, so we can test whether they are empty or not without a
      // branch.
      continue;
    }
    implicit_batch[implicit_batch_size] = (uint16_t)i;
    scalar_array_batch[scalar_array_batch_size] = (uint16_t)i;
    implicit_batch_size += upb_IsNonZeroImplicitPresenceBranchless(f);
    scalar_array_batch_size += upb_IsNonEmptyArrayBranchless(msg, f) &&
                               !upb_MiniTableField_IsSubMessage(f);
    ptr = upb_BatchEncoder_EncodeBatchIfFull(e, ptr, msg, m, implicit_batch,
                                             &implicit_batch_size);
    ptr = upb_BatchEncoder_EncodeBatchIfFull(e, ptr, msg, m, scalar_array_batch,
                                             &scalar_array_batch_size);
  }

  ptr = upb_BatchEncoder_EncodeBatch(
      e, ptr, msg, m, primitive_batch, primitive_batch_size, implicit_batch,
      implicit_batch_size, string_batch, string_batch_size, scalar_array_batch,
      scalar_array_batch_size);

#if 0
  // NYI: extensions
  if (m->UPB_PRIVATE(ext) != kUpb_ExtMode_NonExtendable) {
    /* Encode all extensions together. Unlike C++, we do not attempt to keep
     * these in field number order relative to normal fields or even to each
     * other. */
    size_t ext_count;
    const upb_Extension* ext =
        UPB_PRIVATE(_upb_Message_Getexts)(msg, &ext_count);
    if (ext_count) {
      if (e->options & kUpb_EncodeOption_Deterministic) {
        _upb_sortedmap sorted;
        _upb_mapsorter_pushexts(&e->sorter, ext, ext_count, &sorted);
        while (_upb_sortedmap_nextext(&e->sorter, &sorted, &ext)) {
          encode_ext(e, ext, m->UPB_PRIVATE(ext) == kUpb_ExtMode_IsMessageSet);
        }
        _upb_mapsorter_popmap(&e->sorter, &sorted);
      } else {
        const upb_Extension* end = ext + ext_count;
        for (; ext != end; ext++) {
          encode_ext(e, ext, m->UPB_PRIVATE(ext) == kUpb_ExtMode_IsMessageSet);
        }
      }
    }
  }
#endif
  return ptr;
}

static upb_EncodeStatus upb_Encoder_Encode(upb_encstate* const encoder,
                                           const upb_Message* const msg,
                                           const upb_MiniTable* const l,
                                           char** const buf, size_t* const size,
                                           bool prepend_len) {
  if (UPB_SETJMP(encoder->err) == 0) {
    char* ptr = upb_BatchEncoder_EncodeMessage(encoder, NULL, msg, l);
    *size = ptr - encoder->buf;
    *buf = encoder->buf;
  } else {
    UPB_ASSERT(encoder->status != kUpb_EncodeStatus_Ok);
    *buf = NULL;
    *size = 0;
  }

  _upb_mapsorter_destroy(&encoder->sorter);
  return encoder->status;
}

static upb_EncodeStatus _upb_Encode(const upb_Message* msg,
                                    const upb_MiniTable* l, int options,
                                    upb_Arena* arena, char** buf, size_t* size,
                                    bool prepend_len) {
  upb_encstate e;
  unsigned depth = (unsigned)options >> 16;

  e.status = kUpb_EncodeStatus_Ok;
  e.arena = arena;
  e.buf = NULL;
  e.end = NULL;
  e.depth = depth ? depth : kUpb_WireFormat_DefaultDepthLimit;
  e.options = options;
  _upb_mapsorter_init(&e.sorter);

  return upb_Encoder_Encode(&e, msg, l, buf, size, prepend_len);
}

upb_EncodeStatus upb_BatchedEncode(const upb_Message* msg,
                                   const upb_MiniTable* l, int options,
                                   upb_Arena* arena, char** buf, size_t* size) {
  return _upb_Encode(msg, l, options, arena, buf, size, false);
}
