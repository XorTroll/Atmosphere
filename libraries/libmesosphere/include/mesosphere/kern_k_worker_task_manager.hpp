/*
 * Copyright (c) 2018-2020 Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once
#include <mesosphere/kern_common.hpp>
#include <mesosphere/kern_k_worker_task.hpp>
#include <mesosphere/kern_k_thread.hpp>

namespace ams::kern {

    class KWorkerTaskManager {
        public:
            static constexpr s32 ExitWorkerPriority = 11;

            enum WorkerType {
                WorkerType_Exit,

                WorkerType_Count,
            };
        private:
            KWorkerTask *head_task;
            KWorkerTask *tail_task;
            KThread *thread;
            WorkerType type;
            bool active;
        private:
            static void ThreadFunction(uintptr_t arg);
            void ThreadFunctionImpl();

            KWorkerTask *GetTask();
            void AddTask(KWorkerTask *task);
        public:
            constexpr KWorkerTaskManager() : head_task(), tail_task(), thread(), type(WorkerType_Count), active() { /* ... */ }

            NOINLINE void Initialize(WorkerType wt, s32 priority);
            static void AddTask(WorkerType type, KWorkerTask *task);
    };

}
