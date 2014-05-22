/** \copyright
 * Copyright (c) 2014, Balazs Racz
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
 * \file Loco.hxx
 *
 * Defines a simple DCC locomotive.
 *
 * @author Balazs Racz
 * @date 10 May 2014
 */

#ifndef _DCC_LOCO_HXX_
#define _DCC_LOCO_HXX_

#include "utils/logging.h"
#include "dcc/Packet.hxx"
#include "dcc/PacketSource.hxx"

namespace dcc
{

enum DccTrainUpdateCode
{
    REFRESH = 0,
    SPEED = 1,
    FUNCTION0 = 2,
    FUNCTION5 = 3,
    FUNCTION9 = 4,
    FUNCTION13 = 5,
    FUNCTION21 = 6,
    MIN_REFRESH = SPEED,
    /** @TODO(balazs.racz) choose adaptive max-refresh based on how many
     * functions are actually in use for the loco. */
    MAX_REFRESH = FUNCTION9,
    ESTOP = 16,
};

template <class P> class AbstractTrain : public PacketSource
{
public:
    AbstractTrain()
    {
    }

    void set_speed(SpeedType speed) OVERRIDE
    {
        float16_t new_speed = speed.get_wire();
        if (p.lastSetSpeed_ == new_speed)
        {
            LOG(VERBOSE, "not updating speed: old speed %04x, new speed %04x",
                p.lastSetSpeed_, new_speed);
            return;
        }
        p.lastSetSpeed_ = new_speed;
        if (speed.direction() != p.direction_)
        {
            p.directionChanged_ = 1;
            p.direction_ = speed.direction();
        }
        float f_speed = speed.mph();
        if (f_speed > 0)
        {
            f_speed *= (p.get_speed_steps() / 128);
            unsigned sp = f_speed;
            sp++; // makes sure it is at least speed step 1.
            if (sp > 28)
                sp = 28;
            LOG(VERBOSE, "set speed to step %u", sp);
            p.speed_ = sp;
        }
        else
        {
            p.speed_ = 0;
        }
        packet_processor_notify_update(this, SPEED);
    }

    SpeedType get_speed() OVERRIDE
    {
        SpeedType v;
        v.set_wire(p.lastSetSpeed_);
        return v;
    }
    SpeedType get_commanded_speed() OVERRIDE
    {
        return get_speed();
    }
    void set_emergencystop() OVERRIDE
    {
        p.speed_ = 0;
        SpeedType dir0;
        dir0.set_direction(p.direction_);
        p.lastSetSpeed_ = dir0.get_wire();
        p.directionChanged_ = 1;
        packet_processor_notify_update(this, ESTOP);
    }
    void set_fn(uint32_t address, uint16_t value) OVERRIDE
    {
        if (address > p.get_max_fn())
        {
            // Ignore.
            return;
        }
        unsigned bit = 1 << address;
        if (value)
        {
            p.fn_ |= bit;
        }
        else
        {
            p.fn_ &= ~bit;
        }
        packet_processor_notify_update(this, p.get_fn_update_code(address));
    }
    uint16_t get_fn(uint32_t address) OVERRIDE
    {
        if (address > p.get_max_fn())
        {
            // Unknown.
            return 0;
        }
        return (p.fn_ & (1 << address)) ? 1 : 0;
    }
    uint32_t legacy_address() OVERRIDE
    {
        return p.address_;
    }

protected:
    // Payload -- actual data we know about the train.
    P p;
};

struct Dcc28Payload
{
    Dcc28Payload()
    {
        memset(this, 0, sizeof(*this));
    }
    // largest address allowed is 10239.
    unsigned address_ : 14;
    unsigned isShortAddress_ : 1;
    // 0: forward, 1: reverse
    unsigned direction_ : 1;
    unsigned lastSetSpeed_ : 16;
    // functions f0-f28.
    unsigned fn_ : 29;
    // Which refresh packet should go out next.
    unsigned nextRefresh_ : 3;
    unsigned speed_ : 5;
    unsigned directionChanged_ : 1;

    /** @Returns the number of speed steps (in float). */
    float get_speed_steps()
    {
        return 28.0f;
    }

    /** @Returns the largest function number that is still valid. */
    unsigned get_max_fn()
    {
        return 28;
    }

    /** @returns the update code to send ot the packet handler for a given
     * function value change. */
    unsigned get_fn_update_code(unsigned address);
};

class Dcc28Train : public AbstractTrain<Dcc28Payload>
{
public:
    Dcc28Train(DccShortAddress a);
    Dcc28Train(DccLongAddress a);
    ~Dcc28Train();

    // Generates next outgoing packet.
    void get_next_packet(unsigned code, Packet *packet) OVERRIDE;
};

struct MMOldPayload
{
    MMOldPayload()
    {
        memset(this, 0, sizeof(*this));
    }
    // largest address allowed is 80, but we keep a few more bits around to
    // allow for an extension to arbitrary MM address packets.
    unsigned address_ : 8;
    unsigned lastSetSpeed_ : 16;
    unsigned fn_ : 1;
    // 0: forward, 1: reverse
    unsigned direction_ : 1;
    unsigned directionChanged_ : 1;
    unsigned speed_ : 4;

    /** @Returns the number of speed steps (in float). */
    float get_speed_steps()
    {
        return 14.0f;
    }

    /** @Returns the largest function number that is still valid. */
    unsigned get_max_fn()
    {
        return 0;
    }

    /** @returns the update code to send ot the packet handler for a given
     * function value change. */
    unsigned get_fn_update_code(unsigned address) {
        return SPEED;
    }
};

class MMOldTrain : public AbstractTrain<MMOldPayload>
{
public:
    MMOldTrain(MMAddress a);
    ~MMOldTrain();

    // Generates next outgoing packet.
    void get_next_packet(unsigned code, Packet *packet) OVERRIDE;
};

} // namespace dcc

#endif // _DCC_LOCO_HXX_