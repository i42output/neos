/*
  tasking.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

     * Neither the name of the copyright holder nor the names of its
       contributors may be used to endorse or promote products derived from
       this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// The Ada runtime: tasking (9). Each task other than the environment task is a coroutine of the host's, with a primary and a 
// secondary stack of its own; the program's thread runs them, one at a time: a task runs until it blocks (an activation, a 
// rendezvous, a protected entry whose barrier is closed, a delay, a master's dependents), when the environment task's scheduler
// resumes the next task that can run (the environment task runs the scheduler when it blocks itself). Switching tasks switches 
// the program's globals that are a task's own (its stack pointer and limit, its pending exception) and the runtime's state of
// it (its secondary stack, its exception occurrence, its finalization chain). A master awaits its dependent tasks (registered
// on its chain when they are created: see finalize_to). Protected actions need no lock: a task is not switched within one.
// Protected entries: the code of their barriers and bodies is called by the runtime (host call) on behalf of the task whose
// action opens a barrier (the proxy model). Times are nanoseconds (Duration is a count of nanoseconds).
// Priorities (Annex D): the scheduler resumes the ready task of the highest active priority (FIFO within priorities: a task that
// becomes ready is queued after those of its priority that are ready, one that is preempted stays first); a running task is
// preempted at its dispatching points (a task that it makes ready (its activation, a rendezvous's end, a protected action's) of
// a higher priority runs first). A task's active priority is its base priority, raised by the callers that it is accepting and
// by its activator during its activation (D.1(21)). Entry queues are ordered by priority under Priority_Queuing (D.4), protected
// actions check their ceilings under Ceiling_Locking (D.3), and held tasks (D.11) do not run. The task dispatching policies
// (D.2.2) are by priority: FIFO_Within_Priorities (the default), Non_Preemptive_FIFO_Within_Priorities (a task is not preempted:
// only blocking, a delay and the Yield operations are its dispatching points), Round_Robin_Within_Priorities (a task whose quantum's
// budget of execution time is exhausted moves to the tail of its priority's ready queue) and EDF_Within_Priorities (ready queues
// ordered by the tasks' active deadlines). The scheduler accounts the thread's execution time to the task that it runs (D.14).
// Preemption by a task that becomes ready other than by the running task's actions (an expired delay, an exhausted budget, an 
// execution-time event) takes effect at the running task's next dispatching point, at a protected action's end, or at a poll
// (the program polls at its loops' iterations when the partition specifies a task dispatching policy or uses execution-time
// events: see poll).

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <vector>
#include "core.hpp"
#include "exec.hpp"

using namespace neos::ada_runtime;

namespace
{
    constexpr std::uint64_t TaskStack = std::uint64_t{ 1u } << 20u;
    constexpr std::uint64_t TaskSecondaryStack = std::uint64_t{ 256u } << 10u;
    constexpr std::int64_t DefaultPriority = 15;    ///< (System.Default_Priority)
    constexpr std::int64_t PriorityLast = 30;       ///< (System.Priority'Last: a protected object's default ceiling (D.3(10)))
    constexpr std::int64_t UnixOrigin = std::int64_t{ 65744 } * 86400 * 1000000000;   ///< (Ada.Calendar.Time's origin, as nanoseconds before 1970)
    constexpr std::int64_t TimeFirst = std::numeric_limits<std::int64_t>::min();   ///< (Ada.Real_Time.Time_First)
    constexpr std::int64_t TimeLast = std::numeric_limits<std::int64_t>::max();    ///< (Ada.Real_Time.Time_Last, Time_Span_Last)
    constexpr std::int64_t DefaultQuantum = 10000000;   ///< (Ada.Dispatching.Round_Robin.Default_Quantum (10 ms))
    constexpr std::int64_t PollInterval = 100000;       ///< (the least time between polls that dispatch (see poll): 100 microseconds)
    constexpr std::int64_t NumberOfCpus = 1;            ///< (System.Multiprocessors.Number_Of_CPUs: the program's thread)

    // the task dispatching policies (D.2.2)
    enum : std::int64_t
    {
        UnspecifiedPolicy = 0,          ///< (none specified: as FIFO_Within_Priorities)
        FifoWithinPriorities = 1,
        NonPreemptiveFifoWithinPriorities = 2,
        RoundRobinWithinPriorities = 3,
        EdfWithinPriorities = 4
    };

    std::int64_t saturating_add(std::int64_t aLeft, std::int64_t aRight)
    {
        if (aRight > 0 && aLeft > TimeLast - aRight)
            return TimeLast;
        if (aRight < 0 && aLeft < TimeFirst - aRight)
            return TimeFirst;
        return aLeft + aRight;
    }

    struct task;

    // an entry call: its caller, the entry, the parameters (the address of a block of the actuals' addresses)
    struct call_record
    {
        task* caller = nullptr;
        std::int64_t entry = 0;
        std::int64_t family = 0;
        std::int64_t params = 0;
        bool accepted = false;
        bool requeued = false;
        bool done = false;
        bool failed = false;
        bool abortable = true;              ///< (it can be cancelled (not requeued without abort))
        std::uint64_t sequence = 0u;        ///< (when it was queued: FIFO among calls of the same priority (Priority_Queuing))
        std::int64_t priority = 0;          ///< (its caller's active priority when it was queued (Priority_Queuing, D.4(9)))
        std::deque<call_record*>* queue = nullptr;  ///< (the queue it is on (its entry's, or the one it was requeued on))
        occurrence exception;
    };

    struct protected_object;

    // an asynchronous select's triggering statement in progress (its delay's expiry, or its entry call)
    struct atc_frame
    {
        std::int64_t deadline = -1;
        std::unique_ptr<call_record> call;
        struct task* target = nullptr;
        protected_object* po = nullptr;
        std::int64_t entry = 0;
        bool skipped = false;               ///< (it completed before the abortable part started: that is not run)
        bool triggered() const;
    };

    struct task
    {
        std::int64_t id = 0;
        neos_rt_coroutine* coroutine = nullptr;
        unsigned function = 0u;             ///< (its body (a procedure: static link, the task object))
        std::int64_t link = 0;
        std::int64_t self = 0;
        execution_state* state = nullptr;
        std::unique_ptr<execution_state> ownState;
        unsigned long long sp = 0u;         ///< (its globals while it is not running)
        unsigned long long limit = 0u;
        unsigned long long exc = 0u;
        bool started = false;
        bool activated = false;
        bool completed = false;
        bool terminated = false;
        bool aborted = false;
        std::int64_t deferAbort = 0;        ///< (abort-deferred operations in progress (Initialize, Adjust, Finalize, 9.8(6-11)): its abort (or an asynchronous select's) takes effect after them)
        std::int64_t elaboration = 0;       ///< (its body's elaboration flag's address (0: none))
        bool terminateRequested = false;
        bool atTerminate = false;           ///< (waiting at a selective accept with an open terminate alternative)
        std::vector<task*> pending;         ///< (the tasks that it created that are not activated yet)
        std::int64_t master = 0;            ///< (the task whose master it depends on (its creator's id): aborted with it)
        std::map<std::int64_t, std::deque<call_record*>> entries;
        std::vector<call_record*> accepting;    ///< (the calls that it is accepting (nested accept statements))
        std::vector<std::int64_t> awaiting;     ///< (the entries whose calls it is waiting for (an accept or a selective accept's open alternatives))
        bool blocked = false;
        std::function<bool()> ready;
        std::int64_t wake = -1;             ///< (monotonic: when a delay that it is waiting on expires (-1: none))
        std::vector<std::unique_ptr<atc_frame>> atc;    ///< (its asynchronous selects' triggering statements in progress)
        std::int64_t priority = DefaultPriority;        ///< (its base priority (D.1))
        task* activator = nullptr;          ///< (the task that activates it: its active priority is inherited until it is activated)
        bool held = false;                  ///< (Ada.Asynchronous_Task_Control.Hold: it does not run until it is continued (D.11))
        bool wasReady = false;              ///< (the scheduler: ready when it last looked (see note_ready))
        std::vector<std::int64_t> ceilings; ///< (the protected actions that it is in: their objects' ceilings (inherited, D.3(9)))
        bool dispatchPending = false;       ///< (a protected action within another served calls: a dispatching point when they end)
        std::int64_t specificHandler = 0;   ///< (Ada.Task_Termination: its specific handler, its dependents' fallback handler (an
        std::int64_t fallbackHandler = 0;   ///< access-to-protected-procedure value: its descriptor's address (procedure, link, object)))
        std::int64_t readySequence = 0;     ///< (the scheduler: its place among the ready tasks of its priority (the lowest first))
        std::int64_t executionTime = 0;     ///< (its execution time (D.14) when it last stopped running (not the environment task's: see execution_time))
        std::int64_t budgetEnd = 0;         ///< (Round_Robin_Within_Priorities: its execution time when its quantum's budget is exhausted (D.2.5))
        std::int64_t deadline = TimeLast;           ///< (its base absolute deadline (D.2.6): a monotonic time)
        std::int64_t relativeDeadline = TimeLast;   ///< (its relative deadline (D.2.6))
        std::int64_t releaseTime = TimeFirst;       ///< (when it was last made ready (D.2.6(14.2/5)))
        std::int64_t releaseDeadline = TimeFirst;   ///< (Delay_Until_And_Set_Deadline: its deadline when it is next made ready (TimeFirst: none))
        std::int64_t releaseOffset = TimeFirst;     ///< (Suspend_Until_True_And_Set_Deadline: its deadline less its release time then (TimeFirst: none))
        std::vector<std::int64_t> deadlineFloors;   ///< (the protected actions that it is in: the active deadlines that they impose (D.3(13.4/5)))
        std::int64_t cpu = 0;               ///< (the processor that it is assigned to (D.16): 0: none (Not_A_Specific_CPU))
        bool cpuFailed = false;             ///< (its CPU is not a processor: its activation fails (D.16(14/5)))
    };

    // a protected object: its entries' barriers and bodies (functions), and their queues
    struct protected_object
    {
        std::int64_t self = 0;
        std::int64_t table = 0;             ///< (by entry: its barrier's function, its body's function (i64s))
        std::int64_t count = 0;
        std::int64_t link = 0;
        std::map<std::int64_t, std::deque<call_record*>> queues;
        std::int64_t ceiling = PriorityLast;    ///< (its ceiling priority (D.3))
        task* user = nullptr;                   ///< (the task whose protected action it is in (if any))
        std::int64_t uses = 0;                  ///< (its protected actions in progress (nested: a function's within a procedure's))
        std::int64_t relativeDeadline = 0;      ///< (its relative deadline (D.3(13.2/5), D.5.2))
        std::int64_t priority = -1;             ///< (its Priority attribute if assigned (its ceiling at its protected action's end, D.5.2(5/3)); -1: not)
        std::int64_t cpu = 0;                   ///< (the processor that it is assigned to (D.16(15/5)): 0: none)
    };

    task gEnvironment;
    task* gCurrent = nullptr;
    std::map<std::int64_t, std::unique_ptr<task>> gTasks;
    std::int64_t gNextTask = 2;
    std::int64_t gLastRun = 0;
    std::map<std::int64_t, std::unique_ptr<protected_object>> gProtected;
    call_record* gProtectedCall = nullptr;      ///< (the call whose entry body is running)
    std::int64_t gNextProtected = 1;
    bool gPriorityQueuing = false;              ///< (pragma Queuing_Policy (Priority_Queuing) (D.4))
    bool gCeilingLocking = false;               ///< (pragma Locking_Policy (Ceiling_Locking) (D.3))
    std::int64_t gReadySequence = 0;            ///< (a task that becomes ready: after the ready tasks of its priority (D.2.1(5)))
    std::int64_t gPreemptedSequence = 0;        ///< (a task that is preempted: before them (D.2.1(6)))
    std::uint64_t gCallSequence = 0u;
    std::uint64_t gEntryBodies = 0u;            ///< (the protected entry bodies run (for their callers): see protected_action_end)
    std::array<std::int64_t, 32> gPolicies{};   ///< (by priority: its task dispatching policy (D.2.2))
    std::array<std::int64_t, 32> gQuanta = []() { std::array<std::int64_t, 32> q; q.fill(DefaultQuantum); return q; }();  ///< (by priority: its quantum (D.2.5))
    bool gGenerateDeadlines = false;            ///< (pragma Generate_Deadlines (D.2.6))
    std::int64_t gSliceStart = 0;               ///< (the thread's execution time when the running task (not the environment task) was resumed)
    std::int64_t gOthersTime = 0;               ///< (the execution time of the tasks other than the environment task)
    std::map<std::int64_t, std::int64_t> gFinalTimes;   ///< (the execution times of the tasks no longer known whose times are needed (execution-time events))
    // the partition's restrictions checked at run time (D.7): their values (-1: none)
    std::int64_t gMaxEntryQueueLength = -1;     ///< (Max_Entry_Queue_Length: Program_Error at a call or requeue that exceeds it (D.7(19.1/2)))
    std::int64_t gMaxTasks = -1;                ///< (Max_Tasks: Storage_Error at a creation that exceeds it (D.7(19/1)))
    std::int64_t gTasksCreated = 0;
    std::int64_t gMaxAtcNesting = -1;           ///< (Max_Asynchronous_Select_Nesting: Storage_Error at a nesting that exceeds it (D.7(18/1)))
    bool gNoUnassignedCpu = false;              ///< (No_Tasks_Unassigned_To_CPU: Program_Error for Not_A_Specific_CPU (D.7(10.10/4)))
    bool gNoAllocatorsAfterElaboration = false; ///< (No_Standard_Allocators_After_Elaboration: Storage_Error after the elaboration (D.7(19.3/3)))
    bool gElaborated = false;                   ///< (the partition's library items are elaborated)
    std::atomic<std::uint32_t> gInterruptsPending{ 0u };    ///< (the interrupts that have occurred and not been handled: by Interrupt_Id)
    std::int64_t gInterruptsAttached = 0;                   ///< (the interrupts that have handlers)
    void deliver_interrupts();


    std::int64_t monotonic_now()
    {
        using namespace std::chrono;
        return static_cast<std::int64_t>(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
    }

    std::int64_t realtime_now()
    {
        using namespace std::chrono;
        return static_cast<std::int64_t>(duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
    }

    task* current()
    {
        if (gCurrent == nullptr)
        {
            gEnvironment.id = 1;
            gEnvironment.state = &state();
            gEnvironment.started = true;
            gEnvironment.activated = true;
            gCurrent = &gEnvironment;
        }
        return gCurrent;
    }

    task* task_of(std::int64_t aId)
    {
        if (aId == 1)
            return current() == &gEnvironment || gEnvironment.id == 1 ? &gEnvironment : nullptr;
        auto t = gTasks.find(aId);
        return t != gTasks.end() ? t->second.get() : nullptr;
    }

    bool callable(task const& aTask)
    {
        return !aTask.completed && !aTask.terminated && !aTask.aborted;
    }

    void raise_occurrence(occurrence const& aOccurrence)
    {
        state().current = aOccurrence;
        set_pending(aOccurrence.id);
    }

    occurrence take_pending()
    {
        // (the running task's pending exception (if any): taken (no longer pending))
        auto& s = state();
        occurrence result;
        if (s.excGlobal != nullptr && *s.excGlobal != 0u)
        {
            result = s.current;
            result.id = static_cast<std::int64_t>(*s.excGlobal);
            *s.excGlobal = 0u;
        }
        return result;
    }

    // --- the scheduler

    void finish(task& aTask);

    void switch_to(task& aTask)
    {
        // (the environment task resumes a task: the globals and the runtime's state are the task's until it suspends itself)
        auto& env = gEnvironment;
        auto& s = *env.state;
        env.sp = s.spGlobal != nullptr ? *s.spGlobal : 0u;
        env.limit = s.limitGlobal != nullptr ? *s.limitGlobal : 0u;
        env.exc = s.excGlobal != nullptr ? *s.excGlobal : 0u;
        if (s.spGlobal != nullptr)
        {
            *s.spGlobal = aTask.sp;
            *s.limitGlobal = aTask.limit;
            *s.excGlobal = aTask.exc;
        }
        set_state(aTask.state);
        gCurrent = &aTask;
        gSliceStart = thread_cpu_time();
        int const finished = host()->coroutine_resume(aTask.coroutine);
        // (the thread's execution time while it ran: the task's (D.14))
        auto const used = thread_cpu_time() - gSliceStart;
        aTask.executionTime += used;
        gOthersTime += used;
        if (s.spGlobal != nullptr)
        {
            aTask.sp = *s.spGlobal;
            aTask.limit = *s.limitGlobal;
            aTask.exc = *s.excGlobal;
            *s.spGlobal = env.sp;
            *s.limitGlobal = env.limit;
            *s.excGlobal = env.exc;
        }
        set_state(env.state);
        gCurrent = &env;
        if (finished)
        {
            aTask.terminated = true;
            host()->coroutine_delete(aTask.coroutine);
            aTask.coroutine = nullptr;
        }
    }

    std::int64_t active_priority(task const& aTask, int aDepth = 0, bool aCeilings = true);

    bool runnable(task const& aTask)
    {
        if (!aTask.started || aTask.terminated || aTask.coroutine == nullptr)
            return false;
        // (held: its held priority is below every priority, it does not run unless it inherits one (a rendezvous, D.11(4/2)))
        if (aTask.held && !aTask.aborted && active_priority(aTask) < 0)
            return false;
        if (!aTask.blocked || aTask.aborted || aTask.terminateRequested)
            return true;
        if (aTask.wake >= 0 && monotonic_now() >= aTask.wake)
            return true;
        return aTask.ready && aTask.ready();
    }

    // (a task's active priority: its base priority, or the active priority of a caller that it is accepting (a rendezvous), or of
    // its activator (during its activation), if higher (D.1(21)))
    std::int64_t active_priority(task const& aTask, int aDepth, bool aCeilings)
    {
        auto result = aTask.held ? std::int64_t{ -1 } : aTask.priority;
        if (aCeilings)
            for (auto c : aTask.ceilings)
                result = std::max(result, c);
        if (aDepth > 64)
            return result;
        for (auto const* c : aTask.accepting)
            if (c->caller != nullptr && c->caller != &aTask)
                result = std::max(result, active_priority(*c->caller, aDepth + 1));
        // (waiting to accept a call that has been made: its rendezvous has begun (the task accepts it when it runs))
        if (aTask.blocked)
            for (auto e : aTask.awaiting)
                if (auto q = aTask.entries.find(e); q != aTask.entries.end() && !q->second.empty())
                    if (auto const* c = q->second.front(); c->caller != nullptr && c->caller != &aTask)
                        result = std::max(result, active_priority(*c->caller, aDepth + 1));
        if (aTask.activator != nullptr && !aTask.activated && aTask.activator != &aTask)
            result = std::max(result, active_priority(*aTask.activator, aDepth + 1));
        return result;
    }

    // (the task dispatching policy of a priority (D.2.2))
    std::int64_t policy_of(std::int64_t aPriority)
    {
        return aPriority >= 0 && aPriority < static_cast<std::int64_t>(gPolicies.size()) ? gPolicies[static_cast<std::size_t>(aPriority)] : UnspecifiedPolicy;
    }

    // (a task's execution time (D.14): the thread's while the scheduler ran it; the environment task's: the rest of the thread's)
    std::int64_t execution_time(task const& aTask)
    {
        auto const now = thread_cpu_time();
        bool const othersRunning = gCurrent != nullptr && gCurrent != &gEnvironment;
        if (&aTask == &gEnvironment)
            return now - gOthersTime - (othersRunning ? now - gSliceStart : 0);
        return aTask.executionTime + (&aTask == gCurrent ? now - gSliceStart : 0);
    }

    // (Round_Robin_Within_Priorities: a task added or moved to the tail of the ready queue for its base priority has its priority's
    // quantum as its budget (D.2.5(11/2)))
    void replenish(task& aTask)
    {
        auto const priority = std::clamp<std::int64_t>(aTask.priority, 0, static_cast<std::int64_t>(gQuanta.size()) - 1);
        if (policy_of(priority) == RoundRobinWithinPriorities)
            aTask.budgetEnd = saturating_add(execution_time(aTask), gQuanta[static_cast<std::size_t>(priority)]);
    }

    // (a task made ready (added to a ready queue): its release time, its deadline (Delay_Until_And_Set_Deadline's, or under
    // Generate_Deadlines (D.2.6(12.1/5, 15/5))), its budget)
    void made_ready(task& aTask)
    {
        auto const now = monotonic_now();
        aTask.releaseTime = aTask.wake >= 0 && aTask.wake <= now ? aTask.wake : now;
        if (aTask.releaseDeadline != TimeFirst)
        {
            aTask.deadline = aTask.releaseDeadline;
            aTask.releaseDeadline = TimeFirst;
        }
        else if (aTask.releaseOffset != TimeFirst)
        {
            aTask.deadline = saturating_add(aTask.releaseTime, aTask.releaseOffset);
            aTask.releaseOffset = TimeFirst;
        }
        else if (gGenerateDeadlines)
            aTask.deadline = saturating_add(aTask.releaseTime, aTask.relativeDeadline);
        replenish(aTask);
    }

    // (a task's active deadline (D.2.6(14/5)): its base deadline, or the earlier deadline that it inherits from a caller that it is
    // accepting (29.1/5), from its activator during its activation (29.2/5), or from a protected action that it is in (D.3(13.4/5)))
    std::int64_t active_deadline(task const& aTask, int aDepth = 0)
    {
        auto result = aTask.deadline;
        for (auto d : aTask.deadlineFloors)
            result = std::min(result, d);
        if (aDepth > 64)
            return result;
        for (auto const* c : aTask.accepting)
            if (c->caller != nullptr && c->caller != &aTask)
                result = std::min(result, active_deadline(*c->caller, aDepth + 1));
        if (aTask.activator != nullptr && !aTask.activated && aTask.activator != &aTask)
            result = std::min(result, active_deadline(*aTask.activator, aDepth + 1));
        return result;
    }

    // (whether a ready task of active priority aPriority comes before another of active priority aOther: the higher priority first;
    // in a priority's ready queue the first to be queued (FIFO), or under EDF_Within_Priorities the earliest active deadline
    // (D.2.6(17/5)))
    bool before(task const& aTask, std::int64_t aPriority, task const& aOther, std::int64_t aOtherPriority)
    {
        if (aPriority != aOtherPriority)
            return aPriority > aOtherPriority;
        if (policy_of(aPriority) == EdfWithinPriorities)
            if (auto const d = active_deadline(aTask), e = active_deadline(aOther); d != e)
                return d < e;
        return aTask.readySequence < aOther.readySequence;
    }

    // (the tasks that have become ready since the scheduler last looked: queued (their place: after the ready tasks))
    void note_ready()
    {
        for (auto const& [id, t] : gTasks)
        {
            bool const ready = runnable(*t);
            if (ready && !t->wasReady)
            {
                t->readySequence = ++gReadySequence;
                made_ready(*t);
            }
            t->wasReady = ready;
        }
    }

    // (the ready task of the highest active priority (the first of them to become ready) whose active priority is at least aFloor
    // (above it if aAbove), or null)
    task* best_ready(std::int64_t aFloor, bool aAbove)
    {
        note_ready();
        task* best = nullptr;
        std::int64_t bestPriority = 0;
        for (auto const& [id, t] : gTasks)
        {
            if (t.get() == gCurrent || !runnable(*t))
                continue;
            auto const p = active_priority(*t);
            if (p < aFloor || (aAbove && p == aFloor))
                continue;
            if (best == nullptr || before(*t, p, *best, bestPriority))
            {
                best = t.get();
                bestPriority = p;
            }
        }
        return best;
    }

    bool run_one()
    {
        // (the ready task of the highest priority, resumed until it suspends itself)
        auto* next = best_ready(std::numeric_limits<std::int64_t>::min(), false);
        if (next == nullptr)
            return false;
        gLastRun = next->id;
        switch_to(*next);
        return true;
    }

    // (a dispatching point of the running task: preempted by a ready task of a higher priority (it stays first among the ready
    // tasks of its priority))
    std::int64_t atc_triggered(task const& aTask);

    // (the ready task that preempts the running task (of active priority aPriority), if any: one of a higher priority, or under
    // EDF_Within_Priorities one of the same priority with an earlier active deadline (D.2.6(21.1/5)))
    task* preemptor(task const& aSelf, std::int64_t aPriority)
    {
        auto* best = best_ready(aPriority, false);
        if (best == nullptr)
            return nullptr;
        auto const p = active_priority(*best);
        if (p > aPriority || (p == aPriority && policy_of(p) == EdfWithinPriorities && active_deadline(*best) < active_deadline(aSelf)))
            return best;
        return nullptr;
    }

    void yield();

    // (aYield: Yield_To_Higher's, a dispatching point under Non_Preemptive_FIFO_Within_Priorities too)
    void dispatch(bool aYield = false)
    {
        auto* self = current();
        // (the entry body that it is running (if any): its own (not the preempting tasks'))
        auto* const protectedCall = gProtectedCall;
        bool const inProtectedAction = !self->ceilings.empty() || protectedCall != nullptr;
        auto const mine = active_priority(*self);
        // (Non_Preemptive_FIFO_Within_Priorities: blocking, a delay and the Yield operations are the only dispatching points (D.2.4(9/3)))
        bool const preemptive = aYield || policy_of(mine) != NonPreemptiveFifoWithinPriorities;
        if (!preemptive)
        {
            // (not preempted)
        }
        // (Round_Robin_Within_Priorities: a task whose budget is exhausted, without an inherited priority and not within a protected
        // action, moves to the tail of its priority's ready queue (D.2.5(14/2)))
        else if (!inProtectedAction && policy_of(mine) == RoundRobinWithinPriorities && mine == self->priority && execution_time(*self) >= self->budgetEnd)
            yield();
        else if (self == &gEnvironment)
        {
            gProtectedCall = nullptr;
            while (auto* next = preemptor(gEnvironment, active_priority(gEnvironment)))
                switch_to(*next);
            gProtectedCall = protectedCall;
        }
        else
        {
            bool environment = gEnvironment.blocked && gEnvironment.ready && gEnvironment.ready();
            if (environment)
            {
                auto const p = active_priority(gEnvironment);
                environment = p > mine || (p == mine && policy_of(p) == EdfWithinPriorities && active_deadline(gEnvironment) < active_deadline(*self));
            }
            if (environment || preemptor(*self, mine) != nullptr)
            {
                self->blocked = false;
                self->wasReady = true;
                self->readySequence = --gPreemptedSequence;
                gProtectedCall = nullptr;
                host()->coroutine_suspend();
                gProtectedCall = protectedCall;
            }
        }
        // (aborted, or its asynchronous select's triggering statement completed (by a task that preempted it perhaps): that takes
        // effect now (not within a protected action, which is abort-deferred))
        if (!inProtectedAction && self->deferAbort == 0 && (state().excGlobal == nullptr || *state().excGlobal == 0u))
        {
            if (self->aborted && self != &gEnvironment)
                set_pending(AbortSignal);
            else if (!self->atc.empty() && atc_triggered(*self) != 0)
                set_pending(AtcSignalBase + atc_triggered(*self));
        }
    }

    // (a point where the running task polls for the delays that have expired meanwhile (a protected action's end, E'Count: as if their
    // timers interrupted it): it is preempted by the tasks that they make ready, if of a higher priority)
    void preempt_poll()
    {
        auto* self = current();
        if (!self->ceilings.empty() || gProtectedCall != nullptr)
            return;
        deliver_interrupts();
        if (gTasks.empty())
            return;
        auto const now = monotonic_now();
        bool expired = gEnvironment.blocked && !gEnvironment.wasReady && gEnvironment.wake >= 0 && gEnvironment.wake <= now;
        for (auto const& [id, t] : gTasks)
            if (t->blocked && !t->wasReady && t->wake >= 0 && t->wake <= now)
            {
                expired = true;
                break;
            }
        if (expired)
            dispatch();
    }

    // (the running task yields to the ready tasks of its priority (a delay that has expired: it is queued after them, D.2.1(5)), with
    // a new budget (D.2.5(11/2)))
    void yield()
    {
        auto* self = current();
        replenish(*self);
        if (self == &gEnvironment)
        {
            auto const mine = active_priority(gEnvironment);
            note_ready();
            auto const mark = gReadySequence;
            while (auto* next = best_ready(mine, false))
            {
                if (active_priority(*next) == mine && next->readySequence > mark)
                    break;
                switch_to(*next);
            }
            return;
        }
        self->blocked = false;
        self->wasReady = true;
        self->readySequence = ++gReadySequence;
        host()->coroutine_suspend();
    }

    bool idle(std::int64_t aOwnWake)
    {
        // (no task can run: until the earliest delay expires (false: none (a deadlock)), or an interrupt occurs (if any has a handler:
        // waited for in slices))
        std::int64_t earliest = aOwnWake;
        for (auto const& [id, t] : gTasks)
            if (t->started && !t->terminated && t->blocked && t->wake >= 0 && (earliest < 0 || t->wake < earliest))
                earliest = t->wake;
        if (gInterruptsAttached > 0)
        {
            constexpr std::int64_t Slice = 1000000;
            if (gInterruptsPending.load() != 0u)
                return true;
            auto const now = monotonic_now();
            std::this_thread::sleep_for(std::chrono::nanoseconds{ earliest < 0 ? Slice : std::clamp<std::int64_t>(earliest - now, 0, Slice) });
            return true;
        }
        if (earliest < 0)
            return false;
        auto const now = monotonic_now();
        if (earliest > now)
            std::this_thread::sleep_for(std::chrono::nanoseconds{ earliest - now });
        return true;
    }

    // (the running task waits until the condition holds (or until its wake time, if any, has passed: then the condition is
    // checked again by the caller); false: it cannot hold (a deadlock), or the task is aborted (the abort signal is pending))
    std::int64_t atc_triggered(task const& aTask)
    {
        // (the outermost asynchronous select whose triggering statement has completed: its depth (0: none))
        for (std::size_t i = 0u; i < aTask.atc.size(); ++i)
            if (aTask.atc[i]->triggered())
                return static_cast<std::int64_t>(i + 1u);
        return 0;
    }

    bool block_until(std::function<bool()> const& aCondition, std::int64_t aWake, std::function<bool()> const& aInterruptible)
    {
        auto* self = current();
        auto const expired = [&]() { return aWake >= 0 && monotonic_now() >= aWake; };
        // (an abort-deferred wait (e.g. for an entry call requeued without abort, 9.5.4(13)): not interrupted by an abort)
        auto const interruptible = [&aInterruptible, self]() { return self->deferAbort == 0 && (!aInterruptible || aInterruptible()); };
        if (self->aborted && self != &gEnvironment && interruptible())
        {
            set_pending(AbortSignal);
            return false;
        }
        // (an asynchronous select's abortable part waiting (an abort completion point): aborted when its triggering statement
        // has completed)
        auto const triggered = [self, interruptible]() { return !self->atc.empty() && interruptible() && atc_triggered(*self) != 0; };
        if (triggered())
        {
            set_pending(AtcSignalBase + atc_triggered(*self));
            return false;
        }
        if (aCondition() || expired())
            return true;
        auto wake = aWake;
        for (auto const& f : self->atc)
            if (f->deadline >= 0 && (wake < 0 || f->deadline < wake))
                wake = f->deadline;
        if (triggered())
        {
            set_pending(AtcSignalBase + atc_triggered(*self));
            return false;
        }
        self->blocked = true;
        self->ready = self->atc.empty() ? aCondition : std::function<bool()>{ [aCondition, triggered]() { return aCondition() || triggered(); } };
        self->wake = wake;
        bool result = true;
        if (self == &gEnvironment)
        {
            while (!aCondition() && !expired() && !triggered())
                if (deliver_interrupts(), !run_one() && !idle(wake))
                {
                    // (a deadlock: no task can run, none is delayed)
                    if (!aCondition() && !triggered())
                        result = false;
                    break;
                }
            made_ready(gEnvironment);
            // (ready again: the ready tasks of a higher priority first)
            if (result)
                while (auto* next = best_ready(active_priority(gEnvironment), true))
                    switch_to(*next);
        }
        else
        {
            while (!aCondition() && !expired() && !triggered() && !(self->aborted && interruptible()) && !self->terminateRequested)
            {
                host()->coroutine_suspend();
                // (resumed (it seemed ready: e.g. aborted in an abort-deferred wait) but still waiting: after the ready tasks of its
                // priority (else it would be resumed first again, and they would not run))
                self->readySequence = ++gReadySequence;
            }
            if (self->aborted && interruptible() && !aCondition())
            {
                set_pending(AbortSignal);
                result = false;
            }
        }
        if (result && !aCondition() && triggered())
        {
            set_pending(AtcSignalBase + atc_triggered(*self));
            result = false;
        }
        self->blocked = false;
        self->ready = nullptr;
        self->wake = -1;
        return result;
    }

    bool block_until(std::function<bool()> const& aCondition, std::int64_t aWake = -1)
    {
        return block_until(aCondition, aWake, std::function<bool()>{});
    }

    void call_termination_handler(task& aTask, occurrence const& aOccurrence);

    void task_main(void* aData)
    {
        auto& t = *static_cast<task*>(aData);
        neos_rt_type const parameters[2] = { NEOS_RT_I64, NEOS_RT_I64 };
        neos_rt_value arguments[2];
        arguments[0].i64 = t.link;
        arguments[1].i64 = t.self;
        neos_rt_value result;
        if (!t.aborted)
            host()->call(t.function, 2u, parameters, arguments, NEOS_RT_VOID, &result);
        // (an exception that the task body did not handle: the task completes)
        auto const failure = take_pending();
        finish(t);
        call_termination_handler(t, failure);
    }

    void finish(task& aTask)
    {
        // (a task completed: the calls queued on its entries fail (Tasking_Error))
        aTask.completed = true;
        // (the queues emptied, not removed: the task itself may be waiting on one (an accept: then it is aborted))
        for (auto& [entry, queue] : aTask.entries)
        {
            for (auto* c : queue)
            {
                c->failed = true;
                c->done = true;
                c->exception = occurrence{};
                c->exception.id = 0;    // (Tasking_Error: set by the caller)
            }
            queue.clear();
        }
    }

    std::int64_t clock_until(std::int64_t aKind, std::int64_t aValue)
    {
        // (a delay as a monotonic time: 0: none (-1); 1: relative (a Duration); 2: until an Ada.Calendar.Time; 3: until an 
        // Ada.Real_Time.Time)
        switch (aKind)
        {
        case 1: return monotonic_now() + std::max<std::int64_t>(aValue, 0);
        case 2: return monotonic_now() + std::max<std::int64_t>(aValue + UnixOrigin - realtime_now(), 0);
        case 3: return aValue;
        default: return -1;
        }
    }

    void tasking_error(std::string const& aMessage)
    {
        raise_predefined(3, aMessage);
    }

    // --- tasks

    std::map<std::int64_t, std::vector<std::int64_t>> gCollections;
    std::int64_t gNextCollection = 1;

    // (a collection: the tasks that an access type's allocators create, awaited by the access type's master (registered on its
    // chain: the running task's))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_collection_create()
    {
        auto const id = gNextCollection++;
        gCollections[id];
        current()->state->finalization.push_back(execution_state::finalizable{ id, CollectionEntry, 0 });
        return id;
    }

    // (a new task (not yet activated): its body, that's static link, the task object; registered on its master's chain (the 
    // creator's), or in a collection (an allocator's: aCollection))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_task_create(std::int64_t aFunction, std::int64_t aLink, std::int64_t aSelf, std::int64_t aCollection)
    {
        auto* creator = current();
        if (gMaxTasks >= 0 && ++gTasksCreated > gMaxTasks)
        {
            raise_predefined(2, "the partition's restriction Max_Tasks is exceeded");
            return 0;
        }
        auto t = std::make_unique<task>();
        t->id = gNextTask++;
        t->function = static_cast<unsigned>(aFunction);
        t->link = aLink;
        t->self = aSelf;
        t->ownState = std::make_unique<execution_state>();
        t->state = t->ownState.get();
        auto& s = *t->state;
        auto const& creatorState = *creator->state;
        s.spGlobal = creatorState.spGlobal;
        s.limitGlobal = creatorState.limitGlobal;
        s.excGlobal = creatorState.excGlobal;
        auto const stack = allocate_segment(TaskStack);
        auto const secondary = allocate_segment(TaskSecondaryStack);
        if (stack == 0 || secondary == 0)
        {
            raise_predefined(2, "cannot allocate a task's stack");
            return 0;
        }
        s.stackBase = static_cast<std::uint64_t>(stack);
        s.ssBase = static_cast<std::uint64_t>(secondary);
        s.ssTop = s.ssBase;
        s.ssLimit = s.ssBase + TaskSecondaryStack;
        t->sp = static_cast<unsigned long long>(stack);
        t->limit = static_cast<unsigned long long>(stack) + TaskStack - 65536u;
        t->exc = 0u;
        auto const id = t->id;
        // (its base priority: its creator's, unless specified (see task_priority, D.1(19))); its processor: its activator's (D.16(14/5))
        t->priority = creator->priority;
        t->cpu = creator->cpu;
        creator->pending.push_back(t.get());
        t->master = creator->id;
        // (its master: awaits it)
        if (aCollection != 0)
            gCollections[aCollection].push_back(id);
        else
            creator->state->finalization.push_back(execution_state::finalizable{ id, TaskEntry, 0 });
        gTasks[id] = std::move(t);
        return id;
    }

    // (the tasks that the running task created and has not activated: activated; it waits until their activation is complete
    // (Tasking_Error if one of them failed))
    void activate(std::vector<task*> const& pending);

    void activate_from(std::size_t aMark)
    {
        auto* self = current();
        if (self->pending.size() <= aMark)
            return;
        std::vector<task*> pending(self->pending.begin() + static_cast<std::ptrdiff_t>(aMark), self->pending.end());
        self->pending.resize(aMark);
        activate(pending);
    }

    // (the pending tasks [aFirst, aEnd) (a package specification's) and from aFrom (its body's declarative part's): activated
    // together)
    NEOS_ADA_PRIMITIVE void neos_ada_task_activate_ranges(std::int64_t aFirst, std::int64_t aEnd, std::int64_t aFrom)
    {
        auto* self = current();
        auto const size = static_cast<std::int64_t>(self->pending.size());
        auto const from = std::clamp<std::int64_t>(aFrom, 0, size);
        auto const end = std::clamp<std::int64_t>(aEnd, 0, from);
        auto const first = std::clamp<std::int64_t>(aFirst, 0, end);
        std::vector<task*> pending(self->pending.begin() + first, self->pending.begin() + end);
        pending.insert(pending.end(), self->pending.begin() + from, self->pending.end());
        self->pending.erase(self->pending.begin() + from, self->pending.end());
        self->pending.erase(self->pending.begin() + first, self->pending.begin() + end);
        if (!pending.empty())
            activate(pending);
    }

    void activate(std::vector<task*> const& pending)
    {
        // (one whose body is not yet elaborated: none activated (all completed), then Program_Error (9.2(5), 3.11(14)))
        bool unelaborated = false;
        for (auto* t : pending)
            if (t->elaboration != 0)
            {
                std::int64_t flag = 0;
                std::memcpy(&flag, memory() + t->elaboration, sizeof(flag));
                unelaborated = unelaborated || flag == 0;
            }
        if (unelaborated)
        {
            for (auto* t : pending)
            {
                t->terminated = true;
                t->activated = true;
                finish(*t);
            }
            raise_predefined(1, "task activated before its body is elaborated");
            return;
        }
        for (auto* t : pending)
        {
            if (t->aborted)
            {
                t->terminated = true;
                t->activated = true;
                continue;
            }
            // (its CPU not a processor: it fails, and becomes completed (D.16(14/5)))
            if (t->cpuFailed)
            {
                t->terminated = true;
                finish(*t);
                continue;
            }
            t->coroutine = host()->coroutine_new(&task_main, t);
            t->started = true;
            t->activator = current();
        }
        block_until([pending]() { return std::all_of(pending.begin(), pending.end(), [](task* t) { return t->activated || t->completed || t->terminated; }); });
        for (auto* t : pending)
            t->activator = nullptr;
        // (one that failed (its activation raised an exception): Tasking_Error; one aborted meanwhile isn't a failure (9.2(5)))
        for (auto* t : pending)
            if (!t->activated && !t->aborted)
            {
                tasking_error("task activation failed");
                return;
            }
    }

    // (a protected object deallocated: finalized (its queued calls fail, 9.4(20.1/2)))
    NEOS_ADA_PRIMITIVE void neos_ada_po_finalize(std::int64_t aHandle)
    {
        protected_finalize(aHandle);
    }

    // (a task's object moved since it was created (a function's return object): where it is now; its master the running one's
    // (see task_unregister))
    NEOS_ADA_PRIMITIVE void neos_ada_task_relocate(std::int64_t aTask, std::int64_t aSelf)
    {
        auto* t = task_of(aTask);
        if (t == nullptr)
            return;
        t->self = aSelf;
        auto& chain = current()->state->finalization;
        if (std::none_of(chain.begin(), chain.end(), [aTask](auto const& f) { return f.object == aTask && f.function == TaskEntry; }))
            chain.push_back(execution_state::finalizable{ aTask, TaskEntry, 0 });
    }

    // (a return object's task (not yet activated): no longer awaited by the function's master (the caller's will, see task_relocate))
    NEOS_ADA_PRIMITIVE void neos_ada_task_unregister(std::int64_t aTask)
    {
        auto& chain = current()->state->finalization;
        for (auto i = chain.size(); i-- > 0u; )
            if (chain[i].object == aTask && chain[i].function == TaskEntry)
            {
                chain.erase(chain.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
    }

    NEOS_ADA_PRIMITIVE void neos_ada_task_elaboration(std::int64_t aTask, std::int64_t aFlag)
    {
        if (auto* t = task_of(aTask))
            t->elaboration = aFlag;
    }

    NEOS_ADA_PRIMITIVE void neos_ada_task_activate()
    {
        activate_from(0u);
    }

    // (the tasks that the running task has created and not activated: their number (an allocator's are those created after it))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_task_pending()
    {
        return static_cast<std::int64_t>(current()->pending.size());
    }

    // (the tasks created after a mark that are not activated (an exception left their declarative part): terminated, never
    // activated (9.2(3/2)))
    NEOS_ADA_PRIMITIVE void neos_ada_task_discard_from(std::int64_t aMark)
    {
        auto* self = current();
        auto const mark = static_cast<std::size_t>(aMark < 0 ? 0 : aMark);
        for (std::size_t i = mark; i < self->pending.size(); ++i)
        {
            auto* t = self->pending[i];
            t->terminated = true;
            t->activated = true;
            finish(*t);
        }
        if (self->pending.size() > mark)
            self->pending.resize(mark);
    }

    // (the tasks created after a mark (see task_pending): activated (an allocator's, 9.2(5)))
    NEOS_ADA_PRIMITIVE void neos_ada_task_activate_from(std::int64_t aMark)
    {
        activate_from(static_cast<std::size_t>(aMark < 0 ? 0 : aMark));
    }

    // (the running task's activation is complete (its declarative part elaborated): its activator's priority no longer inherited
    // (a dispatching point))
    NEOS_ADA_PRIMITIVE void neos_ada_task_activated()
    {
        current()->activated = true;
        dispatch();
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_task_callable(std::int64_t aTask)
    {
        auto* t = task_of(aTask);
        return t != nullptr && callable(*t) ? 1 : 0;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_task_terminated(std::int64_t aTask)
    {
        auto* t = task_of(aTask);
        return t == nullptr || t->terminated ? 1 : 0;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_current_task()
    {
        return current()->id;
    }

    // (abort statements: each task aborted (it completes at its next scheduling point; the running one now))
    NEOS_ADA_PRIMITIVE void neos_ada_task_abort(std::int64_t aTask)
    {
        auto* t = task_of(aTask);
        if (t == nullptr || t->terminated)
            return;
        t->aborted = true;
        if (!t->started)
        {
            t->terminated = true;
            t->activated = true;
            t->completed = true;
        }
        finish(*t);
        // (and the tasks that depend on it (9.8(4)))
        std::vector<std::int64_t> dependents;
        for (auto const& [id, u] : gTasks)
            if (u->master == t->id && !u->terminated && !u->aborted)
                dependents.push_back(id);
        for (auto id : dependents)
            neos_ada_task_abort(id);
        if (t == current() && t != &gEnvironment)
            set_pending(AbortSignal);
    }

    // (a delay statement)
    NEOS_ADA_PRIMITIVE void neos_ada_delay(std::int64_t aKind, std::int64_t aValue)
    {
        auto const wake = clock_until(aKind, aValue);
        if (wake <= monotonic_now())
        {
            // (an abort completion point: an asynchronous select's abortable part aborted if its triggering statement has
            // completed)
            if (auto* self = current(); self->deferAbort == 0 && !self->atc.empty() && atc_triggered(*self) != 0)
            {
                set_pending(AtcSignalBase + atc_triggered(*self));
                return;
            }
            // (a delay that has expired: the task yields to the ready tasks of its priority (it is queued after them, D.2.1(5)))
            yield();
            if (auto* self = current(); self != &gEnvironment && self->aborted && self->deferAbort == 0)
                set_pending(AbortSignal);
            return;
        }
        block_until([wake]() { return monotonic_now() >= wake; }, wake);
    }

    // --- entry queues: FIFO, or by their callers' active priorities (FIFO among calls of the same priority) under Priority_Queuing 
    // (D.4(9-10))

    std::int64_t call_priority(call_record const& aCall)
    {
        return aCall.priority;
    }

    bool enqueue_check(std::deque<call_record*> const& aQueue)
    {
        // (Max_Entry_Queue_Length: Program_Error at a call or requeue that exceeds it (D.7(19.1/2)))
        if (gMaxEntryQueueLength >= 0 && static_cast<std::int64_t>(aQueue.size()) >= gMaxEntryQueueLength)
        {
            raise_predefined(1, "the partition's restriction Max_Entry_Queue_Length is exceeded");
            return false;
        }
        return true;
    }

    void enqueue(std::deque<call_record*>& aQueue, call_record* aCall)
    {
        aCall->sequence = ++gCallSequence;
        aCall->queue = &aQueue;
        // (not the ceiling of the protected action that queues it (its own, or the entry body that requeues it))
        aCall->priority = aCall->caller != nullptr ? active_priority(*aCall->caller, 0, false) : 0;
        if (!gPriorityQueuing)
        {
            aQueue.push_back(aCall);
            return;
        }
        auto const priority = call_priority(*aCall);
        auto i = std::find_if(aQueue.begin(), aQueue.end(), [priority](call_record const* c) { return call_priority(*c) < priority; });
        aQueue.insert(i, aCall);
    }

    // (a task's base priority changed while it is blocked on an entry call: the call placed by its new active priority (D.4(10)); a
    // call queued while its caller runs (an asynchronous select's abortable part) keeps its place)
    void requeue_by_priority(task& aTask)
    {
        if (!gPriorityQueuing || !aTask.blocked)
            return;
        auto const reorder = [&aTask](std::deque<call_record*>& aQueue)
        {
            auto i = std::find_if(aQueue.begin(), aQueue.end(), [&aTask](call_record const* c) { return c->caller == &aTask && !c->accepted; });
            if (i == aQueue.end())
                return;
            auto* c = *i;
            aQueue.erase(i);
            c->priority = active_priority(aTask, 0, false);
            auto const priority = call_priority(*c);
            aQueue.insert(std::find_if(aQueue.begin(), aQueue.end(), [priority](call_record const* d) { return call_priority(*d) < priority; }), c);
        };
        for (auto& [id, t] : gTasks)
            for (auto& [entry, queue] : t->entries)
                reorder(queue);
        for (auto& [entry, queue] : gEnvironment.entries)
            reorder(queue);
        for (auto& [id, po] : gProtected)
            for (auto& [entry, queue] : po->queues)
                reorder(queue);
    }

    // --- rendezvous

    void complete_call(call_record& aCall, task* aTarget)
    {
        // (a call that failed because the task completed: Tasking_Error in the caller)
        (void)aTarget;
        if (aCall.failed)
        {
            if (aCall.exception.id != 0)
                raise_occurrence(aCall.exception);
            else
                tasking_error("the called task has completed");
        }
        // (the end of an entry call: an abort completion point (9.8(15)), the caller's abort (deferred while the call was being
        // accepted, or requeued without abort) takes effect; likewise an asynchronous select's abortable part's)
        auto* self = current();
        if (self->deferAbort != 0)
            return;
        if (self->aborted && self != &gEnvironment)
            set_pending(AbortSignal);
        else if (!self->atc.empty() && atc_triggered(*self) != 0)
            set_pending(AtcSignalBase + atc_triggered(*self));
    }

    // (an abort-deferred operation (a controlled object's Initialize, Adjust or Finalize, 9.8(6-11)): begun, ended (then its task's
    // abort, or its asynchronous select's abortable part's, takes effect, if no exception is propagating))
    NEOS_ADA_PRIMITIVE void neos_ada_abort_defer()
    {
        abort_defer();
    }

    NEOS_ADA_PRIMITIVE void neos_ada_abort_undefer(std::int64_t aSignal)
    {
        abort_undefer(aSignal != 0);
    }

    // (an abort-deferred operation's end, after which its effect is complete (an object initialized, then registered for its
    // finalization): the task's pending abort (or its asynchronous select's) signalled)
    NEOS_ADA_PRIMITIVE void neos_ada_abort_check()
    {
        ++current()->deferAbort;
        abort_undefer(true);
    }

    // (an entry call (timed or conditional: aKind (see clock_until), 4: conditional (else)): 1 if it was made, 0 if not (the
    // call was not accepted in time))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_entry_call(std::int64_t aTask, std::int64_t aEntry, std::int64_t aParams, std::int64_t aKind, std::int64_t aValue)
    {
        auto* t = task_of(aTask);
        if (t == nullptr || !callable(*t))
        {
            tasking_error("the called task is not callable");
            return 0;
        }
        call_record c;
        c.caller = current();
        c.entry = aEntry;
        c.params = aParams;
        // (a timed call whose delay has already expired: as a conditional call (9.7.2(5)))
        if (aKind != 4 && aKind != 0)
            if (auto const expiry = clock_until(aKind, aValue); expiry >= 0 && monotonic_now() >= expiry)
                aKind = 4;
        if (aKind == 4)
        {
            // (conditional: only if the task is waiting to accept it now (at an accept, or a selective accept with it open))
            bool const accepting = t->blocked && std::find(t->awaiting.begin(), t->awaiting.end(), aEntry) != t->awaiting.end();
            if (!accepting || !enqueue_check(t->entries[aEntry]))
                return 0;
            enqueue(t->entries[aEntry], &c);
            // (accepted (it is being accepted); then, if requeued with abort and not done at once: cancelled (its expiry is now))
            block_until([&c]() { return c.done || c.accepted || c.requeued; });
            while (!c.done)
            {
                if (c.accepted || !c.abortable)
                {
                    if (!block_until([&c]() { return c.done || (!c.accepted && c.abortable); }, -1, [&c]() { return !c.accepted && c.abortable; }) &&
                        !c.done && (c.accepted || !c.abortable))
                        break;
                    continue;
                }
                if (c.queue != nullptr)
                    if (auto i = std::find(c.queue->begin(), c.queue->end(), &c); i != c.queue->end())
                    {
                        c.queue->erase(i);
                        return 0;
                    }
                block_until([&c]() { return c.done; });
            }
            complete_call(c, t);
            return 1;
        }
        if (!enqueue_check(t->entries[aEntry]))
            return 0;
        enqueue(t->entries[aEntry], &c);
        auto const wake = clock_until(aKind, aValue);
        // (a call being accepted (a rendezvous in progress), or requeued without abort: not cancellable (abort deferred, 9.8(6-11)))
        auto const cancellable = [&c]() { return !c.accepted && c.abortable; };
        auto const withdraw = [&c]()
        {
            if (c.queue != nullptr)
                if (auto i = std::find(c.queue->begin(), c.queue->end(), &c); i != c.queue->end())
                {
                    c.queue->erase(i);
                    return true;
                }
            return false;
        };
        while (!c.done)
        {
            if (!cancellable())
            {
                // (until it completes, or is requeued with abort (cancellable again, its original expiry applies, 9.5.4(13)))
                if (!block_until([&c, &cancellable]() { return c.done || cancellable(); }, -1, cancellable) && !c.done && !cancellable())
                    break;
                continue;
            }
            bool const ok = block_until([&c, &cancellable]() { return c.done || !cancellable(); }, wake, cancellable);
            if (c.done || !ok)
                break;
            if (!cancellable())
                continue;
            if (wake >= 0 && monotonic_now() >= wake)
            {
                // (timed out: withdrawn)
                if (withdraw())
                    return 0;
            }
        }
        if (!c.done)
        {
            // (the caller is aborted, or a deadlock)
            withdraw();
            if (!c.accepted && c.abortable)
            {
                if (state().excGlobal == nullptr || *state().excGlobal == 0u)
                    raise_predefined(1, "deadlock: the entry call cannot be accepted");
                return 0;
            }
            block_until([&c]() { return c.done; }, -1, cancellable);
        }
        complete_call(c, t);
        return 1;
    }

    // (an accept statement: waits for a call of the entry; the call (its handle))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_accept(std::int64_t aEntry)
    {
        auto* self = current();
        auto& queue = self->entries[aEntry];
        self->awaiting = { aEntry };
        bool const called = block_until([&queue]() { return !queue.empty(); });
        self->awaiting.clear();
        if (!called)
        {
            if (state().excGlobal == nullptr || *state().excGlobal == 0u)
                raise_predefined(1, "deadlock: no call of the entry can be made");
            return 0;
        }
        auto* c = queue.front();
        queue.pop_front();
        c->accepted = true;
        self->accepting.push_back(c);
        return reinterpret_cast<std::int64_t>(c);
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_call_params(std::int64_t aCall)
    {
        return aCall != 0 ? reinterpret_cast<call_record*>(aCall)->params : 0;
    }

    // (an accept statement's end (normal: aFailed 0; else the exception that it propagates is propagated by the caller too))
    NEOS_ADA_PRIMITIVE void neos_ada_accept_done(std::int64_t aCall, std::int64_t aFailed)
    {
        if (aCall == 0)
            return;
        auto* self = current();
        auto* c = reinterpret_cast<call_record*>(aCall);
        if (aFailed != 0)
        {
            auto& s = state();
            c->failed = true;
            c->exception = s.current;
            if (s.excGlobal != nullptr && *s.excGlobal != 0u)
                c->exception.id = static_cast<std::int64_t>(*s.excGlobal);
            // (the acceptor aborted (or its asynchronous select's abortable part abandoned): the caller's Tasking_Error (9.8(19)))
            if (static_cast<std::uint64_t>(c->exception.id) >= static_cast<std::uint64_t>(AtcSignalBase))
                c->exception.id = 0;
        }
        if (c->requeued && aFailed == 0)
            c->requeued = false;    // (requeued: not done yet)
        else
            c->done = true;
        auto i = std::find(self->accepting.begin(), self->accepting.end(), c);
        if (i != self->accepting.end())
            self->accepting.erase(i);
        // (the rendezvous's end: the caller ready (a dispatching point))
        if (aFailed == 0)
            dispatch();
    }

    // (a selective accept: its open accept alternatives' entries (aCount i64s at aEntries (-1: a closed one)), its delay (as 
    // clock_until's), whether it has an open terminate alternative or an else part: the index of the alternative whose call is
    // accepted (see accepted_call), -1: the delay expired; -2: the else part; -3: terminate (the task completes); -4: no open
    // alternative and no else part (Program_Error))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_select(std::int64_t aEntries, std::int64_t aCount, std::int64_t aDelayKind, std::int64_t aDelay,
        std::int64_t aTerminate, std::int64_t aElse)
    {
        auto* self = current();
        std::vector<std::int64_t> open;
        for (std::int64_t i = 0; i < aCount; ++i)
            open.push_back(*at<std::int64_t>(aEntries + 8 * i));
        auto const ready = [&]() -> std::int64_t
        {
            // (under Priority_Queuing: the alternative whose first call's caller has the highest priority, the first such (D.4(12)))
            std::int64_t result = -1;
            std::int64_t best = 0;
            for (std::size_t i = 0u; i < open.size(); ++i)
                if (open[i] >= 0)
                    if (auto q = self->entries.find(open[i]); q != self->entries.end() && !q->second.empty())
                    {
                        if (!gPriorityQueuing)
                            return static_cast<std::int64_t>(i);
                        auto const p = call_priority(*q->second.front());
                        if (result < 0 || p > best)
                        {
                            result = static_cast<std::int64_t>(i);
                            best = p;
                        }
                    }
            return result;
        };
        auto const take = [&](std::int64_t aIndex)
        {
            auto& queue = self->entries[open[static_cast<std::size_t>(aIndex)]];
            auto* c = queue.front();
            queue.pop_front();
            c->accepted = true;
            self->accepting.push_back(c);
            return aIndex;
        };
        if (auto const i = ready(); i >= 0)
            return take(i);
        if (aElse != 0)
            return -2;
        bool const anyOpen = std::any_of(open.begin(), open.end(), [](std::int64_t e) { return e >= 0; });
        if (!anyOpen && aDelayKind == 0 && aTerminate == 0)
        {
            raise_predefined(1, "selective accept with no open alternative");
            return -4;
        }
        auto const wake = clock_until(aDelayKind, aDelay);
        self->atTerminate = aTerminate != 0;
        for (auto e : open)
            if (e >= 0)
                self->awaiting.push_back(e);
        block_until([&]() { return ready() >= 0 || self->terminateRequested; }, wake);
        self->awaiting.clear();
        self->atTerminate = false;
        if (state().excGlobal != nullptr && *state().excGlobal != 0u)
            return -1;
        if (auto const i = ready(); i >= 0)
            return take(i);
        if (self->terminateRequested)
            return -3;
        if (wake >= 0)
            return -1;
        raise_predefined(1, "deadlock: no call of an open entry can be made");
        return -1;
    }

    // (the call that an accept or a selective accept accepted most recently)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_accepted_call()
    {
        auto* self = current();
        return self->accepting.empty() ? 0 : reinterpret_cast<std::int64_t>(self->accepting.back());
    }

    // (E'Caller (C.7.1(14/3)): within an accept statement for the entry (its key: the high half), the caller of the call that the
    // innermost one accepted; within a protected entry body (aEntry -1), the caller of the call it is for)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_entry_caller(std::int64_t aEntry)
    {
        if (aEntry < 0)
            return gProtectedCall != nullptr && gProtectedCall->caller != nullptr ? gProtectedCall->caller->id : 0;
        auto const* self = current();
        for (auto i = self->accepting.rbegin(); i != self->accepting.rend(); ++i)
            if (((*i)->entry >> 32) == (aEntry >> 32))
                return (*i)->caller != nullptr ? (*i)->caller->id : 0;
        return 0;
    }

    // (E'Count: the calls queued on an entry of the running task)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_entry_count(std::int64_t aEntry)
    {
        preempt_poll();
        auto* self = current();
        auto q = self->entries.find(aEntry);
        return q != self->entries.end() ? static_cast<std::int64_t>(q->second.size()) : 0;
    }

    // --- protected objects

    std::int64_t constexpr ProtectedHandle = 8;     // (a protected object's id: after its tag (a type with interfaces'))

    protected_object* protected_of(std::int64_t aSelf)
    {
        auto p = gProtected.find(*at<std::int64_t>(aSelf + ProtectedHandle));
        if (p == gProtected.end())
            return nullptr;
        // (an object moved since it was created (a function's result, built in place): where it is now)
        p->second->self = aSelf;
        return p->second.get();
    }

    // (a protected object: its entries' barriers and bodies (aTable: aCount pairs of function indexes), their static link)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_po_create(std::int64_t aSelf, std::int64_t aTable, std::int64_t aCount, std::int64_t aLink, std::int64_t aMastered)
    {
        auto p = std::make_unique<protected_object>();
        auto const id = gNextProtected++;
        p->self = aSelf;
        p->table = aTable;
        p->count = aCount;
        p->link = aLink;
        gProtected[id] = std::move(p);
        *at<std::int64_t>(aSelf + ProtectedHandle) = id;
        // (finalized with its master (not an allocated one's): see protected_finalize)
        if (aMastered != 0)
            state().finalization.push_back({ id, ProtectedEntry, 0 });
        return id;
    }

    struct barrier_failure {};

    bool barrier_open(protected_object& aObject, std::int64_t aEntry, std::int64_t aFamily)
    {
        auto const function = *at<std::int64_t>(aObject.table + 16 * aEntry);
        neos_rt_type const parameters[3] = { NEOS_RT_I64, NEOS_RT_I64, NEOS_RT_I64 };
        neos_rt_value arguments[3];
        arguments[0].i64 = aObject.link;
        arguments[1].i64 = aObject.self;
        arguments[2].i64 = aFamily;
        neos_rt_value result;
        result.i64 = 0;
        host()->call(static_cast<unsigned>(function), 3u, parameters, arguments, NEOS_RT_I64, &result);
        // (an exception raised by a barrier's evaluation: Program_Error for every call queued on the object's entries (9.5.3(7/3)))
        if (take_pending().id != 0)
        {
            for (auto& [entry, queue] : aObject.queues)
            {
                for (auto* c : queue)
                {
                    c->failed = true;
                    c->done = true;
                    c->exception = occurrence{};
                    c->exception.id = predefined_id(1);
                    c->exception.message = "an entry barrier's evaluation raised an exception";
                }
                queue.clear();
            }
            throw barrier_failure{};
        }
        return result.i64 != 0;
    }

    void run_body(protected_object& aObject, call_record& aCall)
    {
        // (an entry body, for a call (its caller's: an exception that it propagates is the caller's))
        auto const function = *at<std::int64_t>(aObject.table + 16 * aCall.entry + 8);
        neos_rt_type const parameters[4] = { NEOS_RT_I64, NEOS_RT_I64, NEOS_RT_I64, NEOS_RT_I64 };
        neos_rt_value arguments[4];
        arguments[0].i64 = aObject.link;
        arguments[1].i64 = aObject.self;
        arguments[2].i64 = aCall.params;
        arguments[3].i64 = aCall.family;
        neos_rt_value result;
        auto* const saved = gProtectedCall;
        gProtectedCall = &aCall;
        ++gEntryBodies;
        auto* self = current();
        self->ceilings.push_back(aObject.ceiling);
        host()->call(static_cast<unsigned>(function), 4u, parameters, arguments, NEOS_RT_VOID, &result);
        self->ceilings.pop_back();
        gProtectedCall = saved;
        auto const failure = take_pending();
        if (aCall.requeued && failure.id == 0)
        {
            // (requeued: not done yet)
            aCall.requeued = false;
            return;
        }
        if (failure.id != 0)
        {
            aCall.failed = true;
            aCall.exception = failure;
        }
        aCall.done = true;
    }

    // (the protected objects that calls were requeued on by entry bodies (not yet served: after the requeuing protected action))
    std::vector<protected_object*> gRequeueTargets;

    // (a queued call that can be cancelled (not requeued without abort) whose caller is aborted, or whose caller's asynchronous select's
    // triggering statement has completed: it is not served (its caller withdraws it, 9.8(15), 9.7.4(7)))
    bool cancelling(call_record const& aCall)
    {
        auto const* caller = aCall.caller;
        if (!aCall.abortable || caller == nullptr || caller->deferAbort != 0)
            return false;
        if (caller->aborted && caller != &gEnvironment)
            return true;
        for (auto const& f : caller->atc)
            if (f->call.get() == &aCall)
                return false;
        return !caller->atc.empty() && atc_triggered(*caller) != 0;
    }

    void service(protected_object& aObject)
    {
        // (after a protected action: the queued calls whose barriers are open, served (each one opens others perhaps))
        auto const pending = take_pending();
        try
        {
            for (bool progress = true; progress;)
            {
                progress = false;
                // (an entry family's members' calls are in one queue: the first call of a member whose barrier is open; under
                // Priority_Queuing, of the entries' such calls, the one whose caller has the highest priority (D.4(12)))
                std::deque<call_record*>* chosenQueue = nullptr;
                std::deque<call_record*>::iterator chosen;
                std::int64_t best = 0;
                for (auto& [entry, queue] : aObject.queues)
                {
                    for (auto i = queue.begin(); i != queue.end(); ++i)
                        if (!cancelling(**i) && barrier_open(aObject, entry, (*i)->family))
                        {
                            if (chosenQueue == nullptr || call_priority(**i) > best)
                            {
                                chosenQueue = &queue;
                                chosen = i;
                                best = call_priority(**i);
                            }
                            break;
                        }
                    if (chosenQueue != nullptr && !gPriorityQueuing)
                        break;
                }
                if (chosenQueue != nullptr)
                {
                    auto* c = *chosen;
                    chosenQueue->erase(chosen);
                    run_body(aObject, *c);
                    progress = true;
                }
            }
        }
        catch (barrier_failure const&)
        {
            // (the calls failed (their queues are empty))
        }
        // (the objects that its entry bodies requeued calls on: served now (their protected actions follow this one))
        while (!gRequeueTargets.empty())
        {
            auto* target = gRequeueTargets.back();
            gRequeueTargets.pop_back();
            if (target != &aObject)
                service(*target);
        }
        if (pending.id != 0)
            raise_occurrence(pending);
    }

    // (a protected action's end (an external call of a protected procedure, or function: see po_enter): a dispatching point if it is
    // not within another (the tasks whose calls it served may preempt the caller))
    // (a protected action's begin: the caller's priority within the object's ceiling under Ceiling_Locking (D.3(13)), and the object
    // not in use by another task (one that it preempted: its priority is above the ceiling); the caller inherits the ceiling)
    bool protected_action_begin(protected_object& aObject)
    {
        auto* self = current();
        if ((gCeilingLocking && active_priority(*self) > aObject.ceiling) || (aObject.user != nullptr && aObject.user != self))
        {
            raise_predefined(1, "the caller's priority is above the protected object's ceiling");
            return false;
        }
        // (an object assigned to a processor: the caller assigned to it (D.16(15/5)))
        if (aObject.cpu != 0 && self->cpu != aObject.cpu)
        {
            raise_predefined(1, "the protected object is assigned to a processor that the caller is not assigned to");
            return false;
        }
        // (a ceiling under EDF_Within_Priorities: the caller's active deadline less its last release time not less than the object's
        // relative deadline; within the action the caller's active deadline is at most now plus that (D.3(13.4/5, 13.5/5)))
        std::int64_t floor = TimeLast;
        if (policy_of(aObject.ceiling) == EdfWithinPriorities)
        {
            if (active_deadline(*self) < saturating_add(self->releaseTime, aObject.relativeDeadline))
            {
                raise_predefined(1, "the caller's deadline is within the protected object's relative deadline");
                return false;
            }
            floor = saturating_add(monotonic_now(), aObject.relativeDeadline);
        }
        aObject.user = self;
        ++aObject.uses;
        self->ceilings.push_back(aObject.ceiling);
        self->deadlineFloors.push_back(floor);
        return true;
    }

    // (a protected action's end (an external call of a protected procedure (after its object's entry calls are served) or function):
    // a dispatching point if it served calls (their callers are ready), or a protected action within it did (the tasks above the
    // ceilings of the protected actions that it is still in preempt it))
    void protected_action_end(protected_object* aObject, std::uint64_t aEntryBodies)
    {
        auto* self = current();
        if (aObject != nullptr && aObject->uses > 0 && --aObject->uses == 0)
        {
            aObject->user = nullptr;
            // (its Priority attribute assigned: its ceiling (D.5.2(5/3)))
            if (aObject->priority >= 0)
            {
                aObject->ceiling = aObject->priority;
                aObject->priority = -1;
            }
        }
        if (!self->ceilings.empty())
            self->ceilings.pop_back();
        if (!self->deadlineFloors.empty())
            self->deadlineFloors.pop_back();
        if (gEntryBodies != aEntryBodies)
            self->dispatchPending = true;
        if (self->dispatchPending)
        {
            if (self->ceilings.empty() && gProtectedCall == nullptr)
                self->dispatchPending = false;
            // (an exception that is propagating: kept (the preempting tasks' own))
            auto const pending = take_pending();
            dispatch();
            if (pending.id != 0 && (state().excGlobal == nullptr || *state().excGlobal == 0u))
                raise_occurrence(pending);
        }
        else
            preempt_poll();
    }

    NEOS_ADA_PRIMITIVE void neos_ada_po_service(std::int64_t aSelf)
    {
        auto const entryBodies = gEntryBodies;
        auto* p = protected_of(aSelf);
        if (p != nullptr)
            service(*p);
        protected_action_end(p, entryBodies);
    }

    // (an external call of a protected subprogram: its protected action begins)
    NEOS_ADA_PRIMITIVE void neos_ada_po_enter(std::int64_t aSelf)
    {
        if (auto* p = protected_of(aSelf))
            protected_action_begin(*p);
        else
        {
            current()->ceilings.push_back(PriorityLast);
            current()->deadlineFloors.push_back(TimeLast);
        }
    }

    // (an external call of a protected function: its protected action ends)
    NEOS_ADA_PRIMITIVE void neos_ada_po_leave(std::int64_t aSelf)
    {
        auto* p = protected_of(aSelf);
        if (p != nullptr && p->uses > 0 && --p->uses == 0)
            p->user = nullptr;
        auto* self = current();
        if (!self->ceilings.empty())
            self->ceilings.pop_back();
        if (!self->deadlineFloors.empty())
            self->deadlineFloors.pop_back();
        preempt_poll();
    }

    // (a protected object's ceiling priority (D.3(6)))
    NEOS_ADA_PRIMITIVE void neos_ada_po_ceiling(std::int64_t aId, std::int64_t aPriority)
    {
        if (aPriority < 0 || aPriority > 31)
        {
            raise_predefined(1, "ceiling priority out of range");
            return;
        }
        if (auto p = gProtected.find(aId); p != gProtected.end())
            p->second->ceiling = aPriority;
    }

    // (a protected entry call (timed or conditional as entry_call's): 1 if it was made)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_po_call(std::int64_t aSelf, std::int64_t aEntry, std::int64_t aFamily, std::int64_t aParams,
        std::int64_t aKind, std::int64_t aValue)
    {
        auto* p = protected_of(aSelf);
        if (p == nullptr)
        {
            raise_predefined(1, "protected object not initialized");
            return 0;
        }
        call_record c;
        c.caller = current();
        c.entry = aEntry;
        c.family = aFamily;
        c.params = aParams;
        // (its protected action: queued first (its entry's 'Count includes it), then the open barriers' calls served (this one perhaps))
        auto const entryBodies = gEntryBodies;
        if (!enqueue_check(p->queues[aEntry]) || !protected_action_begin(*p))
            return 0;
        enqueue(p->queues[aEntry], &c);
        service(*p);
        protected_action_end(p, entryBodies);
        if (c.done)
        {
            complete_call(c, nullptr);
            return 1;
        }
        // (withdrawn from the queue it is on (unless requeued without abort: it can't be, 9.5.4(13)))
        auto const withdraw = [&c]()
        {
            if (c.queue != nullptr)
                if (auto i = std::find(c.queue->begin(), c.queue->end(), &c); i != c.queue->end())
                    c.queue->erase(i);
        };
        if (aKind == 4 && c.abortable)
        {
            withdraw();
            return 0;
        }
        auto const wake = aKind == 4 ? -1 : clock_until(aKind, aValue);
        auto const abortable = [&c]() { return c.abortable; };
        bool ok = true;
        while (!c.done)
        {
            if (!c.abortable)
            {
                // (requeued without abort: until it completes, or is requeued with abort (its original expiry applies))
                if (!block_until([&c]() { return c.done || c.abortable; }, -1, abortable) && !c.done && !c.abortable)
                {
                    ok = false;
                    break;
                }
                continue;
            }
            ok = block_until([&c]() { return c.done || !c.abortable; }, wake, abortable);
            if (c.done || !ok || c.abortable)
                break;
        }
        if (!c.done)
        {
            withdraw();
            if (ok && wake >= 0)
                return 0;
            if (state().excGlobal == nullptr || *state().excGlobal == 0u)
                raise_predefined(1, "deadlock: the protected entry's barrier cannot open");
            return 0;
        }
        complete_call(c, nullptr);
        return 1;
    }

    // --- asynchronous select (9.7.4): its triggering statement is started, its abortable part runs (aborted (by the exception
    // AtcSignalBase + its depth) at a point where it waits, if the triggering statement has completed by then), then the
    // triggering statement is cancelled if it hasn't completed

    bool atc_frame::triggered() const
    {
        if (call != nullptr)
            return call->done;
        return deadline >= 0 && monotonic_now() >= deadline;
    }

    std::int64_t atc_push(std::unique_ptr<atc_frame> aFrame)
    {
        if (gMaxAtcNesting >= 0 && static_cast<std::int64_t>(current()->atc.size()) >= gMaxAtcNesting)
        {
            raise_predefined(2, "the partition's restriction Max_Asynchronous_Select_Nesting is exceeded");
            return 0;
        }
        auto* self = current();
        self->atc.push_back(std::move(aFrame));
        return static_cast<std::int64_t>(self->atc.size());
    }

    // (a delay as the triggering statement: its depth)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_atc_delay(std::int64_t aKind, std::int64_t aValue)
    {
        auto f = std::make_unique<atc_frame>();
        f->deadline = clock_until(aKind, aValue);
        if (f->deadline < 0)
            f->deadline = monotonic_now();
        return atc_push(std::move(f));
    }

    // (a task entry call as the triggering statement (queued): its depth)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_atc_entry_call(std::int64_t aTask, std::int64_t aEntry, std::int64_t aParams)
    {
        auto* t = task_of(aTask);
        if (t == nullptr || !callable(*t))
        {
            tasking_error("the called task is not callable");
            return 0;
        }
        if (!enqueue_check(t->entries[aEntry]))
            return 0;
        auto f = std::make_unique<atc_frame>();
        f->call = std::make_unique<call_record>();
        f->call->caller = current();
        f->call->entry = aEntry;
        f->call->params = aParams;
        f->target = t;
        f->entry = aEntry;
        auto* c = f->call.get();
        enqueue(t->entries[aEntry], c);
        // (a task waiting to accept it: its rendezvous now (it completes before the abortable part starts))
        if (t->blocked && t->ready && t->ready())
            block_until([c]() { return c->done || c->accepted; });
        if (c->accepted && !c->done)
            block_until([c]() { return c->done; });
        return atc_push(std::move(f));
    }

    // (a protected entry call as the triggering statement (queued, and served if its barrier is open): its depth)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_atc_po_call(std::int64_t aSelf, std::int64_t aEntry, std::int64_t aFamily, std::int64_t aParams)
    {
        auto* p = protected_of(aSelf);
        if (p == nullptr)
        {
            raise_predefined(1, "protected object not initialized");
            return 0;
        }
        if (!enqueue_check(p->queues[aEntry]))
            return 0;
        auto f = std::make_unique<atc_frame>();
        f->call = std::make_unique<call_record>();
        f->call->caller = current();
        f->call->entry = aEntry;
        f->call->family = aFamily;
        f->call->params = aParams;
        f->po = p;
        f->entry = aEntry;
        enqueue(p->queues[aEntry], f->call.get());
        auto* call = f->call.get();
        auto const depth = atc_push(std::move(f));
        service(*p);
        (void)call;
        return depth;
    }

    // (whether an asynchronous select's triggering statement has completed (its abortable part is not run if it has))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_atc_triggered(std::int64_t aDepth)
    {
        auto* self = current();
        if (aDepth <= 0 || static_cast<std::size_t>(aDepth) > self->atc.size())
            return 0;
        auto& frame = *self->atc[static_cast<std::size_t>(aDepth - 1)];
        frame.skipped = frame.triggered();
        return frame.skipped ? 1 : 0;
    }

    // (an asynchronous select's abortable part ended (normally, or by an exception: its own abort signal is handled here): its
    // triggering statement cancelled (a call being accepted: awaited), 1 if it completed)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_atc_end(std::int64_t aDepth)
    {
        auto* self = current();
        if (aDepth <= 0 || static_cast<std::size_t>(aDepth) > self->atc.size())
            return 0;
        // (its frame and any inner ones (abandoned by an exception))
        std::unique_ptr<atc_frame> frame = std::move(self->atc[static_cast<std::size_t>(aDepth - 1)]);
        for (auto i = self->atc.size(); i-- > static_cast<std::size_t>(aDepth);)
        {
            auto& inner = self->atc[i];
            if (inner->call != nullptr && !inner->call->done && !inner->call->accepted)
            {
                auto& queue = inner->po != nullptr ? inner->po->queues[inner->entry] : inner->target->entries[inner->entry];
                auto q = std::find(queue.begin(), queue.end(), inner->call.get());
                if (q != queue.end())
                    queue.erase(q);
            }
        }
        self->atc.resize(static_cast<std::size_t>(aDepth - 1));
        auto& s = state();
        bool const ours = s.excGlobal != nullptr && *s.excGlobal == static_cast<unsigned long long>(AtcSignalBase + aDepth);
        if (ours)
            *s.excGlobal = 0u;
        bool triggered = ours;
        if (frame->call != nullptr)
        {
            auto& c = *frame->call;
            if (!c.done && !c.accepted)
            {
                // (not accepted: withdrawn)
                auto& queue = frame->po != nullptr ? frame->po->queues[frame->entry] : frame->target->entries[frame->entry];
                auto q = std::find(queue.begin(), queue.end(), &c);
                if (q != queue.end())
                    queue.erase(q);
            }
            else if (!c.done)
            {
                // (being accepted: its rendezvous awaited (an exception pending meanwhile kept))
                auto const pending = take_pending();
                while (!c.done)
                    if (!block_until([&c]() { return c.done; }))
                        (void)take_pending();
                if (pending.id != 0)
                    raise_occurrence(pending);
            }
            if (c.done)
            {
                triggered = true;
                if (c.failed && (s.excGlobal == nullptr || *s.excGlobal == 0u))
                    complete_call(c, frame->target);
            }
        }
        // (a delay: cancelled if the abortable part completed (it was not aborted))
        if (frame->skipped)
            triggered = true;
        return triggered ? 1 : 0;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_po_count(std::int64_t aSelf, std::int64_t aEntry)
    {
        auto* p = protected_of(aSelf);
        if (p == nullptr)
            return 0;
        auto q = p->queues.find(aEntry);
        return q != p->queues.end() ? static_cast<std::int64_t>(q->second.size()) : 0;
    }

    // (the number of calls queued on a member of a protected object's entry family (its index's offset))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_po_count_family(std::int64_t aSelf, std::int64_t aEntry, std::int64_t aFamily)
    {
        auto* p = protected_of(aSelf);
        if (p == nullptr)
            return 0;
        auto q = p->queues.find(aEntry);
        if (q == p->queues.end())
            return 0;
        std::int64_t count = 0;
        for (auto const* c : q->second)
            if (c->family == aFamily)
                ++count;
        return count;
    }

    // (a requeue (of the call that the innermost accept statement accepted (aSource 0), or of the call whose entry body is
    // running (1)) on an entry of a task (aKind 0: aTarget its id) or of a protected object (1: aTarget its address))
    NEOS_ADA_PRIMITIVE void neos_ada_requeue(std::int64_t aSource, std::int64_t aKind, std::int64_t aTarget, std::int64_t aEntry, std::int64_t aFamily)
    {
        auto* self = current();
        call_record* c = aSource == 0 ? (self->accepting.empty() ? nullptr : self->accepting.back()) : gProtectedCall;
        if (c == nullptr)
            return;
        // (Max_Entry_Queue_Length: Program_Error at the requeue (D.7(19.1/2)))
        {
            std::deque<call_record*>* target = nullptr;
            if ((aKind & 1) == 0)
            {
                if (auto* t = task_of(aTarget); t != nullptr && callable(*t))
                    target = &t->entries[aEntry];
            }
            else if (auto* p = protected_of(aTarget))
                target = &p->queues[aEntry];
            if (target != nullptr && !enqueue_check(*target))
                return;
        }
        c->requeued = true;
        c->accepted = false;
        c->entry = aEntry;
        c->family = aFamily;
        // (requeued without abort: no longer cancellable (9.5.4(13)); with abort: as an entry call is)
        c->abortable = (aKind & 2) != 0;
        aKind &= 1;
        if (aKind == 0)
        {
            auto* t = task_of(aTarget);
            if (t == nullptr || !callable(*t))
            {
                c->requeued = false;
                c->failed = true;
                c->done = true;
                c->exception = occurrence{};
                return;
            }
            enqueue(t->entries[aEntry], c);
            return;
        }
        auto* p = protected_of(aTarget);
        if (p == nullptr)
            return;
        enqueue(p->queues[aEntry], c);
        if (aSource == 0)
            service(*p);
        else
            gRequeueTargets.push_back(p);
    }

    // --- interrupts (C.3): the process's signals (std::signal); an interrupt that occurs is pending until the running task polls
    // (outside protected actions: a delay, a wait, a protected action's end, E'Count), when its handler (a protected procedure: an
    // access value's descriptor (procedure, static link, object)) is called

    // (an interrupt's signal: the ones that can have handlers (the others are reserved))
    int interrupt_signal(std::int64_t aInterrupt)
    {
        switch (aInterrupt)
        {
#if !defined(_WIN32)
        case 1: return SIGHUP;
        case 3: return SIGQUIT;
        case 10: return SIGUSR1;
        case 12: return SIGUSR2;
        case 13: return SIGPIPE;
        case 14: return SIGALRM;
#endif
        case 2: return SIGINT;
        case 15: return SIGTERM;
        default: return 0;
        }
    }

    struct interrupt
    {
        std::int64_t handler = 0;           ///< (its handler (0: none, its default treatment))
        bool attachedStatically = false;    ///< (by an Attach_Handler aspect: not detached by Ada.Interrupts (C.3.2))
        std::int64_t previous = 0;          ///< (when it was attached statically: the handler it replaced (restored when its object is finalized))
    };
    std::array<interrupt, 32> gInterrupts;
    std::set<std::int64_t> gInterruptHandlerFunctions;     ///< (the protected procedures that can be interrupt handlers (C.3.1))

    void interrupt_occurred(int aSignal)
    {
        for (std::int64_t i = 0; i < static_cast<std::int64_t>(gInterrupts.size()); ++i)
            if (interrupt_signal(i) == aSignal)
                gInterruptsPending.fetch_or(std::uint32_t{ 1u } << i);
        std::signal(aSignal, &interrupt_occurred);
    }

    void set_interrupt_handler(std::int64_t aInterrupt, std::int64_t aHandler)
    {
        auto& i = gInterrupts[static_cast<std::size_t>(aInterrupt)];
        if ((i.handler != 0) != (aHandler != 0))
        {
            gInterruptsAttached += aHandler != 0 ? 1 : -1;
            std::signal(interrupt_signal(aInterrupt), aHandler != 0 ? &interrupt_occurred : SIG_DFL);
        }
        i.handler = aHandler;
    }

    // (a protected procedure (a parameterless one: an access value's descriptor) called (its protected action); an exception that it
    // propagates is lost)
    void call_protected_procedure(std::int64_t aHandler)
    {
        auto const function = *at<std::int64_t>(aHandler);
        auto const link = *at<std::int64_t>(aHandler + 8);
        auto const object = *at<std::int64_t>(aHandler + 16);
        auto* p = object != 0 ? protected_of(object) : nullptr;
        auto const pending = take_pending();
        auto const entryBodies = gEntryBodies;
        if (p == nullptr || protected_action_begin(*p))
        {
            neos_rt_type const parameters[2] = { NEOS_RT_I64, NEOS_RT_I64 };
            neos_rt_value arguments[2];
            arguments[0].i64 = link;
            arguments[1].i64 = object;
            neos_rt_value result;
            host()->call(static_cast<unsigned>(function), 2u, parameters, arguments, NEOS_RT_VOID, &result);
            take_pending();
            if (p != nullptr)
            {
                service(*p);
                protected_action_end(p, entryBodies);
            }
        }
        take_pending();
        if (pending.id != 0)
            raise_occurrence(pending);
    }

    void deliver_interrupts()
    {
        if (gInterruptsPending.load() == 0u)
            return;
        auto* self = current();
        if (!self->ceilings.empty() || gProtectedCall != nullptr)
            return;
        auto const pending = gInterruptsPending.exchange(0u);
        for (std::size_t i = 0u; i < gInterrupts.size(); ++i)
            if ((pending & (std::uint32_t{ 1u } << i)) != 0u && gInterrupts[i].handler != 0)
                call_protected_procedure(gInterrupts[i].handler);
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_interrupt_reserved(std::int64_t aInterrupt)
    {
        return aInterrupt < 0 || aInterrupt >= static_cast<std::int64_t>(gInterrupts.size()) || interrupt_signal(aInterrupt) == 0 ? 1 : 0;
    }

    NEOS_ADA_PRIMITIVE void neos_ada_interrupt_handler_register(std::int64_t aFunction)
    {
        gInterruptHandlerFunctions.insert(aFunction);
    }

    // (Ada.Interrupts.Attach_Handler, Exchange_Handler, Detach_Handler (a null handler): the handler that it replaces; Program_Error if
    // the interrupt is reserved, its handler was attached statically, or the new handler is not an interrupt handler (C.3.2(15-17)))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_interrupt_attach(std::int64_t aInterrupt, std::int64_t aHandler)
    {
        if (neos_ada_interrupt_reserved(aInterrupt) != 0)
        {
            raise_predefined(1, "the interrupt is reserved");
            return 0;
        }
        auto& i = gInterrupts[static_cast<std::size_t>(aInterrupt)];
        if (i.attachedStatically || (aHandler != 0 && !gInterruptHandlerFunctions.contains(*at<std::int64_t>(aHandler))))
        {
            raise_predefined(1, i.attachedStatically ? "the interrupt's handler was attached statically" : "not an interrupt handler");
            return 0;
        }
        auto const previous = i.handler;
        set_interrupt_handler(aInterrupt, aHandler);
        return previous;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_interrupt_handler(std::int64_t aInterrupt)
    {
        if (neos_ada_interrupt_reserved(aInterrupt) != 0)
        {
            raise_predefined(1, "the interrupt is reserved");
            return 0;
        }
        return gInterrupts[static_cast<std::size_t>(aInterrupt)].handler;
    }

    // (a protected object's procedure attached by an Attach_Handler aspect, as the object is initialized (C.3.1(7/3)): Program_Error if
    // the interrupt is reserved, or its handler was attached statically)
    NEOS_ADA_PRIMITIVE void neos_ada_interrupt_attach_static(std::int64_t aInterrupt, std::int64_t aHandler)
    {
        if (neos_ada_interrupt_reserved(aInterrupt) != 0 || gInterrupts[static_cast<std::size_t>(aInterrupt)].attachedStatically)
        {
            raise_predefined(1, "the interrupt is reserved, or its handler was attached statically");
            return;
        }
        auto& i = gInterrupts[static_cast<std::size_t>(aInterrupt)];
        i.previous = i.handler;
        i.attachedStatically = true;
        set_interrupt_handler(aInterrupt, aHandler);
    }

    // (a protected object finalized: the handlers that are its procedures detached (those attached statically: the handlers that they
    // replaced restored, C.3.1(12/3)))
    void detach_interrupt_handlers(std::int64_t aObject)
    {
        for (std::size_t n = 0u; n < gInterrupts.size(); ++n)
        {
            auto& i = gInterrupts[n];
            if (i.handler == 0 || *at<std::int64_t>(i.handler + 16) != aObject)
                continue;
            auto const previous = i.attachedStatically ? i.previous : 0;
            i.attachedStatically = false;
            i.previous = 0;
            set_interrupt_handler(static_cast<std::int64_t>(n), previous);
        }
    }

    // (ImpDef.Annex_C.Generate_Interrupt: an interrupt generated (its signal raised); handled now)
    NEOS_ADA_PRIMITIVE void neos_ada_interrupt_generate(std::int64_t aInterrupt)
    {
        if (auto const signal = interrupt_signal(aInterrupt); signal != 0)
        {
            std::raise(signal);
            deliver_interrupts();
        }
    }

    // --- termination handlers (C.7.3)

    // (a terminating task's handler: its specific handler, else the fallback handler of the nearest of its masters' tasks that has
    // one (C.7.3(9-10/2)); called as its last action with the cause of its termination (C.7.3(14/2)))
    void call_termination_handler(task& aTask, occurrence const& aOccurrence)
    {
        auto handler = aTask.specificHandler;
        for (auto master = aTask.master; handler == 0 && master != 0;)
        {
            auto* m = master == 1 ? &gEnvironment : task_of(master);
            if (m == nullptr)
                break;
            handler = m->fallbackHandler;
            master = m == &gEnvironment ? 0 : m->master;
        }
        if (handler == 0)
            return;
        // (the cause: Normal, Abnormal (aborted), Unhandled_Exception (the occurrence))
        bool const abnormal = aTask.aborted || static_cast<std::uint64_t>(aOccurrence.id) >= static_cast<std::uint64_t>(AtcSignalBase);
        std::int64_t const cause = abnormal ? 1 : aOccurrence.id != 0 ? 2 : 0;
        auto const occurrenceHandle = cause == 2 ? occurrence_keep(aOccurrence) : 0;
        auto const function = *at<std::int64_t>(handler);
        auto const link = *at<std::int64_t>(handler + 8);
        auto const object = *at<std::int64_t>(handler + 16);
        // (a protected procedure: its protected action)
        auto* p = object != 0 ? protected_of(object) : nullptr;
        auto const entryBodies = gEntryBodies;
        bool const begun = p == nullptr || protected_action_begin(*p);
        if (begun)
        {
            neos_rt_type const parameters[5] = { NEOS_RT_I64, NEOS_RT_I64, NEOS_RT_I64, NEOS_RT_I64, NEOS_RT_I64 };
            neos_rt_value arguments[5];
            arguments[0].i64 = link;
            arguments[1].i64 = object;
            arguments[2].i64 = cause;
            arguments[3].i64 = aTask.id;
            arguments[4].i64 = occurrenceHandle;
            neos_rt_value result;
            aTask.aborted = false;      // (the handler runs (it is not aborted))
            host()->call(static_cast<unsigned>(function), 5u, parameters, arguments, NEOS_RT_VOID, &result);
            if (p != nullptr)
            {
                service(*p);
                protected_action_end(p, entryBodies);
            }
        }
        // (an exception that the handler propagates: ignored (C.7.3(16/2)))
        take_pending();
        if (occurrenceHandle != 0)
            occurrence_release(occurrenceHandle);
    }

    NEOS_ADA_PRIMITIVE void neos_ada_termination_handler_set(std::int64_t aTask, std::int64_t aKind, std::int64_t aHandler)
    {
        auto* t = aTask == 1 ? &gEnvironment : task_of(aTask);
        if (t == nullptr)
            return;
        (aKind == 0 ? t->specificHandler : t->fallbackHandler) = aHandler;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_termination_handler(std::int64_t aTask, std::int64_t aKind)
    {
        auto* t = aTask == 1 ? &gEnvironment : task_of(aTask);
        return t == nullptr ? 0 : aKind == 0 ? t->specificHandler : t->fallbackHandler;
    }

    // --- priorities (D.1, D.5), policies (D.3, D.4), asynchronous task control (D.11)

    bool valid_priority(std::int64_t aPriority)
    {
        return aPriority >= 0 && aPriority <= 31;
    }

    // (a task's base priority as it is created (its Priority or Interrupt_Priority, D.1(17)); the environment task's (its main
    // subprogram's))
    NEOS_ADA_PRIMITIVE void neos_ada_task_priority(std::int64_t aTask, std::int64_t aPriority)
    {
        if (!valid_priority(aPriority))
        {
            raise_predefined(0, "priority out of range");
            return;
        }
        if (aTask == 1)
            gEnvironment.priority = aPriority;
        else if (auto* t = task_of(aTask))
            t->priority = aPriority;
    }

    // (Ada.Dynamic_Priorities.Set_Priority: a task's base priority (a dispatching point; its queued call placed by it, D.5.1))
    NEOS_ADA_PRIMITIVE void neos_ada_set_priority(std::int64_t aTask, std::int64_t aPriority)
    {
        auto* t = aTask == 1 ? &gEnvironment : task_of(aTask);
        if (t == nullptr || t->terminated)
            return;
        t->priority = aPriority;
        requeue_by_priority(*t);
        // (it goes to the tail of the ready queue of its new active priority (D.5.1(10)): the running task yields to the
        // ready tasks of that priority (not a dispatching point under Non_Preemptive_FIFO_Within_Priorities, D.2.4(9/3)))
        if (t == current())
        {
            if (policy_of(active_priority(*t)) != NonPreemptiveFifoWithinPriorities)
                yield();
            else
                replenish(*t);
        }
        else
        {
            t->readySequence = ++gReadySequence;
            replenish(*t);
        }
        dispatch();
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_get_priority(std::int64_t aTask)
    {
        auto* t = aTask == 1 ? &gEnvironment : task_of(aTask);
        return t != nullptr ? t->priority : DefaultPriority;
    }

    // (the partition's locking (Ceiling_Locking) and queuing (Priority_Queuing) policies)
    NEOS_ADA_PRIMITIVE void neos_ada_tasking_policies(std::int64_t aCeilingLocking, std::int64_t aPriorityQueuing)
    {
        gCeilingLocking = aCeilingLocking != 0;
        gPriorityQueuing = aPriorityQueuing != 0;
    }

    // (Ada.Asynchronous_Task_Control: a task held (it does not run until it is continued), continued (a dispatching point))
    NEOS_ADA_PRIMITIVE void neos_ada_task_hold(std::int64_t aTask, std::int64_t aHold)
    {
        auto* t = aTask == 1 ? &gEnvironment : task_of(aTask);
        if (t == nullptr || t->terminated)
            return;
        t->held = aHold != 0;
        if (t == current() && t->held && t != &gEnvironment)
        {
            // (the running task: it stops now)
            t->blocked = false;
            host()->coroutine_suspend();
            return;
        }
        dispatch();
    }

    // (the main subprogram has returned: the environment task is complete (not callable), awaiting the library level's tasks)
    NEOS_ADA_PRIMITIVE void neos_ada_environment_completed()
    {
        current();
        gEnvironment.completed = true;
    }

    // (Ada.Dispatching.Non_Preemptive.Yield_To_Higher: a dispatching point)
    NEOS_ADA_PRIMITIVE void neos_ada_dispatch()
    {
        dispatch(true);
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_task_held(std::int64_t aTask)
    {
        auto* t = aTask == 1 ? &gEnvironment : task_of(aTask);
        return t != nullptr && t->held ? 1 : 0;
    }


    // --- task dispatching policies (D.2.2-D.2.6), deadlines (D.2.6), execution time (D.14-D.14.2)

    // (the partition's task dispatching policy for a range of priorities)
    NEOS_ADA_PRIMITIVE void neos_ada_dispatching_policy(std::int64_t aFirst, std::int64_t aLast, std::int64_t aPolicy)
    {
        for (auto p = std::max<std::int64_t>(aFirst, 0); p <= aLast && p < static_cast<std::int64_t>(gPolicies.size()); ++p)
            gPolicies[static_cast<std::size_t>(p)] = aPolicy;
    }

    NEOS_ADA_PRIMITIVE void neos_ada_generate_deadlines()
    {
        gGenerateDeadlines = true;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_policy(std::int64_t aPriority)
    {
        return policy_of(aPriority);
    }

    // (Ada.Dispatching.Round_Robin: the quantum of a range of priorities, of a priority)
    NEOS_ADA_PRIMITIVE void neos_ada_quantum_set(std::int64_t aLow, std::int64_t aHigh, std::int64_t aQuantum)
    {
        for (auto p = std::max<std::int64_t>(aLow, 0); p <= aHigh && p < static_cast<std::int64_t>(gQuanta.size()); ++p)
            gQuanta[static_cast<std::size_t>(p)] = aQuantum;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_quantum(std::int64_t aPriority)
    {
        return aPriority >= 0 && aPriority < static_cast<std::int64_t>(gQuanta.size()) ? gQuanta[static_cast<std::size_t>(aPriority)] : DefaultQuantum;
    }

    task* task_or_environment(std::int64_t aTask)
    {
        current();
        return aTask == 1 ? &gEnvironment : task_of(aTask);
    }

    // (a task's execution time (that of a task no longer known: when it was last known))
    std::int64_t task_execution_time(std::int64_t aTask)
    {
        if (auto* t = task_or_environment(aTask))
            return execution_time(*t);
        auto const f = gFinalTimes.find(aTask);
        return f != gFinalTimes.end() ? f->second : 0;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_task_execution_time(std::int64_t aTask)
    {
        return task_execution_time(aTask);
    }

    // (Ada.Dispatching.EDF: a task's (base) deadline, relative deadline, last release time; setting a deadline is a dispatching
    // point (D.2.6(19/5, 21.1/5)))
    NEOS_ADA_PRIMITIVE void neos_ada_deadline_set(std::int64_t aTask, std::int64_t aDeadline)
    {
        if (auto* t = task_or_environment(aTask); t != nullptr && !t->terminated)
        {
            t->deadline = aDeadline;
            dispatch();
        }
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_deadline(std::int64_t aTask)
    {
        auto* t = task_or_environment(aTask);
        return t != nullptr ? t->deadline : TimeLast;
    }

    NEOS_ADA_PRIMITIVE void neos_ada_relative_deadline_set(std::int64_t aTask, std::int64_t aRelativeDeadline)
    {
        if (auto* t = task_or_environment(aTask))
            t->relativeDeadline = aRelativeDeadline;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_relative_deadline(std::int64_t aTask)
    {
        auto* t = task_or_environment(aTask);
        return t != nullptr ? t->relativeDeadline : TimeLast;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_release_time(std::int64_t aTask)
    {
        auto* t = task_or_environment(aTask);
        return t != nullptr ? t->releaseTime : TimeFirst;
    }

    // (Delay_Until_And_Set_Deadline: the running task's deadline when it is next made ready (its delay's end); then (its delay did
    // not block) the deadline taken now if it is not yet)
    NEOS_ADA_PRIMITIVE void neos_ada_release_deadline(std::int64_t aDeadline)
    {
        current()->releaseDeadline = aDeadline;
    }

    NEOS_ADA_PRIMITIVE void neos_ada_release_deadline_end()
    {
        auto* self = current();
        if (self->releaseDeadline != TimeFirst)
            self->deadline = self->releaseDeadline;
        else if (self->releaseOffset != TimeFirst)
            self->deadline = saturating_add(monotonic_now(), self->releaseOffset);
        else
            return;
        self->releaseDeadline = TimeFirst;
        self->releaseOffset = TimeFirst;
        dispatch();
    }

    // (Suspend_Until_True_And_Set_Deadline: the running task's deadline when it is next made ready: the clock then plus aOffset)
    NEOS_ADA_PRIMITIVE void neos_ada_release_offset(std::int64_t aOffset)
    {
        current()->releaseOffset = aOffset;
    }

    // (a task's Relative_Deadline (D.2.6(13/5)): its relative deadline, and its initial absolute deadline (the clock plus that), as
    // it is created (the environment task's: its main subprogram's))
    NEOS_ADA_PRIMITIVE void neos_ada_task_deadline(std::int64_t aTask, std::int64_t aRelativeDeadline)
    {
        if (auto* t = task_or_environment(aTask))
        {
            t->relativeDeadline = aRelativeDeadline;
            t->deadline = saturating_add(monotonic_now(), aRelativeDeadline);
        }
    }

    // (a protected object's relative deadline (D.3(13.2/5)): its Relative_Deadline; its Relative_Deadline attribute (D.5.2))
    NEOS_ADA_PRIMITIVE void neos_ada_po_relative_deadline_set(std::int64_t aSelf, std::int64_t aRelativeDeadline)
    {
        if (auto* p = protected_of(aSelf))
            p->relativeDeadline = aRelativeDeadline;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_po_relative_deadline(std::int64_t aSelf)
    {
        auto* p = protected_of(aSelf);
        return p != nullptr ? p->relativeDeadline : 0;
    }

    // (a protected object's Priority attribute (D.5.2): its value; assigned (Constraint_Error if it is not of Any_Priority; Program_Error
    // if the object has interrupt handlers (aHandlers) and it is not of Interrupt_Priority, D.5.2(6/3)))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_po_priority(std::int64_t aSelf)
    {
        auto* p = protected_of(aSelf);
        return p == nullptr ? PriorityLast : p->priority >= 0 ? p->priority : p->ceiling;
    }

    NEOS_ADA_PRIMITIVE void neos_ada_po_priority_set(std::int64_t aSelf, std::int64_t aPriority, std::int64_t aHandlers)
    {
        if (!valid_priority(aPriority))
        {
            raise_predefined(0, "priority out of range");
            return;
        }
        if (aHandlers != 0 && aPriority < 31)
        {
            raise_predefined(1, "the priority of a protected object with interrupt handlers must be an interrupt priority");
            return;
        }
        if (auto* p = protected_of(aSelf))
            p->priority = aPriority;
    }

    // (a task's processor (D.16, D.16.1): its CPU aspect (as it is created), Set_CPU; one that is not a processor fails its activation)
    NEOS_ADA_PRIMITIVE void neos_ada_task_cpu_set(std::int64_t aTask, std::int64_t aCpu)
    {
        auto* t = task_or_environment(aTask);
        if (t == nullptr)
            return;
        if (aCpu == 0 && gNoUnassignedCpu)
        {
            raise_predefined(1, "the partition's restriction No_Tasks_Unassigned_To_CPU is violated");
            return;
        }
        if (aCpu < 0 || aCpu > NumberOfCpus)
        {
            if (!t->started && t != &gEnvironment)
                t->cpuFailed = true;
            return;
        }
        t->cpu = aCpu;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_task_cpu(std::int64_t aTask)
    {
        auto* t = task_or_environment(aTask);
        return t != nullptr ? t->cpu : 0;
    }

    // (the partition's restrictions checked at run time (D.7): 1: Max_Entry_Queue_Length, 2: Max_Tasks, 3: Max_Asynchronous_Select_Nesting,
    // 4: No_Tasks_Unassigned_To_CPU, 5: No_Standard_Allocators_After_Elaboration)
    NEOS_ADA_PRIMITIVE void neos_ada_restriction(std::int64_t aKind, std::int64_t aValue)
    {
        switch (aKind)
        {
        case 1: gMaxEntryQueueLength = aValue; break;
        case 2: gMaxTasks = aValue; break;
        case 3: gMaxAtcNesting = aValue; break;
        case 4: gNoUnassignedCpu = true; break;
        case 5: gNoAllocatorsAfterElaboration = true; break;
        default: break;
        }
    }

    // (the partition's library items are elaborated (its main subprogram is called))
    NEOS_ADA_PRIMITIVE void neos_ada_elaborated()
    {
        gElaborated = true;
    }

    // (an allocator's storage from a standard storage pool, under No_Standard_Allocators_After_Elaboration (D.7(19.3/3)))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_allocate_standard(std::int64_t aSize)
    {
        if (gNoAllocatorsAfterElaboration && gElaborated)
        {
            raise_predefined(2, "the partition's restriction No_Standard_Allocators_After_Elaboration is violated");
            return 0;
        }
        auto const result = allocate(static_cast<std::uint64_t>(aSize < 0 ? 0 : aSize));
        if (result == 0)
            raise_predefined(2, "heap exhausted");
        return result;
    }

    // (a protected object's processor (D.16(15/5)))
    NEOS_ADA_PRIMITIVE void neos_ada_po_cpu(std::int64_t aSelf, std::int64_t aCpu)
    {
        if (auto* p = protected_of(aSelf))
            p->cpu = aCpu;
    }

    // (the ceiling priority of a protected procedure's object (an access value's descriptor: -1 if it has none))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_handler_ceiling(std::int64_t aHandler)
    {
        if (aHandler == 0)
            return -1;
        auto const object = *at<std::int64_t>(aHandler + 16);
        auto* p = object != 0 ? protected_of(object) : nullptr;
        return p != nullptr ? p->ceiling : -1;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_ceiling_locking()
    {
        return gCeilingLocking ? 1 : 0;
    }

    // (a poll (an iteration of a loop, when the partition specifies a task dispatching policy or uses execution-time events): at
    // most every PollInterval, the interrupts that have occurred handled and a dispatching point (the tasks that have become ready
    // meanwhile: an expired delay, an execution-time event, an exhausted budget)); counted, so that the clock is seldom read)
    std::uint32_t gPolls = 0u;
    std::int64_t gNextPoll = 0;

    NEOS_ADA_PRIMITIVE void neos_ada_poll()
    {
        if ((++gPolls & 63u) != 0u)
            return;
        auto const now = monotonic_now();
        if (now < gNextPoll)
            return;
        gNextPoll = now + PollInterval;
        deliver_interrupts();
        dispatch();
    }

    // execution-time events (D.14.1, D.14.2): an event expires when the execution time of its member tasks since their bases
    // reaches its limit; a server task (of Ada.Execution_Time.Timers, Group_Budgets) waits for its kind's events and executes
    // their handlers
    struct cpu_event
    {
        std::int64_t kind = 0;              ///< (1: an execution-time timer's, 2: a group budget's)
        bool armed = false;
        std::int64_t limit = 0;
        std::vector<std::pair<std::int64_t, std::int64_t>> members;     ///< (its tasks, and their execution times as its bases)
    };

    std::map<std::int64_t, cpu_event> gCpuEvents;   ///< (by key (the address of the object that it is for))

    std::int64_t consumed(cpu_event const& aEvent)
    {
        std::int64_t result = 0;
        for (auto const& [t, base] : aEvent.members)
            result = saturating_add(result, task_execution_time(t) - base);
        return result;
    }

    bool cpu_event_live(cpu_event const& aEvent)
    {
        return aEvent.armed && std::any_of(aEvent.members.begin(), aEvent.members.end(), [](auto const& m)
            {
                auto* t = task_or_environment(m.first);
                return t != nullptr && !t->terminated;
            });
    }

    NEOS_ADA_PRIMITIVE void neos_ada_cpu_event_create(std::int64_t aKey, std::int64_t aKind)
    {
        gCpuEvents[aKey].kind = aKind;
    }

    NEOS_ADA_PRIMITIVE void neos_ada_cpu_event_delete(std::int64_t aKey)
    {
        gCpuEvents.erase(aKey);
    }

    // (a task added to an event's members (its base: its execution time now), or removed (its execution time since its base
    // still counted: the limit less it))
    NEOS_ADA_PRIMITIVE void neos_ada_cpu_event_member(std::int64_t aKey, std::int64_t aTask, std::int64_t aAdd)
    {
        auto& e = gCpuEvents[aKey];
        auto i = std::find_if(e.members.begin(), e.members.end(), [aTask](auto const& m) { return m.first == aTask; });
        if (aAdd != 0)
        {
            if (i == e.members.end())
                e.members.emplace_back(aTask, task_execution_time(aTask));
        }
        else if (i != e.members.end())
        {
            e.limit = saturating_add(e.limit, -(task_execution_time(aTask) - i->second));
            e.members.erase(i);
        }
    }

    // (an event armed (its limit, as of its bases) or disarmed)
    NEOS_ADA_PRIMITIVE void neos_ada_cpu_event_arm(std::int64_t aKey, std::int64_t aLimit, std::int64_t aArmed)
    {
        auto& e = gCpuEvents[aKey];
        e.limit = aLimit;
        e.armed = aArmed != 0;
    }

    // (whether an event of a kind can expire: armed, and a member task not terminated)
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_cpu_event_live(std::int64_t aKind)
    {
        return std::any_of(gCpuEvents.begin(), gCpuEvents.end(), [aKind](auto const& e) { return e.second.kind == aKind && cpu_event_live(e.second); }) ? 1 : 0;
    }

    // (a dispatching point of the running task (e.g. an execution-time event set, which may have expired))
    NEOS_ADA_PRIMITIVE void neos_ada_preempt()
    {
        dispatch();
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_cpu_event_consumed(std::int64_t aKey)
    {
        auto const e = gCpuEvents.find(aKey);
        return e != gCpuEvents.end() ? consumed(e->second) : 0;
    }

    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_cpu_event_limit(std::int64_t aKey)
    {
        auto const e = gCpuEvents.find(aKey);
        return e != gCpuEvents.end() ? e->second.limit : 0;
    }

    // (an event's bases now its members' execution times, its limit less what they have consumed)
    NEOS_ADA_PRIMITIVE void neos_ada_cpu_event_rebase(std::int64_t aKey)
    {
        auto& e = gCpuEvents[aKey];
        e.limit = saturating_add(e.limit, -consumed(e));
        for (auto& m : e.members)
            m.second = task_execution_time(m.first);
    }

    // (the server of a kind of events: waits until one of them expires (its key; it is disarmed), or until none can expire (0); -1: a
    // deadlock (no task can run))
    NEOS_ADA_PRIMITIVE std::int64_t neos_ada_cpu_event_wait(std::int64_t aKind)
    {
        auto const due = [aKind]() -> std::int64_t
        {
            for (auto const& [key, e] : gCpuEvents)
                if (e.kind == aKind && e.armed && consumed(e) >= e.limit)
                    return key;
            return 0;
        };
        auto const idle = [aKind]()
        {
            return std::none_of(gCpuEvents.begin(), gCpuEvents.end(), [aKind](auto const& e) { return e.second.kind == aKind && cpu_event_live(e.second); });
        };
        if (!block_until([&]() { return due() != 0 || idle(); }))
            return -1;
        auto const key = due();
        if (key != 0)
            gCpuEvents[key].armed = false;
        return key;
    }

    constexpr auto I64 = NEOS_RT_I64;
    constexpr auto VOID = NEOS_RT_VOID;

    function_table const sTaskingFunctions{
        { "dispatching_policy", NEOS_RT_FUNCTION(neos_ada_dispatching_policy), VOID, 3u, { I64, I64, I64 }, 0u },
        { "generate_deadlines", NEOS_RT_FUNCTION(neos_ada_generate_deadlines), VOID, 0u, {}, 0u },
        { "policy", NEOS_RT_FUNCTION(neos_ada_policy), I64, 1u, { I64 }, 0u },
        { "quantum_set", NEOS_RT_FUNCTION(neos_ada_quantum_set), VOID, 3u, { I64, I64, I64 }, 0u },
        { "quantum", NEOS_RT_FUNCTION(neos_ada_quantum), I64, 1u, { I64 }, 0u },
        { "task_execution_time", NEOS_RT_FUNCTION(neos_ada_task_execution_time), I64, 1u, { I64 }, 0u },
        { "deadline_set", NEOS_RT_FUNCTION(neos_ada_deadline_set), VOID, 2u, { I64, I64 }, 0u },
        { "deadline", NEOS_RT_FUNCTION(neos_ada_deadline), I64, 1u, { I64 }, 0u },
        { "relative_deadline_set", NEOS_RT_FUNCTION(neos_ada_relative_deadline_set), VOID, 2u, { I64, I64 }, 0u },
        { "relative_deadline", NEOS_RT_FUNCTION(neos_ada_relative_deadline), I64, 1u, { I64 }, 0u },
        { "release_time", NEOS_RT_FUNCTION(neos_ada_release_time), I64, 1u, { I64 }, 0u },
        { "release_deadline", NEOS_RT_FUNCTION(neos_ada_release_deadline), VOID, 1u, { I64 }, 0u },
        { "release_deadline_end", NEOS_RT_FUNCTION(neos_ada_release_deadline_end), VOID, 0u, {}, 0u },
        { "release_offset", NEOS_RT_FUNCTION(neos_ada_release_offset), VOID, 1u, { I64 }, 0u },
        { "task_deadline", NEOS_RT_FUNCTION(neos_ada_task_deadline), VOID, 2u, { I64, I64 }, 0u },
        { "po_relative_deadline_set", NEOS_RT_FUNCTION(neos_ada_po_relative_deadline_set), VOID, 2u, { I64, I64 }, 0u },
        { "po_relative_deadline", NEOS_RT_FUNCTION(neos_ada_po_relative_deadline), I64, 1u, { I64 }, 0u },
        { "handler_ceiling", NEOS_RT_FUNCTION(neos_ada_handler_ceiling), I64, 1u, { I64 }, 0u },
        { "po_priority", NEOS_RT_FUNCTION(neos_ada_po_priority), I64, 1u, { I64 }, 0u },
        { "task_cpu_set", NEOS_RT_FUNCTION(neos_ada_task_cpu_set), VOID, 2u, { I64, I64 }, 0u },
        { "restriction", NEOS_RT_FUNCTION(neos_ada_restriction), VOID, 2u, { I64, I64 }, 0u },
        { "elaborated", NEOS_RT_FUNCTION(neos_ada_elaborated), VOID, 0u, {}, 0u },
        { "allocate_standard", NEOS_RT_FUNCTION(neos_ada_allocate_standard), I64, 1u, { I64 }, 0u },
        { "task_cpu", NEOS_RT_FUNCTION(neos_ada_task_cpu), I64, 1u, { I64 }, 0u },
        { "po_cpu", NEOS_RT_FUNCTION(neos_ada_po_cpu), VOID, 2u, { I64, I64 }, 0u },
        { "po_priority_set", NEOS_RT_FUNCTION(neos_ada_po_priority_set), VOID, 3u, { I64, I64, I64 }, 0u },
        { "ceiling_locking", NEOS_RT_FUNCTION(neos_ada_ceiling_locking), I64, 0u, {}, 0u },
        { "poll", NEOS_RT_FUNCTION(neos_ada_poll), VOID, 0u, {}, 0u },
        { "cpu_event_create", NEOS_RT_FUNCTION(neos_ada_cpu_event_create), VOID, 2u, { I64, I64 }, 0u },
        { "cpu_event_delete", NEOS_RT_FUNCTION(neos_ada_cpu_event_delete), VOID, 1u, { I64 }, 0u },
        { "cpu_event_member", NEOS_RT_FUNCTION(neos_ada_cpu_event_member), VOID, 3u, { I64, I64, I64 }, 0u },
        { "cpu_event_arm", NEOS_RT_FUNCTION(neos_ada_cpu_event_arm), VOID, 3u, { I64, I64, I64 }, 0u },
        { "cpu_event_consumed", NEOS_RT_FUNCTION(neos_ada_cpu_event_consumed), I64, 1u, { I64 }, 0u },
        { "cpu_event_limit", NEOS_RT_FUNCTION(neos_ada_cpu_event_limit), I64, 1u, { I64 }, 0u },
        { "cpu_event_rebase", NEOS_RT_FUNCTION(neos_ada_cpu_event_rebase), VOID, 1u, { I64 }, 0u },
        { "cpu_event_wait", NEOS_RT_FUNCTION(neos_ada_cpu_event_wait), I64, 1u, { I64 }, 0u },
        { "cpu_event_live", NEOS_RT_FUNCTION(neos_ada_cpu_event_live), I64, 1u, { I64 }, 0u },
        { "preempt", NEOS_RT_FUNCTION(neos_ada_preempt), VOID, 0u, {}, 0u },
        { "requeue", NEOS_RT_FUNCTION(neos_ada_requeue), VOID, 5u, { I64, I64, I64, I64, I64 }, 0u },
        { "task_create", NEOS_RT_FUNCTION(neos_ada_task_create), I64, 4u, { I64, I64, I64, I64 }, 0u },
        { "task_priority", NEOS_RT_FUNCTION(neos_ada_task_priority), VOID, 2u, { I64, I64 }, 0u },
        { "set_priority", NEOS_RT_FUNCTION(neos_ada_set_priority), VOID, 2u, { I64, I64 }, 0u },
        { "get_priority", NEOS_RT_FUNCTION(neos_ada_get_priority), I64, 1u, { I64 }, 0u },
        { "tasking_policies", NEOS_RT_FUNCTION(neos_ada_tasking_policies), VOID, 2u, { I64, I64 }, 0u },
        { "task_hold", NEOS_RT_FUNCTION(neos_ada_task_hold), VOID, 2u, { I64, I64 }, 0u },
        { "task_held", NEOS_RT_FUNCTION(neos_ada_task_held), I64, 1u, { I64 }, 0u },
        { "dispatch", NEOS_RT_FUNCTION(neos_ada_dispatch), VOID, 0u, {}, 0u },
        { "interrupt_reserved", NEOS_RT_FUNCTION(neos_ada_interrupt_reserved), I64, 1u, { I64 }, 0u },
        { "interrupt_handler_register", NEOS_RT_FUNCTION(neos_ada_interrupt_handler_register), VOID, 1u, { I64 }, 0u },
        { "interrupt_attach", NEOS_RT_FUNCTION(neos_ada_interrupt_attach), I64, 2u, { I64, I64 }, 0u },
        { "interrupt_handler", NEOS_RT_FUNCTION(neos_ada_interrupt_handler), I64, 1u, { I64 }, 0u },
        { "interrupt_attach_static", NEOS_RT_FUNCTION(neos_ada_interrupt_attach_static), VOID, 2u, { I64, I64 }, 0u },
        { "interrupt_generate", NEOS_RT_FUNCTION(neos_ada_interrupt_generate), VOID, 1u, { I64 }, 0u },
        { "environment_completed", NEOS_RT_FUNCTION(neos_ada_environment_completed), VOID, 0u, {}, 0u },
        { "entry_caller", NEOS_RT_FUNCTION(neos_ada_entry_caller), I64, 1u, { I64 }, 0u },
        { "termination_handler_set", NEOS_RT_FUNCTION(neos_ada_termination_handler_set), VOID, 3u, { I64, I64, I64 }, 0u },
        { "termination_handler", NEOS_RT_FUNCTION(neos_ada_termination_handler), I64, 2u, { I64, I64 }, 0u },
        { "po_enter", NEOS_RT_FUNCTION(neos_ada_po_enter), VOID, 1u, { I64 }, 0u },
        { "po_leave", NEOS_RT_FUNCTION(neos_ada_po_leave), VOID, 1u, { I64 }, 0u },
        { "po_ceiling", NEOS_RT_FUNCTION(neos_ada_po_ceiling), VOID, 2u, { I64, I64 }, 0u },
        { "collection_create", NEOS_RT_FUNCTION(neos_ada_collection_create), I64, 0u, {}, 0u },
        { "task_activate", NEOS_RT_FUNCTION(neos_ada_task_activate), VOID, 0u, {}, 0u },
        { "task_pending", NEOS_RT_FUNCTION(neos_ada_task_pending), I64, 0u, {}, 0u },
        { "task_activate_from", NEOS_RT_FUNCTION(neos_ada_task_activate_from), VOID, 1u, { I64 }, 0u },
        { "task_activate_ranges", NEOS_RT_FUNCTION(neos_ada_task_activate_ranges), VOID, 3u, { I64, I64, I64 }, 0u },
        { "task_elaboration", NEOS_RT_FUNCTION(neos_ada_task_elaboration), VOID, 2u, { I64, I64 }, 0u },
        { "task_relocate", NEOS_RT_FUNCTION(neos_ada_task_relocate), VOID, 2u, { I64, I64 }, 0u },
        { "po_finalize", NEOS_RT_FUNCTION(neos_ada_po_finalize), VOID, 1u, { I64 }, 0u },
        { "task_unregister", NEOS_RT_FUNCTION(neos_ada_task_unregister), VOID, 1u, { I64 }, 0u },
        { "task_discard_from", NEOS_RT_FUNCTION(neos_ada_task_discard_from), VOID, 1u, { I64 }, 0u },
        { "task_activated", NEOS_RT_FUNCTION(neos_ada_task_activated), VOID, 0u, {}, 0u },
        { "task_callable", NEOS_RT_FUNCTION(neos_ada_task_callable), I64, 1u, { I64 }, 0u },
        { "task_terminated", NEOS_RT_FUNCTION(neos_ada_task_terminated), I64, 1u, { I64 }, 0u },
        { "current_task", NEOS_RT_FUNCTION(neos_ada_current_task), I64, 0u, {}, 0u },
        { "task_abort", NEOS_RT_FUNCTION(neos_ada_task_abort), VOID, 1u, { I64 }, 0u },
        { "delay", NEOS_RT_FUNCTION(neos_ada_delay), VOID, 2u, { I64, I64 }, 0u },
        { "abort_defer", NEOS_RT_FUNCTION(neos_ada_abort_defer), VOID, 0u, {}, 0u },
        { "abort_undefer", NEOS_RT_FUNCTION(neos_ada_abort_undefer), VOID, 1u, { I64 }, 0u },
        { "abort_check", NEOS_RT_FUNCTION(neos_ada_abort_check), VOID, 0u, {}, 0u },
        { "entry_call", NEOS_RT_FUNCTION(neos_ada_entry_call), I64, 5u, { I64, I64, I64, I64, I64 }, 0u },
        { "accept", NEOS_RT_FUNCTION(neos_ada_accept), I64, 1u, { I64 }, 0u },
        { "call_params", NEOS_RT_FUNCTION(neos_ada_call_params), I64, 1u, { I64 }, 0u },
        { "accept_done", NEOS_RT_FUNCTION(neos_ada_accept_done), VOID, 2u, { I64, I64 }, 0u },
        { "select", NEOS_RT_FUNCTION(neos_ada_select), I64, 6u, { I64, I64, I64, I64, I64, I64 }, 0u },
        { "accepted_call", NEOS_RT_FUNCTION(neos_ada_accepted_call), I64, 0u, {}, 0u },
        { "entry_count", NEOS_RT_FUNCTION(neos_ada_entry_count), I64, 1u, { I64 }, 0u },
        { "po_create", NEOS_RT_FUNCTION(neos_ada_po_create), I64, 5u, { I64, I64, I64, I64, I64 }, 0u },
        { "po_service", NEOS_RT_FUNCTION(neos_ada_po_service), VOID, 1u, { I64 }, 0u },
        { "po_call", NEOS_RT_FUNCTION(neos_ada_po_call), I64, 6u, { I64, I64, I64, I64, I64, I64 }, 0u },
        { "po_count", NEOS_RT_FUNCTION(neos_ada_po_count), I64, 2u, { I64, I64 }, 0u },
        { "po_count_family", NEOS_RT_FUNCTION(neos_ada_po_count_family), I64, 3u, { I64, I64, I64 }, 0u },
        { "atc_delay", NEOS_RT_FUNCTION(neos_ada_atc_delay), I64, 2u, { I64, I64 }, 0u },
        { "atc_entry_call", NEOS_RT_FUNCTION(neos_ada_atc_entry_call), I64, 3u, { I64, I64, I64 }, 0u },
        { "atc_po_call", NEOS_RT_FUNCTION(neos_ada_atc_po_call), I64, 4u, { I64, I64, I64, I64 }, 0u },
        { "atc_triggered", NEOS_RT_FUNCTION(neos_ada_atc_triggered), I64, 1u, { I64 }, 0u },
        { "atc_end", NEOS_RT_FUNCTION(neos_ada_atc_end), I64, 1u, { I64 }, 0u },
    };
}

namespace neos::ada_runtime
{
    void abort_defer()
    {
        ++current()->deferAbort;
    }

    void abort_undefer(bool aSignal)
    {
        auto* self = current();
        if (self->deferAbort > 0)
            --self->deferAbort;
        if (!aSignal || self->deferAbort != 0)
            return;
        auto& s = state();
        if (s.excGlobal != nullptr && *s.excGlobal != 0u)
            return;
        if (self->aborted && self != &gEnvironment)
            set_pending(AbortSignal);
        else if (!self->atc.empty() && atc_triggered(*self) != 0)
            set_pending(AtcSignalBase + atc_triggered(*self));
    }

    void protected_finalize(std::int64_t aObject)
    {
        // (a protected object finalized: the calls queued on its entries fail (Program_Error), and their callers can run
        // (they are told before the object's components are finalized))
        auto existing = gProtected.find(aObject);
        if (existing == gProtected.end())
            return;
        if (gInterruptsAttached > 0)
            detach_interrupt_handlers(existing->second->self);
        bool failed = false;
        for (auto& [entry, queue] : existing->second->queues)
        {
            for (auto* c : queue)
            {
                c->failed = true;
                c->done = true;
                c->exception = occurrence{};
                c->exception.id = predefined_id(1);
                c->exception.message = "the protected object is finalized";
                failed = true;
            }
            queue.clear();
        }
        if (!failed)
            return;
        auto* self = current();
        if (self == &gEnvironment)
            run_one();
        else
        {
            self->blocked = false;
            host()->coroutine_suspend();
        }
    }

    std::vector<std::int64_t> collection_tasks(std::int64_t aCollection)
    {
        auto c = gCollections.find(aCollection);
        if (c == gCollections.end())
            return {};
        auto result = std::move(c->second);
        gCollections.erase(c);
        return result;
    }

    // (a task waiting at an open terminate alternative with no call queued, whose dependents are each terminated or likewise
    // (9.3(6/1)): it can be terminated)
    bool terminable(task const& aTask, int aDepth = 0)
    {
        if (!aTask.atTerminate || aDepth > 64)
            return false;
        for (auto const& [entry, queue] : aTask.entries)
            if (!queue.empty())
                return false;
        for (auto const& [id, u] : gTasks)
            if (u->master == aTask.id && u.get() != &aTask && u->started && !u->terminated && !terminable(*u, aDepth + 1))
                return false;
        return true;
    }

    void await_tasks(std::vector<std::int64_t> const& aTasks)
    {
        // (a master's dependent tasks: until each is terminated, or waiting at a terminate alternative (with no call queued);
        // those then terminate)
        std::vector<task*> tasks;
        for (auto id : aTasks)
            if (auto* t = task_of(id); t != nullptr && t != &gEnvironment)
                tasks.push_back(t);
        auto const done = [&tasks](bool aTerminating)
        {
            return std::all_of(tasks.begin(), tasks.end(), [aTerminating](task* t)
                {
                    if (!t->started || t->terminated)
                        return true;
                    if (!aTerminating)
                        return false;
                    return terminable(*t);
                });
        };
        for (auto* t : tasks)
            if (!t->started)
                t->terminated = true;
        block_until([&]() { return done(true); });
        for (auto* t : tasks)
            if (!t->terminated && t->atTerminate)
                t->terminateRequested = true;
        block_until([&]() { return done(false); });
        for (auto* t : tasks)
            if (t->terminated && t->coroutine == nullptr)
            {
                // (its execution time, if an execution-time event needs it)
                if (std::any_of(gCpuEvents.begin(), gCpuEvents.end(), [t](auto const& e)
                    { return std::any_of(e.second.members.begin(), e.second.members.end(), [t](auto const& m) { return m.first == t->id; }); }))
                    gFinalTimes[t->id] = t->executionTime;
                gTasks.erase(t->id);
            }
    }
}
