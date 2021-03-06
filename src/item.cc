/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2010 Couchbase, Inc
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include "config.h"

#include "item.h"
#include "cJSON.h"

Atomic<uint64_t> Item::casCounter(1);
const uint32_t Item::metaDataSize(2 * sizeof(uint32_t) + 2 * sizeof(uint64_t) + 2);

bool Item::append(const Item &i) {
    assert(value.get() != NULL);
    assert(i.getValue().get() != NULL);
    size_t newSize = value->length() + i.getValue()->length();
    Blob *newData = Blob::New(newSize);
    char *newValue = (char *) newData->getData();
    std::memcpy(newValue, value->getData(), value->length());
    std::memcpy(newValue + value->length(), i.getValue()->getData(), i.getValue()->length());
    value.reset(newData);
    return true;
}

/**
 * Prepend another item to this item
 *
 * @param itm the item to prepend to this one
 * @return true if success
 */
bool Item::prepend(const Item &i) {
    assert(value.get() != NULL);
    assert(i.getValue().get() != NULL);
    size_t newSize = value->length() + i.getValue()->length();
    Blob *newData = Blob::New(newSize);
    char *newValue = (char *) newData->getData();
    std::memcpy(newValue, i.getValue()->getData(), i.getValue()->length());
    std::memcpy(newValue + i.getValue()->length(), value->getData(), value->length());
    value.reset(newData);
    return true;
}
