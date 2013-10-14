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

#ifndef SRC_STORED_VALUE_H_
#define SRC_STORED_VALUE_H_ 1

#include "config.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <string>

#include "common.h"
#include "ep_time.h"
#include "histo.h"
#include "item.h"
#include "locks.h"
#include "queueditem.h"
#include "stats.h"

// Max value for NRU bits
const uint8_t MAX_NRU_VALUE = 3;
// Initial value for NRU bits
const uint8_t INITIAL_NRU_VALUE = 2;
// Min value for NRU bits
const uint8_t MIN_NRU_VALUE = 0;

// Forward declaration for StoredValue
class HashTable;
class StoredValueFactory;

/**
 * In-memory storage for an item.
 */
class StoredValue {
public:

    void operator delete(void* p) {
        ::operator delete(p);
     }

    uint8_t getNRUValue();

    void setNRUValue(uint8_t nru_val);

    uint8_t incrNRUValue();

    void referenced();

    /**
     * Mark this item as needing to be persisted.
     */
    void markDirty() {
        _isDirty = 1;
    }

    /**
     * Mark this item as dirty as of a certain time.
     *
     * This method is primarily used to mark an item as dirty again
     * after a storage failure.
     *
     * @param dataAge the previous dataAge of this record
     */
    void reDirty() {
        _isDirty = 1;
        clearPendingBySeqno();
    }

    // returns time this object was dirtied.
    /**
     * Mark this item as clean.
     *
     * @param dataAge an output parameter that captures the time this
     *                item was marked dirty
     */
    void markClean() {
        _isDirty = 0;
    }

    /**
     * True if this object is dirty.
     */
    bool isDirty() const {
        return _isDirty;
    }

    /**
     * True if this object is not dirty.
     */
    bool isClean() const {
        return !isDirty();
    }

    bool eligibleForEviction() {
        return isResident() && isClean() && !isDeleted();
    }

    /**
     * Check if this item is expired or not.
     *
     * @param asOf the time to be compared with this item's expiry time
     * @return true if this item's expiry time < asOf
     */
    bool isExpired(time_t asOf) const {
        if (getExptime() != 0 && getExptime() < asOf) {
            return true;
        }
        return false;
    }

    /**
     * Get the pointer to the beginning of the key.
     */
    const char* getKeyBytes() const {
        return keybytes;
    }

    /**
     * Get the length of the key.
     */
    uint8_t getKeyLen() const {
        return keylen;
    }

    /**
     * True of this item is for the given key.
     *
     * @param k the key we're checking
     * @return true if this item's key is equal to k
     */
    bool hasKey(const std::string &k) const {
        return k.length() == getKeyLen()
            && (std::memcmp(k.data(), getKeyBytes(), getKeyLen()) == 0);
    }

    /**
     * Get this item's key.
     */
    const std::string getKey() const {
        return std::string(getKeyBytes(), getKeyLen());
    }

    /**
     * Get this item's value.
     */
    const value_t &getValue() const {
        return value;
    }

    /**
     * Get the expiration time of this item.
     *
     * @return the expiration time for feature items, 0 for small items
     */
    time_t getExptime() const {
        return exptime;
    }

    void setExptime(time_t tim) {
        exptime = tim;
        markDirty();
    }

    /**
     * Get the client-defined flags of this item.
     *
     * @return the flags for feature items, 0 for small items
     */
    uint32_t getFlags() const {
        return flags;
    }

    /**
     * Set the client-defined flags for this item.
     */
    void setFlags(uint32_t fl) {
        flags = fl;
    }

    /**
     * Set a new value for this item.
     *
     * @param itm the item with a new value
     * @param ht the hashtable that contains this StoredValue instance
     * @param preserveSeqno Preserve the revision sequence number from the item.
     */
    void setValue(Item &itm, HashTable &ht, bool preserveSeqno) {
        size_t currSize = size();
        reduceCacheSize(ht, currSize);
        value = itm.getValue();
        deleted = false;
        flags = itm.getFlags();

        cas = itm.getCas();
        exptime = itm.getExptime();
        if (preserveSeqno) {
            revSeqno = itm.getRevSeqno();
        } else {
            ++revSeqno;
            itm.setRevSeqno(revSeqno);
        }

        markDirty();
        size_t newSize = size();
        increaseCacheSize(ht, newSize);
    }

    /**
     * Reset the value of this item.
     */
    void resetValue() {
        assert(!isDeleted());
        markNotResident();
        // item no longer resident once reset the value
        deleted = true;
    }

    /**
     * Eject an item value from memory.
     * @param stats the global stat instance
     * @param ht the hashtable that contains this StoredValue instance
     */
    bool ejectValue(EPStats &stats, HashTable &ht);

    /**
     * Restore the value for this item.
     * @param itm the item to be restored
     * @param ht the hashtable that contains this StoredValue instance
     */
    bool unlocked_restoreValue(Item *itm, HashTable &ht);

    /**
     * Restore the metadata of of a temporary item upon completion of a
     * background fetch assuming the hashtable bucket is locked.
     *
     * @param itm the Item whose metadata is being restored
     * @param status the engine code describing the result of the background
     *               fetch
     */
    bool unlocked_restoreMeta(Item *itm, ENGINE_ERROR_CODE status);

    /**
     * Get this item's CAS identifier.
     *
     * @return the cas ID for feature items, 0 for small items
     */
    uint64_t getCas() const {
        return cas;
    }

    /**
     * Set a new CAS ID.
     *
     * This is a NOOP for small item types.
     */
    void setCas(uint64_t c) {
        cas = c;
    }

    /**
     * Lock this item until the given time.
     *
     * This is a NOOP for small item types.
     */
    void lock(rel_time_t expiry) {
        lock_expiry = expiry;
    }

    /**
     * Unlock this item.
     */
    void unlock() {
        lock_expiry = 0;
    }

    /**
     * True if this item has an ID.
     *
     * An item always has an ID after it's been persisted.
     */
    bool hasBySeqno() {
        return bySeqno > 0;
    }

    /**
     * Get this item's ID.
     *
     * @return the ID for the item; 0 if the item has no ID
     */
    int64_t getBySeqno() {
        return bySeqno;
    }

    /**
     * Set the ID for this item.
     *
     * This is used by the persistene layer.
     *
     * It is an error to set an ID on an item that already has one.
     */
    void setBySeqno(int64_t to) {
        bySeqno = to;
        assert(hasBySeqno());
    }

    /**
     * Clear the ID (after disk deletion when an object was reused).
     */
    void clearBySeqno() {
        bySeqno = state_cleared;
        assert(!hasBySeqno());
    }

    /**
     * Is this item currently waiting to receive a new ID?
     *
     * This is the case when it's been submitted to the storage layer
     * and has been marked clean, but has not yet received its ID.
     *
     * @return true if the item is waiting for an ID.
     */
    bool isPendingBySeqno() {
        return bySeqno == state_pending;
    }

    /**
     * Set this item to be pending an ID.
     */
    void setPendingBySeqno() {
        assert(!hasBySeqno());
        assert(!isPendingBySeqno());
        bySeqno = state_pending;
    }

    /**
     * If we're still in a pending ID state, clear the state.
     */
    void clearPendingBySeqno() {
        if (isPendingBySeqno()) {
            bySeqno = state_cleared;
        }
    }

    /**
     * Set the stored value state to the specified value
     */
    void setStoredValueState(const int64_t to) {
        assert(to == state_deleted_key || to == state_non_existent_key);
        bySeqno = to;
    }

    /**
     * Is this a temporary item created for processing a get-meta request?
     */
     bool isTempItem() {
         return(isTempNonExistentItem() || isTempDeletedItem() || isTempInitialItem());

     }

    /**
     * Is this an initial temporary item?
     */
    bool isTempInitialItem() {
        return bySeqno == state_temp_init;
    }

    /**
     * Is this a temporary item created for a non-existent key?
     */
     bool isTempNonExistentItem() {
         return bySeqno == state_non_existent_key;

     }

    /**
     * Is this a temporary item created for a deleted key?
     */
     bool isTempDeletedItem() {
         return bySeqno == state_deleted_key;

     }

    size_t valuelen() {
        if (isDeleted() || !isResident()) {
            return 0;
        }
        return value->length();
    }

    /**
     * Get the total size of this item.
     *
     * @return the amount of memory used by this item.
     */
    size_t size() {
        return sizeof(StoredValue) + getKeyLen() + valuelen();
    }

    size_t metaDataSize() {
        return sizeof(StoredValue) + getKeyLen();
    }

    /**
     * Return true if this item is locked as of the given timestamp.
     *
     * @param curtime lock expiration marker (usually the current time)
     * @return true if the item is locked
     */
    bool isLocked(rel_time_t curtime) {
        if (lock_expiry == 0 || (curtime > lock_expiry)) {
            lock_expiry = 0;
            return false;
        }
        return true;
    }

    /**
     * True if this value is resident in memory currently.
     */
    bool isResident() const {
        return value.get() != NULL;
    }

    void markNotResident() {
        value.reset();
    }

    /**
     * True if this object is logically deleted.
     */
    bool isDeleted() const {
        return deleted;
    }

    /**
     * Logically delete this object.
     */
    void del(EPStats &, HashTable &ht, bool isMetaDelete=false) {
        if (isDeleted()) {
            return;
        }

        reduceCacheSize(ht, valuelen());
        resetValue();
        markDirty();
        if (!isMetaDelete) {
            setCas(getCas() + 1);
        }
    }


    uint64_t getRevSeqno() const {
        return revSeqno;
    }

    /**
     * Set a new revision sequence number.
     */
    void setRevSeqno(uint64_t s) {
        revSeqno = s;
    }


    /**
     * Generate a new Item out of this object.
     *
     * @param lck if true, the new item will return a locked CAS ID.
     * @param vbucket the vbucket containing this item.
     */
    Item *toItem(bool lck, uint16_t vbucket) const;

    /**
     * Set the memory threshold on the current bucket quota for accepting a new mutation
     */
    static void setMutationMemoryThreshold(double memThreshold);

    static const int64_t state_cleared;
    static const int64_t state_pending;

    /*
     * Values of the bySeqno attribute used by temporarily created StoredValue
     * objects.
     * state_deleted_key: represents an item that's deleted from memory but
     *                    present in the persistent store.
     * state_non_existent_key: represents a non existent item
     */
    static const int64_t state_deleted_key;
    static const int64_t state_non_existent_key;
    static const int64_t state_temp_init;

private:

    StoredValue(const Item &itm, StoredValue *n, EPStats &stats, HashTable &ht,
                bool setDirty = true) :
        value(itm.getValue()), next(n), bySeqno(itm.getBySeqno()),
        flags(itm.getFlags()) {
        cas = itm.getCas();
        exptime = itm.getExptime();
        deleted = false;
        nru = INITIAL_NRU_VALUE;
        lock_expiry = 0;
        keylen = itm.getKey().length();
        revSeqno = itm.getRevSeqno();

        if (setDirty) {
            markDirty();
        } else {
            markClean();
        }

        increaseMetaDataSize(ht, stats, metaDataSize());
        increaseCacheSize(ht, size());
    }

    friend class HashTable;
    friend class StoredValueFactory;

    value_t            value;          // 16 bytes
    StoredValue        *next;          // 8 bytes
    uint64_t           cas;            //!< CAS identifier.
    uint64_t           revSeqno;       //!< Revision id sequence number
    int64_t            bySeqno;        //!< By sequence id number
    rel_time_t         lock_expiry;    //!< getl lock expiration
    uint32_t           exptime;        //!< Expiration time of this item.
    uint32_t           flags;          // 4 bytes
    bool               _isDirty  :  1; // 1 bit
    bool               deleted   :  1;
    uint8_t            nru       :  2; //!< True if referenced since last sweep
    uint8_t            keylen;
    char               keybytes[1];    //!< The key itself.

    static void increaseMetaDataSize(HashTable &ht, EPStats &st, size_t by);
    static void reduceMetaDataSize(HashTable &ht, EPStats &st, size_t by);
    static void increaseCacheSize(HashTable &ht, size_t by);
    static void reduceCacheSize(HashTable &ht, size_t by);
    static bool hasAvailableSpace(EPStats&, const Item &item);
    static double mutation_mem_threshold;

    DISALLOW_COPY_AND_ASSIGN(StoredValue);
};

/**
 * Mutation types as returned by store commands.
 */
typedef enum {
    /**
     * Storage was attempted on a vbucket not managed by this node.
     */
    INVALID_VBUCKET,
    NOT_FOUND,                  //!< The item was not found for update
    INVALID_CAS,                //!< The wrong CAS identifier was sent for a CAS update
    WAS_CLEAN,                  //!< The item was clean before this mutation
    WAS_DIRTY,                  //!< This item was already dirty before this mutation
    IS_LOCKED,                  //!< The item is locked and can't be updated.
    NOMEM                       //!< Insufficient memory to store this item.
} mutation_type_t;

/**
 * Result from add operation.
 */
typedef enum {
    ADD_SUCCESS,                //!< Add was successful.
    ADD_NOMEM,                  //!< No memory for operation
    ADD_EXISTS,                 //!< Did not update -- item exists with this key
    ADD_UNDEL                   //!< Undeletes an existing dirty item
} add_type_t;

/**
 * Base class for visiting a hash table.
 */
class HashTableVisitor {
public:
    virtual ~HashTableVisitor() {}

    /**
     * Visit an individual item within a hash table.
     *
     * @param v a pointer to a value in the hash table
     */
    virtual void visit(StoredValue *v) = 0;
    /**
     * True if the visiting should continue.
     *
     * This is called periodically to ensure the visitor still wants
     * to visit items.
     */
    virtual bool shouldContinue() { return true; }
};

/**
 * Hash table visitor that reports the depth of each hashtable bucket.
 */
class HashTableDepthVisitor {
public:
    virtual ~HashTableDepthVisitor() {}

    /**
     * Called once for each hashtable bucket with its depth.
     *
     * @param bucket the index of the hashtable bucket
     * @param depth the number of entries in this hashtable bucket
     * @param mem counted memory used by this hash table
     */
    virtual void visit(int bucket, int depth, size_t mem) = 0;
};

/**
 * Hash table visitor that finds the min and max bucket depths.
 */
class HashTableDepthStatVisitor : public HashTableDepthVisitor {
public:

    HashTableDepthStatVisitor() : depthHisto(GrowingWidthGenerator<unsigned int>(1, 1, 1.3),
                                             10),
                                  size(0), memUsed(0), min(-1), max(0) {}

    void visit(int bucket, int depth, size_t mem) {
        (void)bucket;
        // -1 is a special case for min.  If there's a value other than
        // -1, we prefer that.
        min = std::min(min == -1 ? depth : min, depth);
        max = std::max(max, depth);
        depthHisto.add(depth);
        size += depth;
        memUsed += mem;
    }

    Histogram<unsigned int> depthHisto;
    size_t                  size;
    size_t                  memUsed;
    int                     min;
    int                     max;
};

/**
 * Hash table visitor that collects stats of what's inside.
 */
class HashTableStatVisitor : public HashTableVisitor {
public:

    HashTableStatVisitor() : numNonResident(0), numTotal(0),
                             memSize(0), valSize(0), cacheSize(0) {}

    void visit(StoredValue *v) {
        ++numTotal;
        memSize += v->size();
        valSize += v->valuelen();

        if (v->isResident()) {
            cacheSize += v->size();
        } else {
            ++numNonResident;
        }
    }

    size_t numNonResident;
    size_t numTotal;
    size_t memSize;
    size_t valSize;
    size_t cacheSize;
};

/**
 * Track the current number of hashtable visitors.
 *
 * This class is a pretty generic counter holder that increments on
 * entry and decrements on return providing RAII guarantees around an
 * atomic counter.
 */
class VisitorTracker {
public:

    /**
     * Mark a visitor as visiting.
     *
     * @param c the counter that should be incremented (and later
     * decremented).
     */
    explicit VisitorTracker(Atomic<size_t> *c) : counter(c) {
        counter->incr(1);
    }
    ~VisitorTracker() {
        counter->decr(1);
    }
private:
    Atomic<size_t> *counter;
};

/**
 * Creator of StoredValue instances.
 */
class StoredValueFactory {
public:

    /**
     * Create a new StoredValueFactory of the given type.
     */
    StoredValueFactory(EPStats &s) : stats(&s) { }

    /**
     * Create a new StoredValue with the given item.
     *
     * @param itm the item the StoredValue should contain
     * @param n the the top of the hash bucket into which this will be inserted
     * @param ht the hashtable that will contain the StoredValue instance created
     * @param setDirty if true, mark this item as dirty after creating it
     */
    StoredValue *operator ()(const Item &itm, StoredValue *n, HashTable &ht,
                             bool setDirty = true) {
        return newStoredValue(itm, n, ht, setDirty);
    }

private:

    StoredValue* newStoredValue(const Item &itm, StoredValue *n, HashTable &ht,
                                bool setDirty) {
        size_t base = sizeof(StoredValue);

        const std::string &key = itm.getKey();
        assert(key.length() < 256);
        size_t len = key.length() + base;

        StoredValue *t = new (::operator new(len))
        StoredValue(itm, n, *stats, ht, setDirty);
        std::memcpy(t->keybytes, key.data(), key.length());
        return t;
    }

    EPStats                *stats;
};

/**
 * A container of StoredValue instances.
 */
class HashTable {
public:

    /**
     * Create a HashTable.
     *
     * @param st the global stats reference
     * @param s the number of hash table buckets
     * @param l the number of locks in the hash table
     */
    HashTable(EPStats &st, size_t s = 0, size_t l = 0) : stats(st), valFact(st) {
        size = HashTable::getNumBuckets(s);
        n_locks = HashTable::getNumLocks(l);
        assert(size > 0);
        assert(n_locks > 0);
        assert(visitors == 0);
        values = static_cast<StoredValue**>(calloc(size, sizeof(StoredValue*)));
        mutexes = new Mutex[n_locks];
        activeState = true;
    }

    ~HashTable() {
        clear(true);
        // Wait for any outstanding visitors to finish.
        while (visitors > 0) {
            usleep(100);
        }
        delete []mutexes;
        free(values);
        values = NULL;
    }

    size_t memorySize() {
        return sizeof(HashTable)
            + (size * sizeof(StoredValue*))
            + (n_locks * sizeof(Mutex));
    }

    /**
     * Get the number of hash table buckets this hash table has.
     */
    size_t getSize(void) { return size; }

    /**
     * Get the number of locks in this hash table.
     */
    size_t getNumLocks(void) { return n_locks; }

    /**
     * Get the number of items within this hash table.
     */
    size_t getNumItems(void) { return numItems; }

    /**
     * Get the number of non-resident items within this hash table.
     */
    size_t getNumNonResidentItems(void) { return numNonResidentItems; }

    /**
     * Get the number of items whose values are ejected from this hash table.
     */
    size_t getNumEjects(void) { return numEjects; }

    /**
     * Get the total item memory size in this hash table.
     */
    size_t getItemMemory(void) { return memSize; }

    /**
     * Clear the hash table.
     *
     * @param deactivate true when this hash table is being destroyed completely
     *
     * @return a stat visitor reporting how much stuff was removed
     */
    HashTableStatVisitor clear(bool deactivate = false);

    /**
     * Get the number of times this hash table has been resized.
     */
    size_t getNumResizes() { return numResizes; }

    /**
     * Get the number of temp. items within this hash table.
     */
    size_t getNumTempItems(void) { return numTempItems; }

    /**
     * Automatically resize to fit the current data.
     */
    void resize();

    /**
     * Resize to the specified size.
     */
    void resize(size_t to);

    /**
     * Find the item with the given key.
     *
     * @param key the key to find
     * @return a pointer to a StoredValue -- NULL if not found
     */
    StoredValue *find(std::string &key, bool trackReference=true) {
        assert(isActive());
        int bucket_num(0);
        LockHolder lh = getLockedBucket(key, &bucket_num);
        return unlocked_find(key, bucket_num, false, trackReference);
    }

    /**
     * Add an item from online restore.
     *
     * @return true if added, false if skipped
     */
    bool unlocked_restoreItem(const Item &itm,
                              enum queue_operation op,
                              int bucket_num)
    {
        if (unlocked_find(itm.getKey(), bucket_num, true)) {
            // it's already there...
            return false;
        }

        StoredValue *v = valFact(itm, values[bucket_num], *this);
        assert(v);
        values[bucket_num] = v;
        ++numItems;
        if (op == queue_op_del) {
            unlocked_softDelete(v, itm.getCas());
        }
        return true;
    }

    /**
     * Set a new Item into this hashtable. Use this function when your item
     * doesn't contain meta data.
     *
     * @param val the Item to store
     * @param nru the nru bit for the item
     * @return a result indicating the status of the store
     */
    mutation_type_t set(const Item &val, uint8_t nru=0xff)
    {
        return set(val, val.getCas(), true, false, nru);
    }

    /**
     * Set an Item into the this hashtable. Use this function to do a set
     * when your item includes meta data.
     *
     * @param val the Item to store
     * @param cas This is the cas value for the item <b>in</b> the cache
     * @param allowExisting should we allow existing items or not
     * @param hasMetaData should we keep the same revision seqno or increment it
     * @param nru the nru bit for the item
     * @return a result indicating the status of the store
     */
    mutation_type_t set(const Item &val, uint64_t cas,
                        bool allowExisting, bool hasMetaData = true,
                        uint8_t nru=0xff) {
        int bucket_num(0);
        LockHolder lh = getLockedBucket(val.getKey(), &bucket_num);
        StoredValue *v = unlocked_find(val.getKey(), bucket_num, true, false);
        return unlocked_set(v, val, cas, allowExisting, hasMetaData, nru);
    }

    mutation_type_t unlocked_set(StoredValue *v, const Item &val, uint64_t cas,
                                 bool allowExisting, bool hasMetaData = true,
                                 uint8_t nru=0xff) {
        assert(isActive());
        Item &itm = const_cast<Item&>(val);
        if (!StoredValue::hasAvailableSpace(stats, itm)) {
            return NOMEM;
        }

        mutation_type_t rv = NOT_FOUND;

        /*
         * prior to checking for the lock, we should check if this object
         * has expired. If so, then check if CAS value has been provided
         * for this set op. In this case the operation should be denied since
         * a cas operation for a key that doesn't exist is not a very cool
         * thing to do. See MB 3252
         */
        if (v && v->isExpired(ep_real_time()) && !hasMetaData) {
            if (v->isLocked(ep_current_time())) {
                v->unlock();
            }
            if (cas) {
                /* item has expired and cas value provided. Deny ! */
                return NOT_FOUND;
            }
        }

        if (v) {
            if (!allowExisting && !v->isTempItem()) {
                return INVALID_CAS;
            }
            if (v->isLocked(ep_current_time())) {
                /*
                 * item is locked, deny if there is cas value mismatch
                 * or no cas value is provided by the user
                 */
                if (cas != v->getCas()) {
                    return IS_LOCKED;
                }
                /* allow operation*/
                v->unlock();
            } else if (cas != 0 && cas != v->getCas()) {
                return INVALID_CAS;
            }

            if (v->isTempItem()) {
                v->clearBySeqno();
                --numTempItems;
                ++numItems;
            }

            if (!hasMetaData) {
                itm.setCas();
            }
            rv = v->isClean() ? WAS_CLEAN : WAS_DIRTY;
            if (!v->isResident() && !v->isDeleted()) {
                --numNonResidentItems;
            }
            v->setValue(itm, *this, hasMetaData /*Preserve revSeqno*/);
            if (nru <= MAX_NRU_VALUE) {
                v->setNRUValue(nru);
            }
        } else if (cas != 0) {
            rv = NOT_FOUND;
        } else {
            if (!hasMetaData) {
                itm.setCas();
            }
            int bucket_num = getBucketForHash(hash(itm.getKey()));
            v = valFact(itm, values[bucket_num], *this);
            values[bucket_num] = v;
            ++numItems;
            if (nru <= MAX_NRU_VALUE && !v->isTempItem()) {
                v->setNRUValue(nru);
            }

            /**
             * Possibly, this item is being recreated. Conservatively assign it
             * a seqno that is greater than the greatest seqno of all deleted
             * items seen so far.
             */
            uint64_t seqno = getMaxDeletedRevSeqno() + 1;
            v->setRevSeqno(seqno);
            itm.setRevSeqno(seqno);
            rv = WAS_CLEAN;
        }
        return rv;
    }

    /**
     * Insert an item to this hashtable. This is called from the backfill
     * so we need a bit more logic here. If we're trying to insert a partial
     * item we don't allow the object to be stored there (and if you try to
     * insert a full item we're only allowing an item without the value
     * in memory...)
     *
     * @param val the Item to insert
     * @param eject true if we should eject the value immediately
     * @param partial is this a complete item, or just the key and meta-data
     * @return a result indicating the status of the store
     */
    mutation_type_t insert(Item &itm, bool eject, bool partial);

    /**
     * Add an item to the hash table iff it doesn't already exist.
     *
     * @param val the item to store
     * @param isDirty true if the item should be marked dirty on store
     * @param storeVal true if the value should be stored (paged-in)
     * @return an indication of what happened
     */
    add_type_t add(const Item &val, bool isDirty = true, bool storeVal = true) {
        assert(isActive());
        int bucket_num(0);
        LockHolder lh = getLockedBucket(val.getKey(), &bucket_num);
        return unlocked_add(bucket_num, val, isDirty, storeVal);
    }

    /**
     * Unlocked version of the add() method.
     *
     * @param bucket_num the locked partition where the key belongs
     * @param val the item to store
     * @param isDirty true if the item should be marked dirty on store
     * @param storeVal true if the value should be stored (paged-in)
     * @return an indication of what happened
     */
    add_type_t unlocked_add(int &bucket_num,
                            const Item &val,
                            bool isDirty = true,
                            bool storeVal = true);

    /**
     * Add a temporary item to the hash table iff it doesn't already exist.
     *
     * NOTE: This method should be called after acquiring the correct
     *       bucket/partition lock.
     *
     * @param bucket_num the locked partition where the key belongs
     * @param key the key for which a temporary item needs to be added
     * @return an indication of what happened
     */
    add_type_t unlocked_addTempDeletedItem(int &bucket_num,
                                           const std::string &key);

    /**
     * Mark the given record logically deleted.
     *
     * @param key the key of the item to delete
     * @param cas the expected CAS of the item (or 0 to override)
     * @return an indicator of what the deletion did
     */
    mutation_type_t softDelete(const std::string &key, uint64_t cas) {
        assert(isActive());
        int bucket_num(0);
        LockHolder lh = getLockedBucket(key, &bucket_num);
        StoredValue *v = unlocked_find(key, bucket_num, false, false);
        return unlocked_softDelete(v, cas);
    }

    mutation_type_t unlocked_softDelete(StoredValue *v, uint64_t cas) {
        if (v) {
            uint64_t revSeqno = v->getRevSeqno();
            return unlocked_softDelete(v, cas, ++revSeqno);
        }
        return NOT_FOUND;
    }

    /**
     * Unlocked implementation of softDelete.
     */
    mutation_type_t unlocked_softDelete(StoredValue *v, uint64_t cas,
                                        uint64_t newRevSeqno,
                                        bool use_meta=false,
                                        uint64_t newCas=0,
                                        uint32_t newFlags=0,
                                        time_t newExptime=0) {
        mutation_type_t rv = NOT_FOUND;
        if (v) {
            if (v->isExpired(ep_real_time()) && !use_meta) {
                if (!v->isResident() && !v->isDeleted()) {
                    --numNonResidentItems;
                }
                v->setRevSeqno(newRevSeqno);
                v->del(stats, *this, use_meta);
                updateMaxDeletedRevSeqno(v->getRevSeqno());
                return rv;
            }

            if (v->isLocked(ep_current_time())) {
                if (cas != v->getCas()) {
                    return IS_LOCKED;
                }
                v->unlock();
            }

            if (cas != 0 && cas != v->getCas()) {
                return INVALID_CAS;
            }

            if (!v->isResident() && !v->isDeleted()) {
                --numNonResidentItems;
            }

            /* allow operation*/
            v->unlock();

            rv = v->isClean() ? WAS_CLEAN : WAS_DIRTY;
            v->setRevSeqno(newRevSeqno);
            if (use_meta) {
                v->setCas(newCas);
                v->setFlags(newFlags);
                v->setExptime(newExptime);
                if (v->isTempItem()) {
                    --numTempItems;
                    ++numItems;
                    v->clearBySeqno();
                }
            }
            v->del(stats, *this, use_meta);

            updateMaxDeletedRevSeqno(v->getRevSeqno());
        }
        return rv;
    }

    /**
     * Find an item within a specific bucket assuming you already
     * locked the bucket.
     *
     * @param key the key of the item to find
     * @param bucket_num the bucket number
     * @param wantsDeleted true if soft deleted items should be returned
     *
     * @return a pointer to a StoredValue -- NULL if not found
     */
    StoredValue *unlocked_find(const std::string &key, int bucket_num,
                               bool wantsDeleted=false, bool trackReference=true) {
        StoredValue *v = values[bucket_num];
        while (v) {
            if (v->hasKey(key)) {
                if (trackReference && !v->isDeleted()) {
                    v->referenced();
                }
                if (wantsDeleted || !v->isDeleted()) {
                    return v;
                } else {
                    return NULL;
                }
            }
            v = v->next;
        }
        return NULL;
    }

    /**
     * Compute a hash for the given string.
     *
     * @param str the beginning of the string
     * @param len the number of bytes in the string
     *
     * @return the hash value
     */
    inline int hash(const char *str, const size_t len) {
        assert(isActive());
        int h=5381;

        for(size_t i=0; i < len; i++) {
            h = ((h << 5) + h) ^ str[i];
        }

        return h;
    }

    /**
     * Compute a hash for the given string.
     *
     * @param s the string
     * @return the hash value
     */
    inline int hash(const std::string &s) {
        return hash(s.data(), s.length());
    }

    /**
     * Get a lock holder holding a lock for the bucket for the given
     * hash.
     *
     * @param h the input hash
     * @param bucket output parameter to receive a bucket
     * @return a locked LockHolder
     */
    inline LockHolder getLockedBucket(int h, int *bucket) {
        while (true) {
            assert(isActive());
            *bucket = getBucketForHash(h);
            LockHolder rv(mutexes[mutexForBucket(*bucket)]);
            if (*bucket == getBucketForHash(h)) {
                return rv;
            }
        }
    }

    /**
     * Get a lock holder holding a lock for the bucket for the hash of
     * the given key.
     *
     * @param s the start of the key
     * @param n the size of the key
     * @param bucket output parameter to receive a bucket
     * @return a locked LockHolder
     */
    inline LockHolder getLockedBucket(const char *s, size_t n, int *bucket) {
        return getLockedBucket(hash(s, n), bucket);
    }

    /**
     * Get a lock holder holding a lock for the bucket for the hash of
     * the given key.
     *
     * @param s the key
     * @param bucket output parameter to receive a bucket
     * @return a locked LockHolder
     */
    inline LockHolder getLockedBucket(const std::string &s, int *bucket) {
        return getLockedBucket(hash(s.data(), s.size()), bucket);
    }

    /**
     * Delete a key from the cache without trying to lock the cache first
     * (Please note that you <b>MUST</b> acquire the mutex before calling
     * this function!!!
     *
     * @param key the key to delete
     * @param bucket_num the bucket to look in (must already be locked)
     * @return true if an object was deleted, false otherwise
     */
    bool unlocked_del(const std::string &key, int bucket_num) {
        assert(isActive());
        StoredValue *v = values[bucket_num];

        // Special case empty bucket.
        if (!v) {
            return false;
        }

        // Special case the first one
        if (v->hasKey(key)) {
            if (!v->isDeleted() && v->isLocked(ep_current_time())) {
                return false;
            }

            values[bucket_num] = v->next;
            StoredValue::reduceCacheSize(*this, v->size());
            StoredValue::reduceMetaDataSize(*this, stats, v->metaDataSize());
            if (v->isTempItem()) {
                --numTempItems;
            } else {
                --numItems;
            }
            delete v;
            return true;
        }

        while (v->next) {
            if (v->next->hasKey(key)) {
                StoredValue *tmp = v->next;
                if (!tmp->isDeleted() && tmp->isLocked(ep_current_time())) {
                    return false;
                }

                v->next = v->next->next;
                StoredValue::reduceCacheSize(*this, tmp->size());
                StoredValue::reduceMetaDataSize(*this, stats, tmp->metaDataSize());
                if (tmp->isTempItem()) {
                    --numTempItems;
                } else {
                    --numItems;
                }
                delete tmp;
                return true;
            } else {
                v = v->next;
            }
        }

        return false;
    }

    /**
     * Delete the item with the given key.
     *
     * @param key the key to delete
     * @return true if the item existed before this call
     */
    bool del(const std::string &key) {
        assert(isActive());
        int bucket_num(0);
        LockHolder lh = getLockedBucket(key, &bucket_num);
        return unlocked_del(key, bucket_num);
    }

    /**
     * Visit all items within this hashtable.
     */
    void visit(HashTableVisitor &visitor);

    /**
     * Visit all items within this call with a depth visitor.
     */
    void visitDepth(HashTableDepthVisitor &visitor);

    /**
     * Get the number of buckets that should be used for initialization.
     *
     * @param s if 0, return the default number of buckets, else return s
     */
    static size_t getNumBuckets(size_t s = 0);

    /**
     * Get the number of locks that should be used for initialization.
     *
     * @param s if 0, return the default number of locks, else return s
     */
    static size_t getNumLocks(size_t s);

    /**
     * Set the default number of buckets.
     */
    static void setDefaultNumBuckets(size_t);

    /**
     * Set the default number of locks.
     */
    static void setDefaultNumLocks(size_t);

    /**
     * Get the max deleted revision seqno seen so far.
     */
    uint64_t getMaxDeletedRevSeqno() const {
        return maxDeletedRevSeqno.get();
    }

    /**
     * Set the max deleted seqno (required during warmup).
     */
    void setMaxDeletedRevSeqno(const uint64_t seqno) {
        maxDeletedRevSeqno.set(seqno);
    }

    /**
     * Update maxDeletedRevSeqno to a (possibly) new value.
     */
    void updateMaxDeletedRevSeqno(const uint64_t seqno) {
        maxDeletedRevSeqno.setIfBigger(seqno);
    }

    Atomic<uint64_t>     maxDeletedRevSeqno;
    Atomic<size_t>       numNonResidentItems;
    Atomic<size_t>       numEjects;
    //! Memory consumed by items in this hashtable.
    Atomic<size_t>       memSize;
    //! Cache size.
    Atomic<size_t>       cacheSize;
    //! Meta-data size.
    Atomic<size_t>       metaDataMemory;

private:
    inline bool isActive() const { return activeState; }
    inline void setActiveState(bool newv) { activeState = newv; }

    size_t               size;
    size_t               n_locks;
    StoredValue        **values;
    Mutex               *mutexes;
    EPStats&             stats;
    StoredValueFactory   valFact;
    Atomic<size_t>       visitors;
    Atomic<size_t>       numItems;
    Atomic<size_t>       numResizes;
    Atomic<size_t>       numTempItems;
    bool                 activeState;

    static size_t                 defaultNumBuckets;
    static size_t                 defaultNumLocks;

    int getBucketForHash(int h) {
        return abs(h % static_cast<int>(size));
    }

    inline int mutexForBucket(int bucket_num) {
        assert(isActive());
        assert(bucket_num >= 0);
        int lock_num = bucket_num % static_cast<int>(n_locks);
        assert(lock_num < static_cast<int>(n_locks));
        assert(lock_num >= 0);
        return lock_num;
    }

    DISALLOW_COPY_AND_ASSIGN(HashTable);
};

#endif  // SRC_STORED_VALUE_H_
