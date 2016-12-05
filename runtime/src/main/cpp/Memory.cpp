#include <string.h>
#include <stdlib.h>

#include "Assert.h"
#include "Exceptions.h"
#include "Memory.h"
#include "Natives.h"

void FreeObject(ContainerHeader* header) {
  header->ref_count_ = CONTAINER_TAG_INVALID;
  free(header);
}

ArenaContainer::ArenaContainer(uint32_t size) {
    ArenaContainerHeader* header = reinterpret_cast<ArenaContainerHeader*>(
        calloc(size + sizeof(ArenaContainerHeader), 1));
    header_ = header;
    header->ref_count_ = CONTAINER_TAG_INCREMENT;
    header->current_ =
        reinterpret_cast<uint8_t*>(header_) + sizeof(ArenaContainerHeader);
    header->end_ = header->current_ + size;
}

void ObjectContainer::Init(const TypeInfo* type_info) {
  RuntimeAssert(type_info->instanceSize_ >= 0, "Must be an object");
   uint32_t alloc_size =
      sizeof(ContainerHeader) + sizeof(ObjHeader) + type_info->instanceSize_;
  header_ = reinterpret_cast<ContainerHeader*>(calloc(alloc_size, 1));
  if (header_) {
    header_->ref_count_ = CONTAINER_TAG_INCREMENT;
    SetMeta(GetPlace(), type_info);
  }
}

void ArrayContainer::Init(const TypeInfo* type_info, uint32_t elements) {
  RuntimeAssert(type_info->instanceSize_ < 0, "Must be an array");
  uint32_t alloc_size =
      sizeof(ContainerHeader) + sizeof(ArrayHeader) -
      type_info->instanceSize_ * elements;
  header_ = reinterpret_cast<ContainerHeader*>(calloc(alloc_size, 1));
  RuntimeAssert(header_ != nullptr, "Cannot alloc memory");
  if (header_) {
    header_->ref_count_ = CONTAINER_TAG_INCREMENT;
    GetPlace()->count_ = elements;
    SetMeta(GetPlace(), type_info);
  }
}

ObjHeader* ArenaContainer::PlaceObject(const TypeInfo* type_info) {
  RuntimeAssert(type_info->instanceSize_ >= 0, "must be an object");
  uint32_t size = type_info->instanceSize_ + sizeof(ObjHeader);
  ObjHeader* result = reinterpret_cast<ObjHeader*>(Place(size));
  if (!result) {
      return nullptr;
  }
  SetMeta(result, type_info);
  return result;
}

ArrayHeader* ArenaContainer::PlaceArray(const TypeInfo* type_info, int count) {
  RuntimeAssert(type_info->instanceSize_ < 0, "must be an array");
  uint32_t size = sizeof(ArrayHeader) - type_info->instanceSize_ * count;
  ArrayHeader* result = reinterpret_cast<ArrayHeader*>(Place(size));
  if (!result) {
    return nullptr;
  }
  SetMeta(result, type_info);
  result->count_ = count;
  return result;
}

#ifdef __cplusplus
extern "C" {
#endif

void InitMemory() {
  // TODO: initialize heap here.
}

// Now we ignore all placement hints and always allocate heap space for new object.
ObjHeader* AllocInstance(const TypeInfo* type_info, PlacementHint hint) {
  RuntimeAssert(type_info->instanceSize_ >= 0, "must be an object");
  return ObjectContainer(type_info).GetPlace();
}

ObjHeader* AllocArrayInstance(
    const TypeInfo* type_info, PlacementHint hint, uint32_t elements) {
  RuntimeAssert(type_info->instanceSize_ < 0, "must be an array");
  return ArrayContainer(type_info, elements).GetPlace();
}

ObjHeader* AllocStringInstance(
    PlacementHint hint, const char* data, uint32_t length) {
  ArrayHeader* result = ArrayContainer(theStringTypeInfo, length).GetPlace();
  memcpy(
      ByteArrayAddressOfElementAt(result, 0),
      data,
      length);
  return result;
}

ObjHeader* InitInstance(
    ObjHeader** location, const TypeInfo* type_info, PlacementHint hint,
    void (*ctor)(ObjHeader*)) {
  ObjHeader* sentinel = reinterpret_cast<ObjHeader*>(1);
  ObjHeader* value;
  // Wait until other initializers.
  while ((value = __sync_val_compare_and_swap(
             location, nullptr, sentinel)) == sentinel) {
    // TODO: consider yielding.
  }

  if (value != nullptr) {
    // OK'ish, inited by someone else.
    return value;
  }

  ObjHeader* instance = AllocInstance(type_info, hint);
  try {
    ctor(instance);
    __sync_val_compare_and_swap(location, sentinel, instance);
    return instance;
  } catch (...) {
    Release(instance->container());
    __sync_val_compare_and_swap(location, sentinel, nullptr);
    return nullptr;
  }
}

#ifdef __cplusplus
}
#endif
