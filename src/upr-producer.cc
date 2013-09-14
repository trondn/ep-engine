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


ENGINE_ERROR_CODE EventuallyPersistentEngine::uprStep(const void* cookie,
                                                      struct upr_message_producers *producers)
{
    return ENGINE_ENOTSUP;
}

ENGINE_ERROR_CODE EventuallyPersistentEngine::uprStreamReq(const void* cookie,
                                                           const void *gid, size_t ngid,
                                                           uint32_t flags,
                                                           uint32_t opaque,
                                                           uint16_t vbucket,
                                                           uint64_t start_seqno,
                                                           uint64_t end_seqno,
                                                           uint64_t vbucket_uuid,
                                                           uint64_t high_seqno,
                                                           uint64_t *rollback_seqno)
{
    return ENGINE_ENOTSUP;
}

ENGINE_ERROR_CODE EventuallyPersistentEngine::uprGetFailoverLog(const void* cookie,
                                                                uint32_t opaque,
                                                                uint16_t vbucket,
                                                                upr_add_failover_log callback)
{
    return ENGINE_ENOTSUP;
}
