#include "utils/async_if_test_helper.hxx"

#include "nmranet/NMRAnetAsyncDatagram.hxx"
#include "nmranet/NMRAnetAsyncDatagramCan.hxx"
#include "nmranet/NMRAnetAsyncDatagramDefaultHandler.hxx"

namespace NMRAnet
{

class AsyncDatagramTest : public AsyncNodeTest
{
protected:
    AsyncDatagramTest() : datagram_support_(if_can_.get(), 10, 2)
    {
    }

    CanDatagramSupport datagram_support_;
};

class AsyncRawDatagramTest : public AsyncNodeTest
{
protected:
    AsyncRawDatagramTest()
    {
        if_can_->dispatcher()->RegisterHandler(0x1C48, 0xFFFF, &handler_);
        EXPECT_CALL(handler_, get_allocator()).WillRepeatedly(Return(nullptr));
        ON_CALL(handler_, handle_message(_, _))
            .WillByDefault(WithArg<1>(Invoke(&InvokeNotification)));
        if_can_->add_owned_flow(TEST_CreateCanDatagramParser(if_can_.get()));
    }
    ~AsyncRawDatagramTest()
    {
        Wait();
        if_can_->dispatcher()->UnregisterHandler(0x1C48, 0xFFFF, &handler_);
    }

    StrictMock<MockMessageHandler> handler_;
};

TEST_F(AsyncRawDatagramTest, CreateDestroy)
{
}

TEST_F(AsyncRawDatagramTest, SingleFrameDatagramArrivesWrongTarget)
{
    SendPacket(":X1A333555NFF01020304050607;");
}

TEST_F(AsyncRawDatagramTest, MultiFrameDatagramArrivesWrongTarget)
{
    SendPacket(":X1B333555NFF01020304050607;");
    SendPacket(":X1C333555NFF01020304050607;");
    SendPacket(":X1C333555NFF01020304050607;");
    SendPacket(":X1D333555NFF01020304050607;");
}

TEST_F(AsyncRawDatagramTest, SingleFrameDatagramArrivesRightTarget)
{
    EXPECT_CALL(
        handler_,
        handle_message(
            Pointee(AllOf(Field(&IncomingMessage::mti, If::MTI_DATAGRAM),
                          Field(&IncomingMessage::dst_node, node_),
                          Field(&IncomingMessage::payload, NotNull()),
                          Field(&IncomingMessage::payload,
                                IsBufferValue(0xFF01020304050607ULL)) //,
                          )),
            _)).WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    SendPacket(":X1A22A555NFF01020304050607;");
}

TEST_F(AsyncRawDatagramTest, MultiFrameDatagramArrivesRightTarget)
{
    EXPECT_CALL(
        handler_,
        handle_message(
            Pointee(AllOf(Field(&IncomingMessage::mti, If::MTI_DATAGRAM),
                          Field(&IncomingMessage::dst_node, node_),
                          Field(&IncomingMessage::payload, NotNull()),
                          Field(&IncomingMessage::payload,
                                IsBufferValueString(
                                    "01234567112345672123456731234567")) //,
                          )),
            _)).WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    SendPacket(":X1B22A555N3031323334353637;");
    SendPacket(":X1C22A555N3131323334353637;");
    SendPacket(":X1C22A555N3231323334353637;");
    SendPacket(":X1D22A555N3331323334353637;");
}

TEST_F(AsyncRawDatagramTest, OutOfOrderRestart)
{
    SendPacket(":X1B22A555N3031323334353637;");
    SendPacket(":X1C22A555N3131323334353637;");
    SendPacket(":X1C22A555N3231323334353637;");

    // Another start packet -> rejection.
    SendPacketAndExpectResponse(":X1B22A555N3031323334353637;",
                                ":X19A4822AN05552040;");

    // Now the finish packet will die as well.
    SendPacketAndExpectResponse(":X1D22A555N3331323334353637;",
                                ":X19A4822AN05552040;");
}

TEST_F(AsyncRawDatagramTest, MultiFrameDatagramThenStartMiddle)
{
    EXPECT_CALL(
        handler_,
        handle_message(
            Pointee(AllOf(Field(&IncomingMessage::mti, If::MTI_DATAGRAM),
                          Field(&IncomingMessage::dst_node, node_),
                          Field(&IncomingMessage::payload, NotNull()),
                          Field(&IncomingMessage::payload,
                                IsBufferValueString(
                                    "01234567112345672123456731234567")) //,
                          )),
            _)).WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    SendPacket(":X1B22A555N3031323334353637;");
    SendPacket(":X1C22A555N3131323334353637;");
    SendPacket(":X1C22A555N3231323334353637;");
    SendPacket(":X1D22A555N3331323334353637;");
    // Datagram should be complete here.

    // A finish packet out of the blue.
    SendPacketAndExpectResponse(":X1D22A555N3331323334353637;",
                                ":X19A4822AN05552040;");

    // A middle packet out of the blue.
    SendPacketAndExpectResponse(":X1C22A555N3331323334353637;",
                                ":X19A4822AN05552040;");
}

::std::ostream& operator<<(::std::ostream& o, const Buffer* b)
{
    o << "Buffer(";
    if (!b)
    {
        o << "NULL)";
    }
    else
    {
        o << "used " << b->used() << " data ";
        const uint8_t* bytes = static_cast<const uint8_t*>(b->start());
        for (unsigned i = 0; i < b->used(); ++i)
        {
            o << StringPrintf("%02x ", bytes[i]);
        }
        o << ")";
    }
    return o;
}

::std::ostream& operator<<(::std::ostream& o, const NodeHandle& h)
{
    o << StringPrintf("Handle(%012llx, %03x)", h.id, h.alias);
    return o;
}

::std::ostream& operator<<(::std::ostream& o, const IncomingMessage& m)
{
    o << "an IncomingMessage"
      << " of MTI " << StringPrintf("%04x", m.mti) << " from " << m.src
      << " to " << m.dst << " to node " << m.dst_node << " with payload "
      << m.payload;
    return o;
}

TEST_F(AsyncRawDatagramTest, MaxSizeDatagram)
{
    SendPacket(":X1B22A555N3031323334353637;"); // 8
    for (int i = 0; i < 7; i++)
    {                                                                     // +7*
        SendPacket(StringPrintf(":X1C22A555N3%d31323334353637;", i + 1)); // 8
    }
    EXPECT_CALL(
        handler_,
        handle_message(
            Pointee(AllOf(Field(&IncomingMessage::mti, If::MTI_DATAGRAM),
                          Field(&IncomingMessage::dst_node, node_),
                          Field(&IncomingMessage::payload, NotNull()),
                          Field(&IncomingMessage::payload,
                                IsBufferValueString("01234567112345672123456731"
                                                    "23456741234567512345676123"
                                                    "45677123456781234567")) //,
                          )),
            _)).WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    SendPacket(":X1D22A555N3831323334353637;"); // 8
}

TEST_F(AsyncRawDatagramTest, TooLongDatagram)
{
    SendPacket(":X1B22A555N3031323334353637;"); // 8
    for (int i = 0; i < 8; i++)
    {                                                                     // +8*
        SendPacket(StringPrintf(":X1C22A555N3%d31323334353637;", i + 1)); // 8
    }
    SendPacketAndExpectResponse(
        ":X1C22A555N3031323334353637;",
        ":X19A4822AN05551000;"); // Datagram rejected permanent error
}

TEST_F(AsyncRawDatagramTest, MultiFrameDatagramArrivesInterleavedSingle)
{
    EXPECT_CALL(
        handler_,
        handle_message(
            Pointee(AllOf(Field(&IncomingMessage::mti, If::MTI_DATAGRAM),
                          Field(&IncomingMessage::dst_node, node_),
                          Field(&IncomingMessage::payload, NotNull()),
                          Field(&IncomingMessage::payload,
                                IsBufferValueString(
                                    "01234567112345672123456731234567")) //,
                          )),
            _)).WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    EXPECT_CALL(
        handler_,
        handle_message(
            Pointee(AllOf(Field(&IncomingMessage::mti, If::MTI_DATAGRAM),
                          Field(&IncomingMessage::dst_node, node_),
                          Field(&IncomingMessage::payload, NotNull()),
                          Field(&IncomingMessage::payload,
                                IsBufferValueString("01234")) //,
                          )),
            _)).WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    SendPacket(":X1B22A555N3031323334353637;");
    SendPacket(":X1C22A555N3131323334353637;");
    SendPacket(":X1A22A555N3031323334;"); // A single-frame datagram here.
    SendPacket(":X1C22A555N3231323334353637;");
    SendPacket(":X1D22A555N3331323334353637;");
}

TEST_F(AsyncRawDatagramTest, MultiFrameIntermixed)
{
    SendPacket(":X1B22A555N3031323334353637;");
    SendPacket(":X1C22A555N3131323334353637;");
    SendPacket(":X1C22A555N3231323334353637;");
    EXPECT_CALL(
        handler_,
        handle_message(
            Pointee(AllOf(
                Field(&IncomingMessage::mti, If::MTI_DATAGRAM),
                Field(&IncomingMessage::dst_node, node_),
                Field(&IncomingMessage::src, Field(&NodeHandle::alias, 0x577)),
                Field(&IncomingMessage::payload, NotNull()),
                Field(&IncomingMessage::payload,
                      IsBufferValueString("0123456711234567")) //,
                )),
            _)).WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    SendPacket(":X1B22A577N3031323334353637;");
    EXPECT_CALL(
        handler_,
        handle_message(
            Pointee(AllOf(
                Field(&IncomingMessage::mti, If::MTI_DATAGRAM),
                Field(&IncomingMessage::dst_node, node_),
                Field(&IncomingMessage::src, Field(&NodeHandle::alias, 0x555)),
                Field(&IncomingMessage::payload, NotNull()),
                Field(
                    &IncomingMessage::payload,
                    IsBufferValueString("01234567112345672123456731234567")) //,
                )),
            _)).WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    SendPacket(":X1D22A555N3331323334353637;");
    SendPacket(":X1D22A577N3131323334353637;");
}

TEST_F(AsyncRawDatagramTest, MultiFrameIntermixedDst)
{
    EXPECT_CALL(can_bus_, MWrite(":X1910022BN02010D000004;")).Times(1);
    if_can_->local_aliases()->add(TEST_NODE_ID + 1, 0x22B);
    DefaultAsyncNode other_node(if_can_.get(), TEST_NODE_ID + 1);

    SendPacket(":X1B22A555N3031323334353637;");
    SendPacket(":X1C22A555N3131323334353637;");
    SendPacket(":X1C22A555N3231323334353637;");
    EXPECT_CALL(
        handler_,
        handle_message(
            Pointee(AllOf(
                Field(&IncomingMessage::mti, If::MTI_DATAGRAM),
                Field(&IncomingMessage::dst_node, &other_node),
                Field(&IncomingMessage::src, Field(&NodeHandle::alias, 0x555)),
                Field(&IncomingMessage::payload, NotNull()),
                Field(&IncomingMessage::payload,
                      IsBufferValueString("0123456711234567")) //,
                )),
            _)).WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    SendPacket(":X1B22B555N3031323334353637;");
    EXPECT_CALL(
        handler_,
        handle_message(
            Pointee(AllOf(
                Field(&IncomingMessage::mti, If::MTI_DATAGRAM),
                Field(&IncomingMessage::dst_node, node_),
                Field(&IncomingMessage::src, Field(&NodeHandle::alias, 0x555)),
                Field(&IncomingMessage::payload, NotNull()),
                Field(
                    &IncomingMessage::payload,
                    IsBufferValueString("01234567112345672123456731234567")) //,
                )),
            _)).WillOnce(WithArg<1>(Invoke(&InvokeNotification)));
    SendPacket(":X1D22A555N3331323334353637;");
    SendPacket(":X1D22B555N3131323334353637;");
    Wait();
}

class MockDatagramHandlerBase : public DatagramHandler, public ControlFlow
{
public:
    MockDatagramHandlerBase() : ControlFlow(&g_executor, nullptr)
    {
        StartFlowAt(ST(wait_for_datagram));
    }

    virtual void handle_datagram(IncomingDatagram* d) = 0;

    ControlFlowAction wait_for_datagram()
    {
        return Allocate(&queue_, ST(process_datagram));
    }

    ControlFlowAction process_datagram()
    {
        IncomingDatagram* d = GetTypedAllocationResult(&queue_);
        handle_datagram(d);
        d->free();
        return CallImmediately(ST(wait_for_datagram));
    }
};

class MockDatagramHandler : public MockDatagramHandlerBase
{
public:
    MOCK_METHOD1(handle_datagram, void(IncomingDatagram* d));
};

TEST_F(AsyncDatagramTest, DispatchTest)
{
    StrictMock<MockDatagramHandler> dg;
    datagram_support_.registry()->insert(nullptr, 0x30, &dg);
    EXPECT_CALL(
        dg, handle_datagram(Pointee(AllOf(
                Field(&IncomingDatagram::src, Field(&NodeHandle::alias, 0x555)),
                Field(&IncomingDatagram::dst, node_),
                Field(&IncomingDatagram::payload, NotNull()),
                Field(&IncomingDatagram::payload,
                      IsBufferValueString("01234567")) //,
                ))));
    SendPacket(":X1A22A555N3031323334353637;");
    Wait();
    usleep(3000);
}

InitializedAllocator<IncomingDatagram> g_incoming_datagram_allocator(10);

Buffer* string_to_buffer(const string& value)
{
    Buffer* b = buffer_alloc(value.size());
    memcpy(b->start(), value.data(), value.size());
    b->advance(value.size());
    return b;
}

TEST_F(AsyncDatagramTest, OutgoingTestSmall)
{
    PrintAllPackets();
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    Wait();
    SendPacket(":X19A2877CN022A00;"); // Received OK
    n.WaitForNotification();
}

TEST_F(AsyncDatagramTest, OutgoingTestOneFull)
{
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN3031323334353637;");
    a.result()->write_datagram(node_->node_id(), h,
                               string_to_buffer("01234567"), &n);
    Wait();
    SendPacket(":X19A2877CN022A00;"); // Received OK
    n.WaitForNotification();
}

TEST_F(AsyncDatagramTest, OutgoingTestBeginEnd)
{
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1B77C22AN3031323334353637;");
    ExpectPacket(":X1D77C22AN3839303132333435;");
    a.result()->write_datagram(node_->node_id(), h,
                               string_to_buffer("0123456789012345"), &n);
    Wait();
    SendPacket(":X19A2877CN022A00;"); // Received OK
    n.WaitForNotification();
}

TEST_F(AsyncDatagramTest, OutgoingTestMiddle)
{
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1B77C22AN3031323334353637;");
    ExpectPacket(":X1C77C22AN3839303132333435;");
    ExpectPacket(":X1D77C22AN3031323334353637;");
    a.result()->write_datagram(
        node_->node_id(), h, string_to_buffer("012345678901234501234567"), &n);
    Wait();
    SendPacket(":X19A2877CN022A00;"); // Received OK
    n.WaitForNotification();
}

TEST_F(AsyncDatagramTest, ResponseOK)
{
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    SendPacket(":X19A2877CN022A00;"); // Received OK
    n.WaitForNotification();
    EXPECT_EQ((unsigned)DatagramClient::OPERATION_SUCCESS,
              a.result()->result());
}

TEST_F(AsyncDatagramTest, SendByAddressCacheHit)
{
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{TEST_NODE_ID + 3, 0};
    if_can_->remote_aliases()->add(h.id, 0x77C);
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    SendPacket(":X19A2877CN022A00;"); // Received OK
    n.WaitForNotification();
    EXPECT_EQ((unsigned)DatagramClient::OPERATION_SUCCESS,
              a.result()->result());
}

TEST_F(AsyncDatagramTest, SendByAddressCacheMiss)
{
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0x050101FFFFDDULL, 0};
    ExpectPacket(":X1070222AN050101FFFFDD;"); // AME frame
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    SendPacketAndExpectResponse(":X10701210N050101FFFFDD;", // AMD frame
                                ":X1A21022AN30313233343536;");
    SendPacket(":X19A28210N022A00;"); // Received OK
    n.WaitForNotification();
    EXPECT_EQ((unsigned)DatagramClient::OPERATION_SUCCESS,
              a.result()->result());
    // Checks that the new lookup value got into the cache.
    EXPECT_EQ(0x210U, if_can_->remote_aliases()->lookup(h.id));
}

TEST_F(AsyncDatagramTest, ResponseOKWithCode)
{
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    SendPacket(":X19A2877CN022AA5;"); // Received OK
    n.WaitForNotification();
    EXPECT_EQ((unsigned)DatagramClient::OPERATION_SUCCESS |
                  (0xA5 << DatagramClient::RESPONSE_FLAGS_SHIFT),
              a.result()->result());
}

TEST_F(AsyncDatagramTest, ResponseOKPendingReply)
{
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    SendPacket(":X19A2877CN022A80;"); // Received OK
    n.WaitForNotification();
    EXPECT_TRUE(a.result()->result() & (DatagramClient::OK_REPLY_PENDING));
}

TEST_F(AsyncDatagramTest, Rejected)
{
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    SendPacket(":X19A4877CN022A55AA;"); // Datagram rejected.
    n.WaitForNotification();
    EXPECT_EQ(0x55AAU, a.result()->result());
}

TEST_F(AsyncDatagramTest, Timeout)
{
    ScopedOverride ov(&DATAGRAM_RESPONSE_TIMEOUT_NSEC, MSEC_TO_NSEC(20));
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    n.WaitForNotification();
    EXPECT_EQ(
        (unsigned)(DatagramClient::TIMEOUT | DatagramClient::PERMANENT_ERROR),
        a.result()->result());
}

TEST_F(AsyncDatagramTest, RejectedNoData)
{
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    SendPacket(":X19A4877CN022A;"); // Datagram rejected.
    n.WaitForNotification();
    EXPECT_TRUE(DatagramClient::PERMANENT_ERROR & a.result()->result());
}

TEST_F(AsyncDatagramTest, OptionalInteractionRejectedNoPayload)
{
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    SendPacket(":X1906877CN022A5A;"); // OIR, payload invalid
    n.WaitForNotification();
    EXPECT_EQ((unsigned)DatagramClient::PERMANENT_ERROR, a.result()->result());
}

TEST_F(AsyncDatagramTest, OptionalInteractionRejectedWrongMTI)
{
    ScopedOverride ov(&DATAGRAM_RESPONSE_TIMEOUT_NSEC, MSEC_TO_NSEC(20));
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    SendPacket(":X1906877CN022A55AA0991;"); // OIR, payload with a different MTI
    n.WaitForNotification();
    // Timeout means the OIR was ignored.
    EXPECT_EQ(
        (unsigned)(DatagramClient::TIMEOUT | DatagramClient::PERMANENT_ERROR),
        a.result()->result());
}

TEST_F(AsyncDatagramTest, OptionalInteractionRejectedCorrectMTI)
{
    ScopedOverride ov(&DATAGRAM_RESPONSE_TIMEOUT_NSEC, MSEC_TO_NSEC(20));
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    SendPacket(":X1906877CN022A55AA1C48;"); // OIR, payload with correct MTI
    n.WaitForNotification();
    EXPECT_EQ(0x55AAU, a.result()->result());
}

TEST_F(AsyncDatagramTest, OptionalInteractionRejectedMustHaveError)
{
    ScopedOverride ov(&DATAGRAM_RESPONSE_TIMEOUT_NSEC, MSEC_TO_NSEC(20));
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    SendPacket(":X1906877CN022A00AA1C48;"); // OIR, payload with correct MTI
    n.WaitForNotification();
    EXPECT_EQ(0x10AAU, a.result()->result()); // Added PERMANENT_ERROR
}

TEST_F(AsyncDatagramTest, OptionalInteractionRejectedTemporaryError)
{
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    SendPacket(":X1906877CN022A2000;"); // OIR, temporary error
    n.WaitForNotification();
    EXPECT_EQ((unsigned)DatagramClient::RESEND_OK, a.result()->result());
}

TEST_F(AsyncDatagramTest, TerminateDueToErrorNoPayload)
{
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    SendPacket(":X190A877CN022A5A;"); // TDE, payload invalid
    n.WaitForNotification();
    EXPECT_EQ((unsigned)DatagramClient::PERMANENT_ERROR, a.result()->result());
}

TEST_F(AsyncDatagramTest, TerminateDueToErrorWrongMTI)
{
    ScopedOverride ov(&DATAGRAM_RESPONSE_TIMEOUT_NSEC, MSEC_TO_NSEC(20));
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    SendPacket(":X190A877CN022A55AA0991;"); // TDE, payload with a different MTI
    n.WaitForNotification();
    // Timeout means the TDE was ignored.
    EXPECT_EQ(
        (unsigned)(DatagramClient::TIMEOUT | DatagramClient::PERMANENT_ERROR),
        a.result()->result());
}

TEST_F(AsyncDatagramTest, TerminateDueToErrorCorrectMTI)
{
    ScopedOverride ov(&DATAGRAM_RESPONSE_TIMEOUT_NSEC, MSEC_TO_NSEC(20));
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    SendPacket(":X190A877CN022A55AA1C48;"); // TDE, payload with correct MTI
    n.WaitForNotification();
    EXPECT_EQ(0x55AAU, a.result()->result());
}

TEST_F(AsyncDatagramTest, TerminateDueToErrorMustHaveError)
{
    ScopedOverride ov(&DATAGRAM_RESPONSE_TIMEOUT_NSEC, MSEC_TO_NSEC(20));
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    SendPacket(":X190A877CN022A00AA1C48;"); // TDE, payload with correct MTI
    n.WaitForNotification();
    EXPECT_EQ(0x10AAU, a.result()->result());
}

TEST_F(AsyncDatagramTest, TerminateDueToErrorTemporaryError)
{
    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{0, 0x77C};
    ExpectPacket(":X1A77C22AN30313233343536;");
    a.result()->write_datagram(node_->node_id(), h, string_to_buffer("0123456"),
                               &n);
    Wait();
    SendPacket(":X190A877CN022A2000;"); // TDE, temporary error
    n.WaitForNotification();
    EXPECT_EQ((unsigned)DatagramClient::RESEND_OK, a.result()->result());
}

/** Ping-pong is a fake datagram-based service. When it receives a datagram
 * from a particular node, it sends back the datagram to the originating node
 * with a slight difference: a TTL being decremented and the payload being
 * shortened. Two ping-pong datagram handlers can therefore converse with each
 * other after the injection of a single message.
 *
 * Datagram format: id=0x7A, second byte = length, then length number of bytes.
 *
 * The response will be in the same format, with length decreased by one, and
 * the first length - 1 bytes of the incoming payload. A datagram with payload
 * length 0 will not receive a response.
 */
class PingPongHandler : public DefaultDatagramHandler
{
public:
    enum
    {
        DATAGRAM_ID = 0x7A,
    };

    PingPongHandler(DatagramSupport* if_dg, AsyncNode* node)
        : DefaultDatagramHandler(if_dg), processCount_(0)
    {
        ifDatagram_->registry()->insert(node, DATAGRAM_ID, this);
    }

    ~PingPongHandler()
    {
        /// @TODO(balazs.racz) Remove handler entry from the registry. It would
        /// be important to remember the node for that, and need a remove API
        /// on the NodeHandlerMap.
    }

    /// Returns how many datagrams this handler has seen so far.
    int process_count()
    {
        return processCount_;
    }

    virtual ControlFlowAction datagram_arrived()
    {
        processCount_++;
        const uint8_t* bytes =
            static_cast<const uint8_t*>(datagram_->payload->start());
        size_t len = datagram_->payload->used();
        HASSERT(len >= 1);
        HASSERT(bytes[0] == DATAGRAM_ID);
        if (len < 2)
        {
            return respond_reject(DatagramClient::PERMANENT_ERROR);
        }
        if (bytes[1] > 0)
        {
            return respond_ok(DatagramClient::REPLY_PENDING);
        }
        else
        {
            return respond_ok(0);
        }
    }

    virtual ControlFlowAction ok_response_sent()
    {
        uint8_t* bytes = static_cast<uint8_t*>(datagram_->payload->start());
        size_t len = datagram_->payload->used();
        if (!bytes[1])
        {
            datagram_->free();
            // No response.
            return CallImmediately(ST(wait_for_datagram));
        }

        // We take over the buffer ownership.
        Buffer* payload = datagram_->payload;
        datagram_->payload = nullptr;
        datagram_->free();

        // The response datagram should be the same id, one less bytes and one
        // less in the second byte.
        payload->zero();
        payload->advance(len - 1);
        bytes[1]--;
        responsePayload_ = payload;

        return Allocate(ifDatagram_->client_allocator(),
                        ST(send_response_datagram));
    }

    ControlFlowAction send_response_datagram()
    {
        auto* client_flow =
            GetTypedAllocationResult(ifDatagram_->client_allocator());
        client_flow->write_datagram(datagram_->dst->node_id(), datagram_->src,
                                    responsePayload_, this);
        return WaitAndCall(ST(wait_response_datagram));
    }

    ControlFlowAction wait_response_datagram()
    {
        // NOTE: This is dangerous - there must be no other allocations
        // happening in this flow between when we allocate the datagram flow
        // and when this phase is called.
        auto* client_flow =
            GetTypedAllocationResult(ifDatagram_->client_allocator());
        if (client_flow->result() & DatagramClient::OPERATION_PENDING)
        {
            return WaitForNotification();
        }
        if (!client_flow->result() & DatagramClient::OPERATION_SUCCESS)
        {
            LOG(WARNING, "Error sending response datagram for PingPong: %x",
                client_flow->result());
        }
        ifDatagram_->client_allocator()->TypedRelease(client_flow);
        return CallImmediately(ST(wait_for_datagram));
    }

private:
    int processCount_; //< tracks the number of incoming datagrams
    Buffer* responsePayload_;
};

// @TODO(balazs.racz) add a test where a datagram is arriving without
// payload. It should receive a rejection response.

class TwoNodeDatagramTest : public AsyncDatagramTest
{
protected:
    enum
    {
        OTHER_NODE_ID = TEST_NODE_ID + 0x100,
        OTHER_NODE_ALIAS = 0x225,
    };

    void setup_other_node(bool separate_if)
    {
        if (separate_if)
        {
            otherIfCan_.reset(
                new AsyncIfCan(&g_executor, &can_pipe0, 10, 10, 1, 1, 5));
            otherNodeIf_ = otherIfCan_.get();
            otherNodeIf_->add_addressed_message_support(2);
            otherDatagramSupport_.reset(
                new CanDatagramSupport(otherNodeIf_, 10, 2));
            otherNodeDatagram_ = otherDatagramSupport_.get();
        }
        else
        {
            otherNodeIf_ = if_can_.get();
            otherNodeDatagram_ = &datagram_support_;
        }
        otherNodeIf_->local_aliases()->add(OTHER_NODE_ID, OTHER_NODE_ALIAS);
        ExpectPacket(":X19100225N02010D000103;"); // node up
        otherNode_.reset(new DefaultAsyncNode(otherNodeIf_, OTHER_NODE_ID));
        Wait();
    }

    void expect_other_node_lookup()
    {
        ExpectPacket(":X1070222AN02010D000103;"); // looking for DST node
        ExpectPacket(":X1949022AN02010D000103;"); // hard-looking for DST node
        ExpectPacket(":X19170225N02010D000103;"); // node ID verified
    }

    std::unique_ptr<DefaultAsyncNode> otherNode_;
    // Second objects if we want a bus-traffic test.
    std::unique_ptr<AsyncIfCan> otherIfCan_;
    AsyncIfCan* otherNodeIf_;
    std::unique_ptr<CanDatagramSupport> otherDatagramSupport_;
    CanDatagramSupport* otherNodeDatagram_;
};

TEST_F(TwoNodeDatagramTest, PingPongTestOne)
{
    PrintAllPackets();
    setup_other_node(true);
    expect_other_node_lookup();

    PingPongHandler handler_one(&datagram_support_, node_);
    PingPongHandler handler_two(otherNodeDatagram_, otherNode_.get());

    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{OTHER_NODE_ID, 0};
    Buffer* payload = buffer_alloc(4);
    uint8_t* bytes = static_cast<uint8_t*>(payload->start());
    bytes[0] = PingPongHandler::DATAGRAM_ID;
    bytes[1] = 2;
    bytes[2] = 0x30;
    bytes[3] = 0x31;
    payload->advance(4);

    ExpectPacket(":X1A22522AN7A023031;"); // ping
    ExpectPacket(":X19A28225N022A80;");   // ack OK, reply pending
    ExpectPacket(":X1A22A225N7A0130;");   // pong
    ExpectPacket(":X19A2822AN022580;");   // ack OK, reply pending
    ExpectPacket(":X1A22522AN7A00;");     // ping
    ExpectPacket(":X19A28225N022A00;");   // ack OK, no reply

    a.result()->write_datagram(node_->node_id(), h, payload, &n);
    n.WaitForNotification();
    EXPECT_EQ((unsigned)DatagramClient::OK_REPLY_PENDING |
                  DatagramClient::OPERATION_SUCCESS,
              a.result()->result())
        << StringPrintf("result: %x", a.result()->result());
    Wait();
    EXPECT_EQ(2, handler_two.process_count());
    EXPECT_EQ(1, handler_one.process_count());
}

TEST_F(TwoNodeDatagramTest, PingPongTestError)
{
    PrintAllPackets();
    setup_other_node(true);
    expect_other_node_lookup();

    PingPongHandler handler_one(&datagram_support_, node_);
    PingPongHandler handler_two(otherNodeDatagram_, otherNode_.get());

    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{OTHER_NODE_ID, 0};
    Buffer* payload = buffer_alloc(1);
    uint8_t* bytes = static_cast<uint8_t*>(payload->start());
    bytes[0] = PingPongHandler::DATAGRAM_ID;
    payload->advance(1);

    ExpectPacket(":X1A22522AN7A;");       // ping
    ExpectPacket(":X19A48225N022A1000;"); // rejected permanent error

    a.result()->write_datagram(node_->node_id(), h, payload, &n);
    n.WaitForNotification();
    EXPECT_EQ((unsigned)DatagramClient::PERMANENT_ERROR, a.result()->result())
        << StringPrintf("result: %x", a.result()->result());
    Wait();
    EXPECT_EQ(1, handler_two.process_count());
    EXPECT_EQ(0, handler_one.process_count());
}

/// @TODO(balazs.racz): turn this into a TEST_P
TEST_F(TwoNodeDatagramTest, PingPongTestLoopback)
{
    PrintAllPackets();
    setup_other_node(false);
    // expect_other_node_lookup();

    PingPongHandler handler_one(&datagram_support_, node_);
    PingPongHandler handler_two(otherNodeDatagram_, otherNode_.get());

    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{OTHER_NODE_ID, 0};
    Buffer* payload = buffer_alloc(4);
    uint8_t* bytes = static_cast<uint8_t*>(payload->start());
    bytes[0] = PingPongHandler::DATAGRAM_ID;
    bytes[1] = 2;
    bytes[2] = 0x30;
    bytes[3] = 0x31;
    payload->advance(4);

    a.result()->write_datagram(node_->node_id(), h, payload, &n);
    n.WaitForNotification();
    EXPECT_EQ((unsigned)DatagramClient::OK_REPLY_PENDING |
                  DatagramClient::OPERATION_SUCCESS,
              a.result()->result())
        << StringPrintf("result: %x", a.result()->result());
    Wait();
    EXPECT_EQ(2, handler_two.process_count());
    EXPECT_EQ(1, handler_one.process_count());
}

TEST_F(TwoNodeDatagramTest, PingPongLoopbackError)
{
    PrintAllPackets();
    setup_other_node(false);
    //expect_other_node_lookup();

    PingPongHandler handler_one(&datagram_support_, node_);
    PingPongHandler handler_two(otherNodeDatagram_, otherNode_.get());

    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{OTHER_NODE_ID, 0};
    Buffer* payload = buffer_alloc(1);
    uint8_t* bytes = static_cast<uint8_t*>(payload->start());
    bytes[0] = PingPongHandler::DATAGRAM_ID;
    payload->advance(1);

    a.result()->write_datagram(node_->node_id(), h, payload, &n);
    n.WaitForNotification();
    EXPECT_EQ((unsigned)DatagramClient::PERMANENT_ERROR, a.result()->result())
        << StringPrintf("result: %x", a.result()->result());
    Wait();
    EXPECT_EQ(1, handler_two.process_count());
    EXPECT_EQ(0, handler_one.process_count());
}

TEST_F(TwoNodeDatagramTest, NoDestinationHandler)
{
    PrintAllPackets();
    setup_other_node(true);
    expect_other_node_lookup();

    TypedSyncAllocation<DatagramClient> a(datagram_support_.client_allocator());
    SyncNotifiable n;
    NodeHandle h{OTHER_NODE_ID, 0};
    Buffer* payload = buffer_alloc(4);
    uint8_t* bytes = static_cast<uint8_t*>(payload->start());
    bytes[0] = PingPongHandler::DATAGRAM_ID;
    bytes[1] = 2;
    bytes[2] = 0x30;
    bytes[3] = 0x31;
    payload->advance(4);

    ExpectPacket(":X1A22522AN7A023031;"); // ping
    ExpectPacket(":X19A48225N022A1000;"); // rejected, permanent error

    a.result()->write_datagram(node_->node_id(), h, payload, &n);
    n.WaitForNotification();
    EXPECT_EQ((unsigned)DatagramClient::PERMANENT_ERROR,
              a.result()->result())
        << StringPrintf("result: %x", a.result()->result());
    Wait();
}

} // namespace NMRAnet