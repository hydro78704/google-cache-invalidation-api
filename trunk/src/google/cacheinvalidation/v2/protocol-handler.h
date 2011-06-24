// Copyright 2011 Google Inc.
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

// Client for interacting with low-level protocol messages.

#ifndef GOOGLE_CACHEINVALIDATION_V2_PROTOCOL_HANDLER_H_
#define GOOGLE_CACHEINVALIDATION_V2_PROTOCOL_HANDLER_H_

#include <string>
#include <utility>

#include "google/cacheinvalidation/v2/hash_map.h"
#include "google/cacheinvalidation/v2/hash_set.h"
#include "google/cacheinvalidation/v2/system-resources.h"
#include "google/cacheinvalidation/v2/client-protocol-namespace-fix.h"
#include "google/cacheinvalidation/v2/operation-scheduler.h"
#include "google/cacheinvalidation/v2/proto-helpers.h"
#include "google/cacheinvalidation/v2/scoped_ptr.h"
#include "google/cacheinvalidation/v2/statistics.h"
#include "google/cacheinvalidation/v2/ticl-message-validator.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::make_pair;
using INVALIDATION_STL_NAMESPACE::pair;
using INVALIDATION_STL_NAMESPACE::string;

/* Representation of a message header for use in a server message. */
struct ServerMessageHeader {
 public:
  /* Constructs an instance.
   *
   * Arguments:
   *     init_token - server-sent token
   *     init_registration_summary - summary over server registration state
   */
  ServerMessageHeader(const string& init_token,
                      const RegistrationSummary& init_registration_summary)
      : token(init_token) {
    registration_summary.CopyFrom(init_registration_summary);
  }

  string ToString() const {
    return StringPrintf(
        "Token: %s, Summary: %s", token.c_str(),
        ProtoHelpers::ToString(registration_summary).c_str());
  }

  string token;
  RegistrationSummary registration_summary;
};

class ProtocolListener {
 public:
  virtual ~ProtocolListener() {}

  /* Handles a token change event from the server.
   *
   * Arguments:
   * header - server message header
   * new_token - a new token for the client. If NULL, it means destroy the token.
   */
  virtual void HandleTokenChanged(
      const ServerMessageHeader& header, const string& new_token,
      const StatusP& status) = 0;

  /* Handles invalidations from the server.
   *
   * Arguments:
   * header - server message header
   */
  virtual void HandleInvalidations(
      const ServerMessageHeader& header,
      const RepeatedPtrField<InvalidationP>& invalidations) = 0;

  /* Handles registration updates from the server.
   *
   * Arguments:
   * header - server message header
   * reg_status - registration updates
   */
  virtual void HandleRegistrationStatus(
      const ServerMessageHeader& header,
      const RepeatedPtrField<RegistrationStatus>& reg_status) = 0;

  /* Handles a registration sync request from the server.
   *
   * Arguments:
   * header - server message header.
   */
  virtual void HandleRegistrationSyncRequest(
      const ServerMessageHeader& header) = 0;

  /* Handles an info message from the server.
   *
   * Arguments:
   * header - server message header.
   * info_types - types of info requested.
   */
  virtual void HandleInfoMessage(
      const ServerMessageHeader& header,
      const RepeatedField<int>& info_types) = 0;

  /* Stores a summary of the current desired registrations. */
  virtual void GetRegistrationSummary(RegistrationSummary* summary) = 0;

  /* Returns the current server-assigned client token, if any. */
  virtual string GetClientToken() = 0;
};

class ProtocolHandler {
 public:
  /* Configuration for the protocol client. */
  class Config {
   public:
    Config() : batching_delay(TimeDelta::FromMilliseconds(500)) {}

    /* Batching delay - certain messages (e.g., registrations, invalidation acks)
     * are sent to the server after this delay.
     */
    TimeDelta batching_delay;

    void GetConfigParams(vector<pair<string, int> >* config_params) {
      config_params->push_back(
          make_pair("batching_delay", batching_delay.ToInternalValue()));
    }
  };

  /* Creates an instance.
   *
   * config - configuration for the client
   * resources - resources to use
   * statistics - track information about messages sent/received, etc
   * application_name - name of the application using the library (for
   *     debugging/monitoring)
   * listener - callback for protocol events
   * msg_validator - validator for protocol messages
   */
  ProtocolHandler(const Config& config, SystemResources* resources,
                  Statistics* statistics, const string& application_name,
                  ProtocolListener* listener,
                  TiclMessageValidator* msg_validator);

  /* Sends a message to the server to request a client token.
   *
   * Arguments:
   * client_type - client type code as assigned by the notification system's
   *     backend
   * application_client_id - application-specific client id
   * nonce - nonce for the request
   * debug_string - information to identify the caller
   */
  void SendInitializeMessage(
      int client_type, const ApplicationClientIdP& applicationClientId,
      const string& nonce, const string& debugString);

  /* Sends an info message to the server with the performance counters supplied in
   * performance_counters and the config supplies in config_params.
   */
  void SendInfoMessage(const vector<pair<string, int> >& performance_counters,
                       const vector<pair<string, int> >& config_params);

  /* Sends a registration request to the server.
   *
   * Arguments:
   * object_ids - object ids on which to (un)register
   * reg_op_type - whether to register or unregister
   */
  void SendRegistrations(const vector<ObjectIdP>& object_ids,
                         RegistrationP::OpType reg_op_type);

  /* Sends an acknowledgement for invalidation to the server. */
  void SendInvalidationAck(const InvalidationP& invalidation);

  /* Sends a single registration subtree to the server.
   *
   * Arguments:
   * reg_subtree - subtree to send
   */
  void SendRegistrationSyncSubtree(const RegistrationSubtree& reg_subtree);

 private:
  /* Handles a message from the server. */
  void HandleIncomingMessage(string incoming_message);

  /* Verifies that the {@code serverToken} matches the token currently held by
   * the client.
   */
  bool CheckServerToken(const string& server_token);

  /* Sends pending data to the server (e.g., registrations, acks, registration
   * sync messages).
   *
   * Arguments:
   * builder - initial message builder
   * debug_string - information to identify the caller
   */
  void SendMessageToServer(ClientToServerMessage* builder,
                           const string& debugString);

  /* Stores the header to include on a message to the server. */
  void InitClientHeader(ClientHeader* header);

  /* Does the actual work of the batching task. */
  void BatchingTask();

  /* Handles inbound messages from the network. */
  void MessageReceiver(string message);

  /* Responds to changes in network connectivity. */
  void NetworkStatusReceiver(bool status);

  ClientVersion client_version_;
  SystemResources* resources_;  // who owns this?

  // Cached from resources
  Logger* logger_;
  Scheduler* internal_scheduler_;

  ProtocolListener* listener_;
  scoped_ptr<OperationScheduler> operation_scheduler_;
  TiclMessageValidator* msg_validator_;

  /* A debug message id that is added to every message to the server. */
  int message_id_;

  // State specific to a client. If we want to support multiple clients, this
  // could be in a map or could be eliminated (e.g., no batching).

  /* The last known time from the server. */
  int64 last_known_server_time_ms_;

  /* Set of pending registrations stored as a map for overriding later
   * operations.
   */
  hash_map<ObjectIdP, RegistrationP::OpType, ProtoHash, ProtoEq>
  pending_registrations_;

  /* Set of pending invalidation acks. */
  hash_set<InvalidationP, ProtoHash, ProtoEq> acked_invalidations_;

  /* Set of pending registration sub trees for registration sync. */
  hash_set<RegistrationSubtree, ProtoHash, ProtoEq> registration_subtrees_;

  /* Statistics objects to track number of sent messages, etc. */
  Statistics* statistics_;

  /* Task to send all batched messages to the server. */
  scoped_ptr<Closure> batching_task_;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_V2_PROTOCOL_HANDLER_H_
