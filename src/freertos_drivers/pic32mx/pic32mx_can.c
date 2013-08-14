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
 * \file pic32mx_can.c
 *
 * This file implements a can device driver layer for the pic32mx using the
 * Microschip plib CAN library.
 *
 * @author Balazs Racz
 * @date 12 Aug 2013
 */

#include "can.h"
#include <fcntl.h>

#include "GenericTypeDefs.h"
#include <xc.h>
#include "peripheral/CAN.h"
#include "peripheral/int.h"

#define QUEUE_LEN 16

#define CAN_BUS_SPEED 250000

/** Private data for this implementation of CAN
 */
typedef struct pic32mx_can_priv
{
    CAN_MODULE hw;
    node_t node;
    int overrunCount;
    uint8_t MessageFifoArea[2 * QUEUE_LEN * 16];
    os_sem_t tx_sem;
    os_sem_t rx_sem;
} Pic32mxCanPriv;

/** private data for the can device */
static Pic32mxCanPriv can_private[2] =
{
    {
        .hw = CAN1,
        .overrunCount = 0
    },
    {
        .hw = CAN2,
        .overrunCount = 0
    }
};

static int pic32mx_can_open(file_t* file, const char *path, int flags, int mode);
static int pic32mx_can_close(file_t *file, node_t *node);
static ssize_t pic32mx_can_read(file_t *file, void *buf, size_t count);
static ssize_t pic32mx_can_write(file_t *file, const void *buf, size_t count);
static int pic32mx_can_ioctl(file_t *file, node_t *node, int key, void *data);

DEVOPS(pic32mx_can_ops, pic32mx_can_open, pic32mx_can_close, pic32mx_can_read, pic32mx_can_write, pic32mx_can_ioctl);

int pic32mx_can_ioctl(file_t *file, node_t *node, int key, void *data) {
    return 0;
}

static int pic32mx_can_init(devtab_t *dev) {
    // This is called before appl_main is started, so there are no threads here
    // yet.
    Pic32mxCanPriv *priv = dev->priv;
    priv->node.references = 0;
    priv->node.priv = priv;
    os_sem_init(&priv->tx_sem, 0);
    os_sem_init(&priv->rx_sem, 0);
    return 0;
}


static void pic32mx_can_enable(Pic32mxCanPriv *priv);
static void pic32mx_can_disable(Pic32mxCanPriv *priv);


static int pic32mx_can_open(file_t* file, const char *path, int flags, int mode)
{
    Pic32mxCanPriv *priv = file->dev->priv;

    file->node = &priv->node;
    file->offset = 0;
    taskENTER_CRITICAL();
    if (priv->node.references++ == 0)
    {
        pic32mx_can_enable(priv);
    }
    taskEXIT_CRITICAL();
    return 0;
}

static int pic32mx_can_close(file_t* file, node_t *node)
{
    Pic32mxCanPriv *priv = file->dev->priv;

    taskENTER_CRITICAL();
    if (--node->references == 0)
    {
        pic32mx_can_disable(priv);
    }
    taskEXIT_CRITICAL();
    return 0;
}

static void pic_buffer_to_frame(const CANRxMessageBuffer* message,
                                struct can_frame* can_frame)
{
    uint32_t id = message->msgSID.SID;
    if (message->msgEID.IDE) {
        SET_CAN_FRAME_EFF(*can_frame);
        id <<= 18;
        id |= message->msgEID.EID;
        SET_CAN_FRAME_ID_EFF(*can_frame, id);
    } else {
        CLR_CAN_FRAME_EFF(*can_frame);
        SET_CAN_FRAME_ID(*can_frame, id);
    }
    if (message->msgEID.RTR) {
        SET_CAN_FRAME_RTR(*can_frame);
    } else {
        CLR_CAN_FRAME_RTR(*can_frame);
    }
    CLR_CAN_FRAME_ERR(*can_frame);
            
    can_frame->can_dlc = message->msgEID.DLC;
    memcpy(can_frame->data, message->data, can_frame->can_dlc);
}

static void frame_to_pic_buffer(const struct can_frame* can_frame,
                                CANTxMessageBuffer* message)
{
    message->messageWord[0] = 0;
    message->messageWord[1] = 0;
    if (IS_CAN_FRAME_EFF(*can_frame)) {
        uint32_t id = GET_CAN_FRAME_ID_EFF(*can_frame);
        message->msgEID.IDE = 1;
        message->msgSID.SID = id >> 18;
        message->msgEID.EID = id & ((1<<19) - 1);

        //message->msgSID.SID = id & 0x7ff;
        //message->msgEID.EID = id >> 11;
    } else {
        message->msgSID.SID = GET_CAN_FRAME_ID(*can_frame);
        message->msgEID.IDE = 0;
    }
    if (IS_CAN_FRAME_RTR(*can_frame)) {
        message->msgEID.RTR = 1;
    } else {
        message->msgEID.RTR = 0;
    }
    message->msgEID.DLC = can_frame->can_dlc;
    memcpy(message->data, can_frame->data, can_frame->can_dlc);
}

static ssize_t pic32mx_can_read(file_t *file, void *buf, size_t count)
{
    Pic32mxCanPriv *priv = file->dev->priv;
    struct can_frame *can_frame = buf;
    ssize_t result = 0;

    int flags = -1;

    while (count >= sizeof(struct can_frame))
    {
        /*
           At the beginning of the iteration we are in a critical section.

           We need this critical section because the GetRxBuffer call will
           return the same buffer until the Update call is successful. We want
           to prevent multiple threads from receiving the same CAN frame
           however. */

        taskENTER_CRITICAL();
	CANRxMessageBuffer *message =
            (CANRxMessageBuffer *)CANGetRxMessage(priv->hw, CAN_CHANNEL1);
        if (message != NULL) {
            CANUpdateChannel(priv->hw, CAN_CHANNEL1);
        }
        if (flags == -1) {
            flags = file->flags;
        }
        taskEXIT_CRITICAL();

        /* Now let's take a look if we have actually found a message. */
        if (message != NULL) {
            pic_buffer_to_frame(message, can_frame);

            count -= sizeof(struct can_frame);
            result += sizeof(struct can_frame);
            can_frame++;
            continue;
        }
        
        /* We do not have a message. Return a short read or zero if we have a
         * non-blocking filedes. */

        if (result || (flags & O_NONBLOCK)) {
            break;
        }

        /* Blocking read. Enable the receive interrupt and pend on the rx
           semaphore. Spurious interrupts and extra tokens in the semaphore are
           not a problem, they will just drop back here with no messages
           found. 

           There is no race condition between checking the queue above and
           enabling the interrupt here. The interrupt pending flag is
           persistent, thus if there is already a message in the queue it will
           trigger the interrupt immediately.
        */

	CANEnableChannelEvent(priv->hw, CAN_CHANNEL1, CAN_RX_CHANNEL_NOT_EMPTY, TRUE);
        os_sem_wait(&priv->rx_sem);
    } 

    /* As a good-bye we wake up the interrupt handler once more to post on the
     * semaphore in case there is another thread waiting. */
    CANEnableChannelEvent(priv->hw, CAN_CHANNEL1, CAN_RX_CHANNEL_NOT_EMPTY, TRUE);

    return result;
}

static ssize_t pic32mx_can_write(file_t *file, const void *buf, size_t count)
{
    Pic32mxCanPriv *priv = file->dev->priv;
    const struct can_frame *can_frame = buf;
    ssize_t result = 0;

    int flags = -1;

    while (count >= sizeof(struct can_frame))
    {
        taskENTER_CRITICAL();
	CANTxMessageBuffer *message = CANGetTxMessageBuffer(priv->hw, CAN_CHANNEL0);
        if (message != NULL) {
            /* Unfortunately we have to fill the buffer in the critical section
             * or else we risk that another thread will call the FlushTxChannel
             * while our buffer is not fully completed. */
            frame_to_pic_buffer(can_frame, message);
            CANUpdateChannel(priv->hw, CAN_CHANNEL0);
        }
        if (flags == -1) {
            flags = file->flags;
        }
        taskEXIT_CRITICAL();

        /* Did we actually find a slot to transmit? */
        if (message != NULL) {
            CANFlushTxChannel(priv->hw, CAN_CHANNEL0);

            count -= sizeof(struct can_frame);
            result += sizeof(struct can_frame);
            can_frame++;
            continue;
        }

        /* We did not find a transmit slot. We purposefully do not execute a
         * short write here, although that would be an option. */
        if (flags & O_NONBLOCK) {
            break;
        }
        /* Blocking read. We enable the interrupt and wait for the
         * semaphore. There is no race condition here, because the TX buffer
         * not full interrupt is persistent, so if a buffer got free between
         * our check and now, the interrupt will trigger immediately and wake
         * us up. */
	CANEnableChannelEvent(priv->hw, CAN_CHANNEL0, CAN_TX_CHANNEL_NOT_FULL, TRUE);
        os_sem_wait(&priv->tx_sem);
    }
    
    /* As a good-bye we wake up the interrupt handler once more to post on the
     * semaphore in case there is another thread waiting. */
    CANEnableChannelEvent(priv->hw, CAN_CHANNEL0, CAN_TX_CHANNEL_NOT_FULL, TRUE);

    return result;
}


static void pic32mx_can_enable(Pic32mxCanPriv *priv) {
    CANEnableModule(priv->hw, TRUE);
    /* Step 1: Switch the CAN module
     * ON and switch it to Configuration
     * mode. Wait till the mode switch is
     * complete. */
    CANSetOperatingMode(priv->hw, CAN_CONFIGURATION);
    while(CANGetOperatingMode(priv->hw) != CAN_CONFIGURATION);			

    /* Step 2: Configure the Clock.The
     * CAN_BIT_CONFIG data structure is used
     * for this purpose. The propagation segment, 
     * phase segment 1 and phase segment 2
     * are configured to have 3TQ. SYSTEM_FREQ
     * and CAN_BUS_SPEED are defined in  */
	
    CAN_BIT_CONFIG canBitConfig;
    canBitConfig.phaseSeg2Tq            = CAN_BIT_3TQ;
    canBitConfig.phaseSeg1Tq            = CAN_BIT_3TQ;
    canBitConfig.propagationSegTq       = CAN_BIT_3TQ;
    canBitConfig.phaseSeg2TimeSelect    = TRUE;
    canBitConfig.sample3Time            = TRUE;
    canBitConfig.syncJumpWidth          = CAN_BIT_2TQ;

    CANSetSpeed(priv->hw, &canBitConfig, configCPU_CLOCK_HZ, CAN_BUS_SPEED);

    /* Step 3: Assign the buffer area to the
     * CAN module.
     */ 

    CANAssignMemoryBuffer(priv->hw,priv->MessageFifoArea,(2 * 8 * 16));	

    /* Step 4: Configure channel 0 for TX and size of
     * QUEUE_LEN message buffers with RTR disabled and low medium
     * priority. Configure channel 1 for RX and size
     * of QUEUE_LEN message buffers and receive the full message.
     */

    CANConfigureChannelForTx(priv->hw, CAN_CHANNEL0, QUEUE_LEN, CAN_TX_RTR_DISABLED, CAN_LOW_MEDIUM_PRIORITY);
    CANConfigureChannelForRx(priv->hw, CAN_CHANNEL1, QUEUE_LEN, CAN_RX_FULL_RECEIVE);
    CANEnableModuleEvent (priv->hw, CAN_RX_EVENT, TRUE);
    CANEnableModuleEvent (priv->hw, CAN_TX_EVENT, TRUE);
	
    /* Step 6: Enable interrupt and events. 
     * The interrrupt peripheral library is used to enable
     * the CAN interrupt to the CPU. */

    if (priv->hw == CAN1) {
        INTSetVectorPriority(INT_CAN_1_VECTOR, INT_PRIORITY_LEVEL_4);
        INTSetVectorSubPriority(INT_CAN_1_VECTOR, INT_SUB_PRIORITY_LEVEL_0);
        INTEnable(INT_CAN1, INT_ENABLED);
    } else {
        INTSetVectorPriority(INT_CAN_2_VECTOR, INT_PRIORITY_LEVEL_4);
        INTSetVectorSubPriority(INT_CAN_2_VECTOR, INT_SUB_PRIORITY_LEVEL_0);
        INTEnable(INT_CAN2, INT_ENABLED);
    }

    /* Step 7: Switch the CAN mode
     * to normal mode. */

    CANSetOperatingMode(priv->hw, CAN_NORMAL_OPERATION);
    while(CANGetOperatingMode(priv->hw) != CAN_NORMAL_OPERATION);	
}

static void pic32mx_can_disable(Pic32mxCanPriv *priv) {
    /* If the transmit buffer is not empty, we should crash here. Otherweise it
     * is possible that the user sends some frames, then closes the device and
     * the frames never get sent really. */
    if (priv->hw == CAN1) {
        INTEnable(INT_CAN1, INT_DISABLED);
    } else {
        INTEnable(INT_CAN2, INT_DISABLED);
    }
    CANEnableModule(priv->hw,FALSE);
}



DEVTAB_ENTRY(can0, "/dev/can0", pic32mx_can_init, &pic32mx_can_ops, &can_private[0]);
DEVTAB_ENTRY(can1, "/dev/can1", pic32mx_can_init, &pic32mx_can_ops, &can_private[1]);


static void IRQHandler(Pic32mxCanPriv* priv) {
    if(CANGetPendingEventCode(priv->hw) == CAN_CHANNEL1_EVENT)
    {
        /* This means that channel 1 caused the event.
         * The CAN_RX_CHANNEL_NOT_EMPTY event is persistent. You
         * could either read the channel in the ISR
         * to clear the event condition or as done 
         * here, disable the event source, and set
         * an application flag to indicate that a message
         * has been received. The event can be
         * enabled by the application when it has processed
         * one message.
         *
         * Note that leaving the event enabled would
         * cause the CPU to keep executing the ISR since
         * the CAN_RX_CHANNEL_NOT_EMPTY event is persistent (unless
         * the not empty condition is cleared.) 
         * */
        CANEnableChannelEvent(priv->hw, CAN_CHANNEL1, CAN_RX_CHANNEL_NOT_EMPTY, FALSE);
        os_sem_post_from_isr(&priv->rx_sem);
    }
    if(CANGetPendingEventCode(priv->hw) == CAN_CHANNEL0_EVENT)
    {
        /* Same with the TX event. */
	CANEnableChannelEvent(priv->hw, CAN_CHANNEL0, CAN_TX_CHANNEL_NOT_FULL, FALSE);
        os_sem_post_from_isr(&priv->tx_sem);
    }   
}


void __attribute__((interrupt)) can1_interrupt(void) {
    IRQHandler(&can_private[0]);
    INTClearFlag(INT_CAN1);
}

void __attribute__((interrupt)) can2_interrupt(void) {
    IRQHandler(&can_private[1]);
    INTClearFlag(INT_CAN2);
}

//void __attribute__((section(".vector_46"))) can1_int_trampoline(void) 
//    asm("b   can1_interrupt");


asm("\n\t.section .vector_46,\"ax\",%progbits\n\tj can1_interrupt\n\tnop\n.text\n");

asm("\n\t.section .vector_47,\"ax\",%progbits\n\tj can2_interrupt\n\tnop\n.text\n");



// TODO: process receive buffer overflow flags.
