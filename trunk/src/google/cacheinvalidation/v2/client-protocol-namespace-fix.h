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

// Brings invalidation client protocol buffers into invalidation namespace.

#ifndef GOOGLE_CACHEINVALIDATION_V2_CLIENT_PROTOCOL_NAMESPACE_FIX_H_
#define GOOGLE_CACHEINVALIDATION_V2_CLIENT_PROTOCOL_NAMESPACE_FIX_H_

#include "google/cacheinvalidation/v2/client.pb.h"
#include "google/cacheinvalidation/v2/client_protocol.pb.h"
#include "google/cacheinvalidation/v2/types.pb.h"

namespace invalidation {

using ::google::protobuf::RepeatedField;
using ::google::protobuf::RepeatedPtrField;

// Client
using ::ipc::invalidation::PersistentStateBlob;
using ::ipc::invalidation::PersistentTiclState;

// ClientProtocol
using ::ipc::invalidation::AckHandleP;
using ::ipc::invalidation::ApplicationClientIdP;
using ::ipc::invalidation::ClientHeader;
using ::ipc::invalidation::ClientVersion;
using ::ipc::invalidation::ClientToServerMessage;
using ::ipc::invalidation::InfoMessage;
using ::ipc::invalidation::InfoRequestMessage;
using ::ipc::invalidation::InfoRequestMessage_InfoType_GET_PERFORMANCE_COUNTERS;
using ::ipc::invalidation::InvalidationP;
using ::ipc::invalidation::ObjectIdP;
using ::ipc::invalidation::PropertyRecord;
using ::ipc::invalidation::RegistrationP;
using ::ipc::invalidation::RegistrationP_OpType_REGISTER;
using ::ipc::invalidation::RegistrationP_OpType_UNREGISTER;
using ::ipc::invalidation::RegistrationStatus;
using ::ipc::invalidation::RegistrationSubtree;
using ::ipc::invalidation::RegistrationSummary;
using ::ipc::invalidation::ServerToClientMessage;
using ::ipc::invalidation::StatusP;
using ::ipc::invalidation::StatusP_Code_SUCCESS;
using ::ipc::invalidation::StatusP_Code_PERMANENT_FAILURE;
using ::ipc::invalidation::StatusP_Code_TRANSIENT_FAILURE;

// Types
using ::ipc::invalidation::ObjectSource_Type_INTERNAL;

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_V2_CLIENT_PROTOCOL_NAMESPACE_FIX_H_