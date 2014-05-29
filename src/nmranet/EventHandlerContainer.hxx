/** \copyright
 * Copyright (c) 2013, Stuart W Baker
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are  permitted provided that the following conditions are met:
 * 
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \file EventHandlerContainer.hxx
 *
 * Defines containers of event handlers that are able to iterate through the
 * registered event handlers that ought to be called for a given incoming
 * event.
 *
 * @author Balazs Racz
 * @date 3 November 2013
 */

#ifndef _NMRANET_EVENTHANDLERCONTAINER_HXX_
#define _NMRANET_EVENTHANDLERCONTAINER_HXX_

#include <algorithm>
#include <vector>
#include <forward_list>
#include <endian.h>

#ifndef LOGLEVEL
#define LOGLEVEL VERBOSE
#endif

#include "utils/Atomic.hxx"
#include "utils/logging.h"
//#include "nmranet/EventService.hxx"
#include "nmranet/EventHandler.hxx"
//#include "if/nmranet_if.h"
//#include "core/nmranet_event.h"
#include "nmranet/EventHandlerTemplates.hxx"

namespace nmranet
{

// Abstract class for representing iteration through a container for event
// handlers.
class EventIterator {
protected:
    /// Creates an EventIterator.
    EventIterator() {}

public:
    virtual ~EventIterator() {}

    /** Steps the iteration.
     * @returns the next entry or NULL if the iteration is done.
     * May be called many times after the iteratin is ended and should
     * consistently return NULL. */
    virtual EventHandler* next_entry() = 0;

    /** Starts the iteration. If the iteration is not done yet, call
     * clear_iteration first.
     *
     * @param event is the event report to reset the iteration for. */
    virtual void init_iteration(EventReport* event) = 0;

    /** Stops iteration and resets iteration variables. */
    virtual void clear_iteration() = 0;
};

template<class C> class FullContainerIterator : public EventIterator {
public:
    FullContainerIterator(C* container)
        : container_(container) {
        clear_iteration();
    }
    EventHandler* next_entry() OVERRIDE {
        if (it_ == container_->end()) return nullptr;
        EventHandler* h = *it_;
        ++it_;
        return h;
    }
    void clear_iteration() OVERRIDE {
        it_ = container_->end();
    }
    void init_iteration(EventReport*) OVERRIDE {
        it_ = container_->begin();
    }

private:
    typename C::iterator it_;
    C* container_;
};

class VectorEventHandlers : public EventRegistry {
 public:
    VectorEventHandlers() {}

    // Creates a new event iterator. Caller takes ownership of object.
    EventIterator* create_iterator() OVERRIDE {
        return new FullContainerIterator<HandlersList>(&handlers_);
    }

  virtual void register_handlerr(EventHandler* handler, EventId event, unsigned mask) {
    // @TODO(balazs.racz): need some kind of locking here.
    handlers_.push_front(handler);
  }
  virtual void unregister_handlerr(EventHandler* handler, EventId event, unsigned mask) {
    // @TODO(balazs.racz): need some kind of locking here.
    handlers_.remove(handler);
  }

 private:
  typedef std::forward_list<EventHandler*> HandlersList;
  HandlersList handlers_;
};

class TreeEventHandlers : public EventRegistry, private Atomic {
public:
    TreeEventHandlers();

    EventIterator* create_iterator() OVERRIDE;
    void register_handlerr(EventHandler* handler, EventId event, unsigned mask) OVERRIDE;
    void unregister_handlerr(EventHandler* handler, EventId event, unsigned mask) OVERRIDE;

private:
    class Iterator;
    friend class Iterator;

    typedef std::multimap<uint64_t, EventHandler*> OneMaskMap;
    typedef std::map<uint8_t, OneMaskMap> MaskLookupMap;
    /** The registered handlers. The offset in the first map tell us how many
     * bits wide the registration is (it is the mask value in the register
     * call).*/
    MaskLookupMap handlers_;
};

}; /* namespace nmranet */

#endif  // _NMRANET_EVENTHANDLERCONTAINER_HXX_