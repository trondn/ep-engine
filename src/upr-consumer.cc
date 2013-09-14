/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2013 Couchbase, Inc
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
#include "ep_engine.h"

ENGINE_ERROR_CODE EventuallyPersistentEngine::uprStreamStart(const void* cookie,
                                                             uint32_t opaque,
                                                             uint16_t vbucket)
{
    return ENGINE_ENOTSUP;
}

ENGINE_ERROR_CODE EventuallyPersistentEngine::uprStreamEnd(const void* cookie,
                                                           uint32_t opaque,
                                                           uint16_t vbucket,
                                                           uint32_t flags)
{
    return ENGINE_ENOTSUP;
}

ENGINE_ERROR_CODE EventuallyPersistentEngine::uprSnapshotStart(const void* cookie,
                                                               uint32_t opaque,
                                                               uint16_t vbucket)
{
    return ENGINE_ENOTSUP;
}

ENGINE_ERROR_CODE EventuallyPersistentEngine::uprSnapshotEnd(const void* cookie,
                                                             uint32_t opaque,
                                                             uint16_t vbucket)
{
    return ENGINE_ENOTSUP;
}

ENGINE_ERROR_CODE EventuallyPersistentEngine::uprMutation(const void* cookie,
                                                          uint32_t opaque,
                                                          const void *key,
                                                          uint16_t nkey,
                                                          const void *value,
                                                          uint32_t nvalue,
                                                          uint64_t cas,
                                                          uint16_t vbucket,
                                                          uint32_t flags,
                                                          uint8_t datatype,
                                                          uint64_t bySeqno,
                                                          uint64_t revSeqno,
                                                          uint32_t expiration,
                                                          uint32_t lockTime)
{
    return ENGINE_ENOTSUP;
}

ENGINE_ERROR_CODE EventuallyPersistentEngine::uprDeletion(const void* cookie,
                                                          uint32_t opaque,
                                                          const void *key,
                                                          uint16_t nkey,
                                                          uint64_t cas,
                                                          uint16_t vbucket,
                                                          uint64_t bySeqno,
                                                          uint64_t revSeqno)
{
    return ENGINE_ENOTSUP;
}

ENGINE_ERROR_CODE EventuallyPersistentEngine::uprExpiration(const void* cookie,
                                                            uint32_t opaque,
                                                            const void *key,
                                                            uint16_t nkey,
                                                            uint64_t cas,
                                                            uint16_t vbucket,
                                                            uint64_t bySeqno,
                                                            uint64_t revSeqno)
{
    return ENGINE_ENOTSUP;
}

ENGINE_ERROR_CODE EventuallyPersistentEngine::uprFlush(const void* cookie,
                                                       uint32_t opaque,
                                                       uint16_t vbucket)
{
    return ENGINE_ENOTSUP;
}

ENGINE_ERROR_CODE EventuallyPersistentEngine::uprSetVbucketState(const void* cookie,
                                                                 uint32_t opaque,
                                                                 uint16_t vbucket,
                                                                 vbucket_state_t state)
{
    return ENGINE_ENOTSUP;
}
