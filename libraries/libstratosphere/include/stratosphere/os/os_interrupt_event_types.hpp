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
#include <vapours.hpp>
#include <stratosphere/os/impl/os_internal_critical_section.hpp>
#include <stratosphere/os/impl/os_internal_condition_variable.hpp>

namespace ams::os {

    namespace impl {

        class WaitableObjectList;
        class InterruptEventImpl;

    }

    struct InterruptEventType {
        enum State {
            State_NotInitialized = 0,
            State_Initialized    = 1,
        };

        util::TypedStorage<impl::WaitableObjectList, sizeof(util::IntrusiveListNode), alignof(util::IntrusiveListNode)> waitable_object_list_storage;

        u8 clear_mode;
        u8 state;

        util::TypedStorage<impl::InterruptEventImpl, sizeof(svc::Handle) * 2, alignof(svc::Handle)> impl;
    };
    static_assert(std::is_trivial<InterruptEventType>::value);

}
