/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef FLUSHER_H
#define FLUSHER_H 1

#include "common.hh"
#include "ep.hh"
#include "dispatcher.hh"
#include "mutation_log.hh"

enum flusher_state {
    initializing,
    running,
    pausing,
    paused,
    stopping,
    stopped
};

class Flusher;

const double DEFAULT_MIN_SLEEP_TIME = 0.1;
const size_t MAX_CHECKPOINT_PERSISTENCE_TIME = 10; // 10 sec.

/**
 * A DispatcherCallback adaptor over Flusher.
 */
class FlusherStepper : public DispatcherCallback {
public:
    FlusherStepper(Flusher* f) : flusher(f) { }
    bool callback(Dispatcher &d, TaskId &t);

    std::string description() {
        return std::string("Running a flusher loop.");
    }

    hrtime_t maxExpectedDuration() {
        // Flusher can take a while, but let's report if it runs for
        // more than ten minutes.
        return 10 * 60 * 1000 * 1000;
    }

private:
    Flusher *flusher;
};

struct HighPriorityVBEntry {
    HighPriorityVBEntry() :
        cookie(NULL), checkpoint(0), start(gethrtime()) { }
    HighPriorityVBEntry(const void *c, uint64_t chk) :
        cookie(c), checkpoint(chk), start(gethrtime()) { }

    const void *cookie;
    uint64_t checkpoint;
    hrtime_t start;
};

/**
 * Manage persistence of data for an EventuallyPersistentStore.
 */
class Flusher {
public:

    Flusher(EventuallyPersistentStore *st, Dispatcher *d) :
        store(st), _state(initializing), dispatcher(d),
        flushRv(0), prevFlushRv(0), minSleepTime(0.1),
        flushQueue(NULL),
        forceShutdownReceived(false), flushPhase(0) {
    }

    ~Flusher() {
        if (_state != stopped) {
            getLogger()->log(EXTENSION_LOG_WARNING, NULL,
                             "Flusher being destroyed in state %s\n",
                             stateName(_state));

        }
    }

    bool stop(bool isForceShutdown = false);
    void wait();
    bool pause();
    bool resume();

    void initialize(TaskId &);

    void start(void);
    void wake(void);
    bool step(Dispatcher&, TaskId &);

    enum flusher_state state() const;
    const char * stateName() const;

    void addHighPriorityVBucket(uint16_t vbid, uint64_t chkid,
                                const void *cookie);
    void removeHighPriorityVBucket(uint16_t vbid);
    void getAllHighPriorityVBuckets(std::vector<uint16_t> &vbs);
    HighPriorityVBEntry getHighPriorityVBEntry(uint16_t vbid);
    size_t getNumOfHighPriorityVBs();

private:
    bool transition_state(enum flusher_state to);
    int doFlush();
    void completeFlush();
    void schedule_UNLOCKED();
    double computeMinSleepTime();

    const char * stateName(enum flusher_state st) const;

    EventuallyPersistentStore   *store;
    volatile enum flusher_state  _state;
    Mutex                        taskMutex;
    TaskId                       task;
    Dispatcher                  *dispatcher;

    // Current flush cycle state.
    int                      flushRv;
    int                      prevFlushRv;
    double                   minSleepTime;
    vb_flush_queue_t        *flushQueue;
    rel_time_t               flushStart;

    Atomic<bool> forceShutdownReceived;

    Mutex priorityVBMutex;
    std::map<uint16_t, HighPriorityVBEntry> priorityVBList;
    size_t flushPhase;

    DISALLOW_COPY_AND_ASSIGN(Flusher);
};

#endif /* FLUSHER_H */