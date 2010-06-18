// Copyright 2010 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <queue>

#include "base/scoped_ptr.h"
#include "google/cacheinvalidation/googletest.h"
#include "google/cacheinvalidation/logging.h"
#include "google/cacheinvalidation/invalidation-client-impl.h"
#include "google/cacheinvalidation/stl-namespace.h"
#include "google/cacheinvalidation/system-resources-for-test.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::make_pair;
using INVALIDATION_STL_NAMESPACE::pair;
using INVALIDATION_STL_NAMESPACE::vector;

/* A listener for testing. */
class TestListener : public InvalidationListener {
 public:
  TestListener() : invalidate_all_count_(0), all_registrations_lost_count_(0) {}

  virtual void Invalidate(const Invalidation& invalidation, Closure* callback) {
    CHECK(IsCallbackRepeatable(callback));
    invalidations_.push_back(make_pair(invalidation, callback));
  }

  virtual void InvalidateAll(Closure* callback) {
    CHECK(IsCallbackRepeatable(callback));
    ++invalidate_all_count_;
    callback->Run();
    delete callback;
  }

  virtual void AllRegistrationsLost(Closure* callback) {
    CHECK(IsCallbackRepeatable(callback));
    ++all_registrations_lost_count_;
    delete callback;
  }

  virtual void RegistrationLost(const ObjectId& objectId, Closure* callback) {
    CHECK(IsCallbackRepeatable(callback));
    removed_registrations_.push_back(objectId);
    callback->Run();
    delete callback;
  }

  /* The number of InvalidateAll() calls it's received. */
  int invalidate_all_count_;

  /* Number of times AllRegistrationsLost has been called. */
  int all_registrations_lost_count_;

  /* The individual invalidations received, with their callbacks. */
  vector<pair<Invalidation, Closure*> > invalidations_;

  /* Individual registration removals the Ticl has informed us about. */
  vector<ObjectId> removed_registrations_;
};

class InvalidationClientImplTest : public testing::Test {
 public:
  InvalidationClientImplTest() :
      // Calls to the outbound network listener are throttled to no more than
      // one per second, so sometimes we need to advance time by this much in
      // order for the next call to be made.
      fine_throttle_interval_(TimeDelta::FromSeconds(1)),
      default_registration_timeout_(TimeDelta::FromMinutes(1)) {}

  /* A name for the application. */
  static const char* APP_NAME;

  /* Fake data for a session token. */
  static const char* OPAQUE_DATA;

  /* A status object indicating success. */
  Status success_status_;

  /* An object id. */
  ObjectId object_id1_;

  /* An object id. */
  ObjectId object_id2_;

  /* A sample version. */
  static const int64 VERSION;

  /* System resources for testing. */
  scoped_ptr<SystemResourcesForTest> resources_;

  /* Test listener. */
  scoped_ptr<TestListener> listener_;

  /* The invalidation client being tested. */
  scoped_ptr<InvalidationClient> ticl_;

  /* A field that's set when the Ticl informs us about an outgoing message.
   */
  bool outbound_message_ready_;

  /* Listens for outbound messages from the Ticl. */
  void HandleOutboundMessageReady(NetworkEndpoint* const& endpoint) {
    outbound_message_ready_ = true;
  }

  scoped_ptr<NetworkCallback> network_listener_;

  /* The uniquifier that we've assigned for the client. */
  string client_uniquifier_;

  /* The session token we've assigned for the client. */
  string session_token_;

  /* A register operation. */
  RegistrationUpdate reg_op1_;

  /* A register operation. */
  RegistrationUpdate reg_op2_;

  /* Registration responses we've received. */
  vector<RegistrationUpdateResult> reg_results_;

  /* The throttler's smaller window size. */
  TimeDelta fine_throttle_interval_;

  /* The default registration timeout. */
  TimeDelta default_registration_timeout_;

  /* A registration callback that writes its result to reg_results_. */
  void HandleRegistrationResult(const RegistrationUpdateResult& result) {
    reg_results_.push_back(result);
  }

  scoped_ptr<RegistrationCallback > callback_;

  /* Checks that client's message contains a proper id-assignment request. */
  void CheckAssignClientIdRequest(
      const ClientToServerMessage& message, ClientExternalId* result) {
    // Check that the message contains an "assign client id" action.
    ASSERT_TRUE(message.has_action());
    ASSERT_EQ(message.action(), ClientToServerMessage_Action_ASSIGN_CLIENT_ID);

    // Check that the message contains an "assign client id" type.
    ASSERT_TRUE(message.has_message_type());
    ASSERT_EQ(message.message_type(),
              ClientToServerMessage_MessageType_TYPE_ASSIGN_CLIENT_ID);

    // Check that it does not contain a session token or any registration
    // operations or invalidation acknowledgments.
    ASSERT_FALSE(message.has_session_token());
    ASSERT_EQ(message.acked_invalidation_size(), 0);
    ASSERT_EQ(message.register_operation_size(), 0);

    // Check that it contains the fields of an external id.
    ASSERT_TRUE(message.has_client_type());
    ASSERT_EQ(message.client_type().type(), ClientType_Type_CHROME_SYNC);
    ASSERT_TRUE(message.has_app_client_id());
    ASSERT_EQ(message.app_client_id().string_value(), APP_NAME);

    // Check that the client did not specify values for the server-supplied
    // fields.
    ASSERT_FALSE(message.has_session_token());

    result->mutable_client_type()->CopyFrom(message.client_type());
    result->mutable_app_client_id()->CopyFrom(message.app_client_id());
  }

  void TestInitialization() {
    // Start up the Ticl, connect a network listener, and let it do its
    // initialization.
    ticl_->network_endpoint()->RegisterOutboundListener(
        network_listener_.get());
    resources_->RunReadyTasks();

    // Check that it has a message to send, and pull the message.
    ASSERT_TRUE(outbound_message_ready_);
    outbound_message_ready_ = false;
    string serialized;
    ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
    ClientToServerMessage message;
    message.ParseFromString(serialized);

    // Check that the message is a proper request for client id assignment.
    ClientExternalId external_id;
    CheckAssignClientIdRequest(message, &external_id);

    // Construct a uniquifier.
    client_uniquifier_ = "uniquifier";

    // Also construct an initial session token.
    session_token_ = OPAQUE_DATA;

    // Construct a response with the uniquifier and session token.
    ServerToClientMessage response;
    response.mutable_client_type()->set_type(external_id.client_type().type());
    response.mutable_app_client_id()->set_string_value(
        external_id.app_client_id().string_value());
    response.set_nonce(message.nonce());
    response.set_client_id(client_uniquifier_);
    response.set_session_token(session_token_);
    response.mutable_status()->set_code(Status_Code_SUCCESS);
    response.set_message_type(
        ServerToClientMessage_MessageType_TYPE_ASSIGN_CLIENT_ID);

    response.SerializeToString(&serialized);

    // Give the message to the Ticl, and let it handle it.
    ticl_->network_endpoint()->HandleInboundMessage(serialized);
    resources_->RunReadyTasks();

    // Check that it didn't give the app an InvalidateAll.
    ASSERT_EQ(listener_->invalidate_all_count_, 0);

    // Pull another message from the Ticl.
    ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
    message.ParseFromString(serialized);

    // Check that it has the right session token, and that it's polling
    // invalidations.
    ASSERT_TRUE(message.has_session_token());
    ASSERT_EQ(message.session_token(), session_token_);
    ASSERT_TRUE(message.has_action());
    ASSERT_EQ(message.action(),
              ClientToServerMessage_Action_POLL_INVALIDATIONS);
  }

  /* Requests that the Ticl (un)register for two objects.  Checks that the
   * message it sends contains the correct information about these
   * (un)registrations.
   */
  void MakeAndCheckRegistrations(bool is_register) {
    void (InvalidationClient::*operation)(const ObjectId&,
                                          RegistrationCallback*) =
        is_register ?
        &InvalidationClient::Register : &InvalidationClient::Unregister;

    // Explicitness hack here to work around broken callback
    // implementations.
    void (RegistrationCallback::*run_function)(
        const RegistrationUpdateResult&) = &RegistrationCallback::Run;

    // Ask the Ticl to register for two objects.
    outbound_message_ready_ = false;
    (ticl_.get()->*operation)(
        object_id1_,
        NewPermanentCallback(callback_.get(), run_function));
    (ticl_.get()->*operation)(
        object_id2_,
        NewPermanentCallback(callback_.get(), run_function));
    resources_->ModifyTime(fine_throttle_interval_);
    resources_->RunReadyTasks();
    ASSERT_TRUE(outbound_message_ready_);

    RegistrationUpdate_Type operation_type = is_register ?
        RegistrationUpdate_Type_REGISTER : RegistrationUpdate_Type_UNREGISTER;

    // Pull a message, and check that it has the right session token and
    // registration update messages.
    ClientToServerMessage message;
    string serialized;
    ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
    message.ParseFromString(serialized);
    ASSERT_TRUE(message.has_session_token());
    ASSERT_EQ(message.session_token(), session_token_);
    ASSERT_TRUE(message.has_message_type());
    ASSERT_EQ(message.message_type(),
              ClientToServerMessage_MessageType_TYPE_OBJECT_CONTROL);
    ASSERT_EQ(message.register_operation_size(), 2);
    reg_op1_.Clear();
    reg_op1_.mutable_object_id()->CopyFrom(object_id1_);
    reg_op1_.set_sequence_number(1);
    reg_op1_.set_type(operation_type);
    reg_op2_.mutable_object_id()->CopyFrom(object_id2_);
    reg_op2_.set_sequence_number(2);
    reg_op2_.set_type(operation_type);

    string serialized2, serialized_reg_op1, serialized_reg_op2;
    reg_op1_.SerializeToString(&serialized_reg_op1);
    message.register_operation(0).SerializeToString(&serialized);
    reg_op2_.SerializeToString(&serialized_reg_op2);
    message.register_operation(1).SerializeToString(&serialized2);
    ASSERT_TRUE(((serialized == serialized_reg_op1) &&
                 (serialized2 == serialized_reg_op2)) ||
                ((serialized == serialized_reg_op2) &&
                 (serialized2 == serialized_reg_op1)));

    // Check that the Ticl has not responded to the app about either of the
    // operations yet.
    ASSERT_TRUE(reg_results_.empty());
  }

  void TestRegistration(bool is_register) {
    // Do setup and initiate registrations.
    TestInitialization();
    outbound_message_ready_ = false;
    MakeAndCheckRegistrations(is_register);

    // Construct responses and let the Ticl process them.
    ServerToClientMessage response;
    RegistrationUpdateResult* result1 = response.add_registration_result();
    result1->mutable_operation()->CopyFrom(reg_op1_);
    result1->mutable_status()->set_code(Status_Code_SUCCESS);
    RegistrationUpdateResult* result2 = response.add_registration_result();
    result2->mutable_operation()->CopyFrom(reg_op2_);
    result2->mutable_status()->set_code(Status_Code_SUCCESS);
    response.mutable_status()->set_code(Status_Code_SUCCESS);
    response.set_session_token(session_token_);
    response.set_message_type(
        ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
    string serialized;
    response.SerializeToString(&serialized);
    ticl_->network_endpoint()->HandleInboundMessage(serialized);
    resources_->RunReadyTasks();

    // Check that the registration callback was invoked.
    ASSERT_EQ(reg_results_.size(), 2);
    string serialized2;
    reg_results_[0].SerializeToString(&serialized);
    result1->SerializeToString(&serialized2);
    ASSERT_EQ(serialized, serialized2);
    reg_results_[1].SerializeToString(&serialized);
    result2->SerializeToString(&serialized2);
    ASSERT_EQ(serialized, serialized2);

    // Advance the clock a lot, run everything, and make sure it's not trying to
    // resend.
    resources_->ModifyTime(default_registration_timeout_);
    resources_->RunReadyTasks();
    ClientToServerMessage message;
    ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
    message.ParseFromString(serialized);
    ASSERT_EQ(message.register_operation_size(), 0);
  }

  void TestSessionSwitch() {
    TestRegistration(true);

    // Clear the "outbound message ready" flag, so we can check below that the
    // invalid session status causes it to be set.
    outbound_message_ready_ = false;

    // Tell the Ticl its session is invalid.
    ServerToClientMessage message;
    message.set_session_token(session_token_);
    message.mutable_status()->set_code(Status_Code_INVALID_SESSION);
    message.set_message_type(
        ServerToClientMessage_MessageType_TYPE_INVALIDATE_SESSION);
    string serialized;
    message.SerializeToString(&serialized);
    ticl_->network_endpoint()->HandleInboundMessage(serialized);
    resources_->ModifyTime(fine_throttle_interval_);
    resources_->RunReadyTasks();

    // Check that the Ticl has pinged the client to indicate it has a request.
    ASSERT_TRUE(outbound_message_ready_);

    // Pull a message from the Ticl and check that it requests a new session.
    ClientToServerMessage request;
    ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
    request.ParseFromString(serialized);
    ASSERT_TRUE(request.has_action());
    ASSERT_EQ(request.action(), ClientToServerMessage_Action_UPDATE_SESSION);
    ASSERT_TRUE(request.has_message_type());
    ASSERT_EQ(request.message_type(),
              ClientToServerMessage_MessageType_TYPE_UPDATE_SESSION);
    ASSERT_TRUE(request.has_client_id());
    ASSERT_EQ(client_uniquifier_, request.client_id());

    // Give it a new session token.
    int all_registrations_lost_count = listener_->all_registrations_lost_count_;
    session_token_ = "NEW_OPAQUE_DATA";
    message.Clear();
    message.set_client_id(client_uniquifier_);
    message.set_session_token(session_token_);
    message.mutable_status()->set_code(Status_Code_SUCCESS);
    message.set_message_type(
        ServerToClientMessage_MessageType_TYPE_UPDATE_SESSION);
    message.SerializeToString(&serialized);
    ticl_->network_endpoint()->HandleInboundMessage(serialized);
    resources_->RunReadyTasks();

    // Check that it issued AllRegistrationsLost.
    ASSERT_EQ(all_registrations_lost_count + 1,
              listener_->all_registrations_lost_count_);
  }

  virtual void SetUp() {
    object_id1_.Clear();
    object_id1_.set_source(ObjectId_Source_CHROME_SYNC);
    object_id1_.mutable_name()->set_string_value("BOOKMARKS");
    object_id2_.Clear();
    object_id2_.set_source(ObjectId_Source_CHROME_SYNC);
    object_id2_.mutable_name()->set_string_value("HISTORY");
    resources_.reset(new SystemResourcesForTest());
    resources_->ModifyTime(TimeDelta::FromSeconds(1000000));
    resources_->StartScheduler();
    listener_.reset(new TestListener());
    network_listener_.reset(
        NewPermanentCallback(
            this, &InvalidationClientImplTest::HandleOutboundMessageReady));
    callback_.reset(
        NewPermanentCallback(
            this, &InvalidationClientImplTest::HandleRegistrationResult));
    ClientConfig ticl_config;
    ClientType client_type;
    client_type.set_type(ClientType_Type_CHROME_SYNC);
    ticl_.reset(new InvalidationClientImpl(
        resources_.get(), client_type, APP_NAME, listener_.get(), ticl_config));
    reg_results_.clear();
  }

  virtual void TearDown() {
    resources_->StopScheduler();
  }
};

const char* InvalidationClientImplTest::APP_NAME = "app_name";
const char* InvalidationClientImplTest::OPAQUE_DATA = "opaque_data";
const int64 InvalidationClientImplTest::VERSION = 5;

TEST_F(InvalidationClientImplTest, InitializationTest) {
  /* Test plan: start up a new Ticl.  Check that it requests to send a message
   * and that the message requests client id assignment with an appropriately
   * formed partial client id.  Respond with a full client id and session token.
   * Check that the Ticl's next step is to poll invalidations.
   */
  TestInitialization();
}

TEST_F(InvalidationClientImplTest, MismatchingClientIdIgnored) {
  /* Test plan: create a Ticl and pull a bundle from it, which will be
   * requesting a client id.  Respond with a client id, but for a mismatched app
   * client id.  Check that pulling a subsequent bundle results in another
   * assign-client-id action.
   */

  // Start up the Ticl, connect a network listener, and let it do its
  // initialization.
  ticl_->network_endpoint()->RegisterOutboundListener(network_listener_.get());
  resources_->RunReadyTasks();

  // Pull a message.
  ClientToServerMessage message;
  string serialized;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);

  // Check that the message is a proper request for client id assignment.
  ClientExternalId external_id;
  CheckAssignClientIdRequest(message, &external_id);

  // Fabricate a uniquifier and initial session token.
  client_uniquifier_ = "uniquifier";
  session_token_ = OPAQUE_DATA;

  // Construct a response with the uniquifier and session token but the wrong
  // app client id.
  ServerToClientMessage response;
  response.mutable_client_type()->CopyFrom(external_id.client_type());
  response.mutable_app_client_id()->set_string_value("wrong-app-client-id");
  response.set_client_id(client_uniquifier_);
  response.set_session_token(session_token_);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.SerializeToString(&serialized);
  response.set_message_type(
      ServerToClientMessage_MessageType_TYPE_ASSIGN_CLIENT_ID);

  // Give the message to the Ticl, and let it handle it.
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Pull a message.
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);

  // Check that the Ticl is still looking for a client id.
  CheckAssignClientIdRequest(message, &external_id);
}

TEST_F(InvalidationClientImplTest, PollingIntervalRespected) {
  /* Test plan: get a client id and session, and consume the initial
   * poll-invalidations request.  Send a message reducing the polling interval
   * to 10s.  Check that we won't send a poll-invalidations until 10s in the
   * future.  Now increase the polling interval to 100s, and again check that we
   * won't send a poll-invalidations until 100s in the future.
   */

  // Handle setup.
  TestInitialization();

  // Respond to the client's poll with a new polling interval.
  ServerToClientMessage response;
  string serialized;
  response.set_session_token(session_token_);
  response.set_next_poll_interval_ms(10000);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  response.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Advance to 1 ms before the polling interval, and check that the Ticl does
  // not try to poll again.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(9999));
  ClientToServerMessage message;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);

  ASSERT_FALSE(message.has_action());

  // Advance the last ms and check that the Ticl does try to poll.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(1));
  resources_->RunReadyTasks();
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  ASSERT_EQ(message.action(), ClientToServerMessage_Action_POLL_INVALIDATIONS);

  // Respond and increase the polling interval.
  response.Clear();
  response.set_session_token(session_token_);
  response.set_next_poll_interval_ms(100000);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  response.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Advance the time to just before the polling interval expires, and check
  // that no poll request is sent.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(99999));
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  ASSERT_FALSE(message.has_action());

  // Advance so that the polling interval is fully elapsed, and check that the
  // Ticl does poll.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(1));
  resources_->RunReadyTasks();
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  ASSERT_EQ(message.action(), ClientToServerMessage_Action_POLL_INVALIDATIONS);
}

TEST_F(InvalidationClientImplTest, HeartbeatIntervalRespected) {
  /* Test plan: get a client id and session, and consume the initial
   * poll-invalidations message.  Respond and increase heartbeat interval to
   * 80s.  Check that the outbound message listener doesn't get pinged until 80s
   * in the future.  Then send a message reducing the heartbeat interval to 10s.
   * Because of the way the heartbeat timer is implemented, we don't expect the
   * very next heartbeat to occur until 80s in the future, but subsequently it
   * should be 10s.
   */

  // Do setup.
  TestInitialization();

  // Respond with a new heartbeat interval (larger than the default).
  int new_heartbeat_interval_ms = 300000;
  ServerToClientMessage response;
  response.set_session_token(session_token_);
  response.set_next_heartbeat_interval_ms(new_heartbeat_interval_ms);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  string serialized;
  response.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  outbound_message_ready_ = false;

  // Advance to just shy of the heartbeat interval, and check that the Ticl did
  // not nudge the application to send.
  resources_->ModifyTime(
      TimeDelta::FromMilliseconds(new_heartbeat_interval_ms - 1));
  resources_->RunReadyTasks();
  ASSERT_FALSE(outbound_message_ready_);

  // Advance further, and check that it did nudge the application to send.
  resources_->ModifyTime(fine_throttle_interval_);
  resources_->RunReadyTasks();
  ASSERT_TRUE(outbound_message_ready_);

  // Shorten the heartbeat interval and repeat.
  response.Clear();
  response.set_session_token(session_token_);
  response.set_next_heartbeat_interval_ms(10000);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  response.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  outbound_message_ready_ = false;

  // Because the Ticl uses a single timer-task, the next heartbeat will still
  // happen after the longer interval.
  // Periodic task executes after this since heartbeat interval is large.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(80000));
  resources_->RunReadyTasks();
  ASSERT_TRUE(outbound_message_ready_);
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  outbound_message_ready_ = false;

  // But subsequently, heartbeats should happen with the shorter interval.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(9999));
  resources_->RunReadyTasks();
  ASSERT_FALSE(outbound_message_ready_);

  resources_->ModifyTime(fine_throttle_interval_);
  resources_->RunReadyTasks();

  ASSERT_TRUE(outbound_message_ready_);
}

TEST_F(InvalidationClientImplTest, Registration) {
  /* Test plan: get a client id and session.  Register for an object.  Check
   * that the Ticl sends an appropriate registration request.  Respond with a
   * successful status.  Check that the registration callback is invoked with an
   * appropriate result, and that the Ticl does not resend the request.
   */
  TestRegistration(true);
}

TEST_F(InvalidationClientImplTest, Unegistration) {
  /* Test plan: get a client id and session.  Unregister for an object.  Check
   * that the Ticl sends an appropriate unregistration request.  Respond with a
   * successful status.  Check that the unregistration callback is invoked with
   * an appropriate result, and that the Ticl does not resend the request.
   */
  TestRegistration(false);
}

TEST_F(InvalidationClientImplTest, OrphanedRegistration) {
  /* Test plan: get a client id and session.  Register for an object.  Check
   * that the Ticl sends an appropriate registration request.  Don't respond;
   * just check that the callbacks aren't leaked.
   */
  TestInitialization();
  outbound_message_ready_ = false;
  MakeAndCheckRegistrations(true);
}

TEST_F(InvalidationClientImplTest, RegistrationRetried) {
  /* Test plan: get a client id and session.  Register for an object.  Check
   * that the Ticl sends a registration request.  Advance the clock without
   * responding to the request.  Check that the Ticl resends the request.
   * Repeat the last step to ensure that retrying happens more than once.
   * Finally, respond and check that the callback was invoked with an
   * appropriate result.
   */
  TestInitialization();
  outbound_message_ready_ = false;
  MakeAndCheckRegistrations(true);

  // Advance the clock without responding and make sure the Ticl resends the
  // request.
  resources_->ModifyTime(default_registration_timeout_);
  resources_->RunReadyTasks();
  ClientToServerMessage message;
  string serialized;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  ASSERT_EQ(message.register_operation_size(), 2);

  string serialized2, serialized_reg_op1, serialized_reg_op2;
  reg_op1_.SerializeToString(&serialized_reg_op1);
  reg_op2_.SerializeToString(&serialized_reg_op2);
  message.register_operation(0).SerializeToString(&serialized);
  message.register_operation(1).SerializeToString(&serialized2);
  ASSERT_TRUE(((serialized == serialized_reg_op1) &&
               (serialized2 == serialized_reg_op2)) ||
              ((serialized == serialized_reg_op2) &&
               (serialized2 == serialized_reg_op1)));

  // Ack one of the registrations.
  ServerToClientMessage response;
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.set_session_token(session_token_);
  response.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  RegistrationUpdateResult* result = response.add_registration_result();
  result->mutable_operation()->CopyFrom(reg_op2_);
  result->mutable_status()->set_code(Status_Code_SUCCESS);
  response.SerializeToString(&serialized);

  // Deliver the ack and check that the registration callback is invoked.
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  ASSERT_EQ(reg_results_.size(), 1);

  result->SerializeToString(&serialized);
  reg_results_[0].SerializeToString(&serialized2);
  ASSERT_EQ(serialized, serialized2);

  // Advance the clock again, and check that (only) the unacked operation is
  // retried again.
  resources_->ModifyTime(default_registration_timeout_);
  resources_->RunReadyTasks();
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  // regOps = message.getRegisterOperationList();
  ASSERT_EQ(message.register_operation_size(), 1);
  message.register_operation(0).SerializeToString(&serialized);
  reg_op1_.SerializeToString(&serialized2);
  ASSERT_EQ(serialized, serialized2);

  // Now ack the other registration.
  response.Clear();
  result = response.add_registration_result();
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.set_session_token(session_token_);
  response.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  result->mutable_operation()->CopyFrom(reg_op1_);
  result->mutable_status()->set_code(Status_Code_SUCCESS);
  response.SerializeToString(&serialized);

  // Check that the reg. callback was invoked for the second ack.
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  ASSERT_EQ(reg_results_.size(), 2);

  result->SerializeToString(&serialized);
  reg_results_[1].SerializeToString(&serialized2);
  ASSERT_EQ(serialized, serialized2);
}

TEST_F(InvalidationClientImplTest, RegistrationFailure) {
  /* Test plan: get a client id and session.  Register for an object.  Check
   * that the Ticl sends an appropriate registration request.  Respond with an
   * error status.  Check that the registration callback is invoked with an
   * appropriate result, and that the Ticl does not resend the request.
   */

  // Do setup and initiate registrations.
  TestInitialization();
  outbound_message_ready_ = false;
  MakeAndCheckRegistrations(true);

  // Construct and deliver responses: one failure and one success.
  ServerToClientMessage response;
  RegistrationUpdateResult* result1 = response.add_registration_result();
  result1->mutable_operation()->CopyFrom(reg_op1_);
  result1->mutable_status()->set_code(Status_Code_OBJECT_UNKNOWN);
  result1->mutable_status()->set_description("Registration update failed");
  RegistrationUpdateResult* result2 = response.add_registration_result();
  result2->mutable_operation()->CopyFrom(reg_op2_);
  result2->mutable_status()->set_code(Status_Code_SUCCESS);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.set_session_token(session_token_);
  response.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  string serialized;
  response.SerializeToString(&serialized);

  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Check that the registration callback was invoked.
  ASSERT_EQ(reg_results_.size(), 2);
  string serialized2;
  reg_results_[0].SerializeToString(&serialized);
  result1->SerializeToString(&serialized2);
  ASSERT_EQ(serialized, serialized2);

  reg_results_[1].SerializeToString(&serialized);
  result2->SerializeToString(&serialized2);
  ASSERT_EQ(serialized, serialized2);

  // Advance the clock a lot, run everything, and make sure it's not trying to
  // resend.
  resources_->ModifyTime(default_registration_timeout_);
  resources_->RunReadyTasks();
  ClientToServerMessage message;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  ASSERT_EQ(message.register_operation_size(), 0);
}

TEST_F(InvalidationClientImplTest, Invalidation) {
  /* Test plan: get a client id and session token, and register for an object.
   * Deliver an invalidation for that object.  Check that the listener's
   * invalidate() method gets called with the right invalidation.  Check that
   * the Ticl acks the invalidation, but only after the listener has acked it.
   */
  TestRegistration(true);

  // Deliver and invalidation for an object.
  ServerToClientMessage message;
  Invalidation* invalidation = message.add_invalidation();
  invalidation->mutable_object_id()->CopyFrom(object_id1_);
  invalidation->set_version(InvalidationClientImplTest::VERSION);
  message.set_session_token(session_token_);
  message.mutable_status()->set_code(Status_Code_SUCCESS);
  message.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  string serialized;
  message.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Check that the app (listener) was informed of the invalidation.
  ASSERT_EQ(listener_->invalidations_.size(), 1);
  pair<Invalidation, Closure*> tmp = listener_->invalidations_[0];
  string serialized2;
  tmp.first.SerializeToString(&serialized);
  invalidation->SerializeToString(&serialized2);
  ASSERT_EQ(serialized, serialized2);

  // Check that the Ticl isn't acking the invalidation yet, since we haven't
  // called the callback.
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  ClientToServerMessage client_message;
  client_message.ParseFromString(serialized);
  ASSERT_EQ(client_message.acked_invalidation_size(), 0);
  outbound_message_ready_ = false;

  // Now run the callback, and check that the Ticl does ack the invalidation.
  tmp.second->Run();
  delete tmp.second;
  resources_->ModifyTime(fine_throttle_interval_);
  resources_->RunReadyTasks();
  ASSERT_TRUE(outbound_message_ready_);
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  client_message.ParseFromString(serialized);
  ASSERT_EQ(client_message.acked_invalidation_size(), 1);
  client_message.acked_invalidation(0).SerializeToString(&serialized);
  ASSERT_EQ(serialized, serialized2);
}

TEST_F(InvalidationClientImplTest, SessionSwitch) {
  /* Test plan: get client id and session.  Register for a couple of objects.
   * Send the Ticl an invalid-session message.  Check that the Ticl sends an
   * UpdateSession request, and respond with a new session token and last
   * sequence number of 1.  Check that the Ticl resends a registration request
   * for the second register operation.
   */
  TestSessionSwitch();
}

TEST_F(InvalidationClientImplTest, MismatchingInvalidSessionIgnored) {
  /* Test plan: get client id and session.  Register for a couple of objects.
   * Send the Ticl an invalid-session message with a mismatched session token.
   * Check that the Ticl ignores it.
   */
  TestRegistration(true);

  // Tell the Ticl its session is invalid.
  string bogus_session_token = "bogus-session-token";
  ServerToClientMessage message;
  message.mutable_status()->set_code(Status_Code_INVALID_SESSION);
  message.set_session_token(bogus_session_token);
  message.set_message_type(
      ServerToClientMessage_MessageType_TYPE_INVALIDATE_SESSION);
  string serialized;
  message.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Pull a message from the Ticl and check that it doesn't request a new
  // session.
  ClientToServerMessage request;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  request.ParseFromString(serialized);
  ASSERT_FALSE(request.has_action());
}

TEST_F(InvalidationClientImplTest, GarbageCollection) {
  /* Test plan: get a client id and session, and perform some registrations.
   * Send the Ticl a message indicating it has been garbage-collected.  Check
   * that the Ticl requests a new client id.  Respond with one, along with a
   * session.  Check that it repeats the register operations, and that it sends
   * an invalidateAll once the registrations have completed.
   */
  TestRegistration(true);

  // Tell the Ticl we don't recognize it.
  ServerToClientMessage message;
  message.Clear();
  message.mutable_status()->set_code(Status_Code_UNKNOWN_CLIENT);
  message.set_session_token(session_token_);
  string serialized;
  message.set_client_id(client_uniquifier_);
  message.set_message_type(
      ServerToClientMessage_MessageType_TYPE_INVALIDATE_CLIENT_ID);
  message.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Pull a message from it, and check that it's trying to assign a client id.
  ClientToServerMessage request;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  request.ParseFromString(serialized);
  ASSERT_TRUE(request.has_action());
  ASSERT_EQ(request.action(), ClientToServerMessage_Action_ASSIGN_CLIENT_ID);
  ClientExternalId external_id;
  CheckAssignClientIdRequest(request, &external_id);

  // Give it a new uniquifier and session.
  string new_uniquifier_str = "newuniquifierstr";

  session_token_ = "new opaque data";
  ServerToClientMessage response;
  response.set_session_token(session_token_);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.mutable_client_type()->set_type(external_id.client_type().type());
  response.mutable_app_client_id()->set_string_value(
      external_id.app_client_id().string_value());
  response.set_nonce(request.nonce());
  response.set_client_id(new_uniquifier_str);
  response.set_message_type(
      ServerToClientMessage_MessageType_TYPE_ASSIGN_CLIENT_ID);
  response.SerializeToString(&serialized);

  int all_registrations_lost_count = listener_->all_registrations_lost_count_;
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Check that it invoked AllRegistrationsLost().
  ASSERT_EQ(all_registrations_lost_count + 1,
            listener_->all_registrations_lost_count_);
}

TEST_F(InvalidationClientImplTest, MismatchedUnknownClientIgnored) {
  /* Test plan: get a client id and session, and perform some registrations.
   * Send the Ticl a message indicating it has been garbage-collected, with a
   * mismatched client id.  Check that the Ticl ignores it.
   */
  TestRegistration(true);

  // Tell the Ticl we don't recognize it, but supply an incorrect client id.
  ServerToClientMessage message;
  message.mutable_status()->set_code(Status_Code_UNKNOWN_CLIENT);
  message.set_session_token(session_token_);
  message.set_client_id("bogus-client-id");
  message.set_message_type(
      ServerToClientMessage_MessageType_TYPE_INVALIDATE_CLIENT_ID);
  string serialized;
  message.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Pull a message from it, and check that it's not trying to assign a client
  // id.
  ClientToServerMessage request;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  request.ParseFromString(serialized);
  ASSERT_FALSE(request.has_action());
}

TEST_F(InvalidationClientImplTest, Throttling) {
  /* Test plan: initialize the Ticl.  Send it a message telling it to set its
   * heartbeat and polling intervals to 1 ms.  Make sure its pings to the app
   * don't violate the (default) rate limits.
   */
  TestInitialization();

  ServerToClientMessage message;
  message.mutable_status()->set_code(Status_Code_SUCCESS);
  message.set_session_token(session_token_);
  message.set_next_heartbeat_interval_ms(1);
  message.set_next_poll_interval_ms(1);
  message.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  string serialized;
  message.SerializeToString(&serialized);

  ticl_->network_endpoint()->HandleInboundMessage(serialized);

  // Run for five minutes in 10ms increments, counting the number of times the
  // Ticl tells us it has a bundle.
  int ping_count = 0;
  for (int i = 0; i < 30000; ++i) {
    resources_->ModifyTime(TimeDelta::FromMilliseconds(10));
    resources_->RunReadyTasks();
    if (outbound_message_ready_) {
      ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
      outbound_message_ready_ = false;
      ++ping_count;
    }
  }
  ASSERT_GE(ping_count, 28);
  ASSERT_LE(ping_count, 30);
}

}  // namespace invalidation