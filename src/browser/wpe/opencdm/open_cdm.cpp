/*
 * Copyright 2016-2017 TATA ELXSI
 * Copyright 2016-2017 Metrological
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "open_cdm.h"

#include <cdm_logging.h>
#include <cstdlib>
#include <open_cdm_common.h>
#include <open_cdm_mediaengine_factory.h>
#include <open_cdm_platform_factory.h>

using namespace std;

namespace media {

OpenCdm::OpenCdm()
  : platform_(OpenCdmPlatformInterfaceFactory::Create(this))
  , m_eState(KEY_SESSION_INIT) {

  CDM_LOG_LINE("called");

  // TODO: It would be nice if we could call OpenCDM, this seems like a good place to let
  // webkit pass a delegate to us.
}

OpenCdm::~OpenCdm() {
  CDM_LOG_LINE("destroy OpenCdm");

  // FIXME: Smart pointers are smart.
  if (platform_) {
    platform_->MediaKeySessionRelease(m_session_id.session_id, m_session_id.session_id_len);
    delete(platform_);
  }
}

void OpenCdm::SelectKeySystem(const std::string& key_system) {
  CDM_LOG_LINE("ask for keys for system %s", key_system.c_str());
  m_key_system = key_system;
  auto response = platform_->MediaKeys(key_system);
  if (response.platform_response == PLATFORM_CALL_SUCCESS)
    m_eState = KEY_SESSION_INIT;
}

void OpenCdm::SelectSession(const std::string& session_id_rcvd) {
  CDM_LOG_LINE("select session %s", session_id_rcvd.c_str());
  // FIXME: I don't like the look of this strdup, probably a leak.
  // FIXME: Any guesses why we go from a string interface to an open struct?
  m_session_id.session_id = strdup(session_id_rcvd.c_str());
  m_session_id.session_id_len = (uint32_t)session_id_rcvd.size();
}

int OpenCdm::SetServerCertificate(const uint8_t* server_certificate_data,
                                  uint32_t server_certificate_data_size) {
  // FIXME: We propagate RPC call results from this method, but not other ones, why?
  auto ret = platform_->MediaKeySetServerCertificate((uint8_t*)server_certificate_data, server_certificate_data_size);
  return ret.platform_response ==  PLATFORM_CALL_SUCCESS;
}

bool OpenCdm::CreateSession(const std::string& initDataType, unsigned char* pbInitData, int cbInitData, std::string& session_id, LicenseType licenseType) {
  m_eState = KEY_SESSION_INIT;

  auto response = platform_->MediaKeysCreateSession(licenseType, initDataType, pbInitData, cbInitData);

  if (response.platform_response != PLATFORM_CALL_SUCCESS) {
    CDM_LOG_LINE("failed to create a new session");
    m_eState = KEY_SESSION_ERROR;
    return false;
  }

  m_session_id = response.session_id;

  if (m_eState == KEY_SESSION_INIT)
    m_eState = KEY_SESSION_WAITING_FOR_MESSAGE;

  session_id.assign(m_session_id.session_id, m_session_id.session_id_len);
  CDM_LOG_LINE("created a session with id %s", session_id.c_str());
  return true;
}

void OpenCdm::GetKeyMessage(std::string& challenge, int* challengeLength, unsigned char* licenseURL, int* urlLength) {

  CDM_LOG_LINE("taking our lock");
  std::unique_lock<std::mutex> lck(m_mtx);
  CDM_LOG_LINE("state is %s", sessionStateToString(m_eState));

  m_cond_var.wait(lck, [=]() { return m_eState != KEY_SESSION_WAITING_FOR_MESSAGE; });

  CDM_LOG_LINE("key message now ready, state is %s", sessionStateToString(m_eState));

  if (m_eState == KEY_SESSION_MESSAGE_RECEIVED) {
    char temp[m_message.length()];

    m_message.copy(temp,m_message.length(),0);
    challenge.assign((const char*)temp, m_message.length());
    strncpy((char*)licenseURL, (const char*)m_dest_url.c_str(), m_dest_url.length());
    *challengeLength = m_message.length();
    *urlLength = m_dest_url.length();
    char msg[m_message.length()];
    m_message.copy( msg, m_message.length() , 0);
    CDM_LOG_LINE("setting state to KEY_SESSION_WAITING_FOR_LICENSE");
    m_eState = KEY_SESSION_WAITING_FOR_LICENSE;
  }

  if (m_eState == KEY_SESSION_READY) {
    *challengeLength = 0;
    *licenseURL= 0;
  }
}

int OpenCdm::Load(std::string& responseMsg) {
  int ret = 1;
  CDM_DLOG() << "Load >> invoked from ocdm :: estate = " << m_eState << "\n";
  CDM_DLOG() << "Load session with info exisiting key.";
  
  m_eState = KEY_SESSION_WAITING_FOR_LOAD_SESSION;
  MediaKeySessionLoadResponse status = platform_->MediaKeySessionLoad(m_session_id.session_id, m_session_id.session_id_len);
  if (status.platform_response ==  PLATFORM_CALL_SUCCESS) {
    CDM_DLOG() << "Load session with info exisiting key complete.";

     while (m_eState == KEY_SESSION_WAITING_FOR_LOAD_SESSION) {
       CDM_DLOG() << "Waiting for load message!";
       std::unique_lock<std::mutex> lck(m_mtx);
       m_cond_var.wait(lck);
     }

     if (m_eState == KEY_SESSION_UPDATE_LICENSE || m_eState == KEY_SESSION_MESSAGE_RECEIVED) {
       while (m_eState == KEY_SESSION_LOADED) {
         CDM_DLOG() << "Waiting for Load message!";
         std::unique_lock<std::mutex> lck(m_mtx);
         m_cond_var.wait(lck);
       }
       if (m_eState == KEY_SESSION_UPDATE_LICENSE) {
         ret = 0;
         CDM_DLOG() << "setting m_eState to KEY_SESSION_LOADED";
         m_eState = KEY_SESSION_LOADED; // TODO: To be rechecked and updated.
       }
       if (m_eState == KEY_SESSION_MESSAGE_RECEIVED) {
         ret = 0;
         CDM_DLOG() << "setting m_eState to KEY_SESSION_EXPIRED";
         m_eState = KEY_SESSION_EXPIRED; // TODO: To be rechecked and updated.
         responseMsg.assign("message:");
      }
    }
  }
  responseMsg.append(m_message.c_str(), m_message.length());
  return ret;
}

// FIXME: At some point we will need to update this to return a key status vector to comply with the spec as one session can have several keys.
OpenCdm::KeyStatus OpenCdm::Update(unsigned char* pbResponse, int cbResponse, std::string& responseMsg)
{
  CDM_LOG_LINE("invoked, state is %s", sessionStateToString(m_eState));
  CDM_LOG_LINE("update response from application was contained %d bytes: ", cbResponse);
  //CDMDumpMemory(pbResponse, cbResponse);

  platform_->MediaKeySessionUpdate((uint8_t*)pbResponse, cbResponse, m_session_id.session_id, m_session_id.session_id_len);

  CDM_LOG_LINE("taking our lock");
  std::unique_lock<std::mutex> lck(m_mtx);
  CDM_LOG_LINE("waiting for licence, state is %s", sessionStateToString(m_eState));
  m_cond_var.wait(lck, [=]() { return m_eState != KEY_SESSION_WAITING_FOR_LICENSE; });
  CDM_LOG_LINE("received a lience update, state is now %s", sessionStateToString(m_eState));

  // FIXME: We get into UPDATE_LICENSE when the key status is considered usable, why
  // UPDATE_LICENSE is a good state to have set is anybody's guess. Mine would be
  // that this state should be called KEY_STATUS_CHANGED. The implication here would
  // be that the right approach is to fill responseMsg with the key status vector, but
  // without a structured codec, that's rather nasty.
  if (m_eState == KEY_SESSION_MESSAGE_RECEIVED) {
    responseMsg.assign("message:");
    responseMsg.append(m_message.c_str(), m_message.length());
  }

  CDM_LOG_LINE("The response message contains %d bytes and its contents are:", responseMsg.size());
  CDMDumpMemory(reinterpret_cast<const uint8_t*>(responseMsg.data()), responseMsg.size());

  return OpenCdm::internalStatusToKeyStatus();
}

int OpenCdm::Remove(std::string& responseMsg) {
  CDM_DLOG() << "Remove >> invoked from ocdm :: estate = " << m_eState << "\n";
  int ret = 1;
  CDM_DLOG() << "\nEnd";
  CDM_DLOG() << "Remove session with info exisiting key.";

  m_eState = KEY_SESSION_WAITING_FOR_LICENSE_REMOVAL;
  MediaKeySessionRemoveResponse status = platform_->MediaKeySessionRemove(m_session_id.session_id, m_session_id.session_id_len);
  if (status.platform_response ==  PLATFORM_CALL_SUCCESS) {
    CDM_DLOG() << "Remove session with info exisiting key complete.";
  
    while (m_eState == KEY_SESSION_WAITING_FOR_LICENSE_REMOVAL) {
      CDM_DLOG() << "Waiting for remove message!";
      std::unique_lock<std::mutex> lck(m_mtx);
      m_cond_var.wait(lck);
    }

    if (m_eState == KEY_SESSION_REMOVED || m_eState == KEY_SESSION_MESSAGE_RECEIVED) {
        while (m_eState == KEY_SESSION_REMOVED) {
           CDM_DLOG() << "Waiting for remove message!";
           std::unique_lock<std::mutex> lck(m_mtx);
           m_cond_var.wait(lck);
        }
        if (m_eState == KEY_SESSION_MESSAGE_RECEIVED) {
           ret = 0;
           CDM_DLOG() << "setting m_eState to KEY_SESSION_REMOVED";
           m_eState = KEY_SESSION_REMOVED; // TODO: To be rechecked and updated.
           responseMsg.assign("message:");
      }
   }
  }
  responseMsg.append(m_message.c_str(), m_message.length());
  return ret;
}

int OpenCdm::Close() {
  CDM_DLOG() << "Close >> invoked from ocdm :: estate = " << m_eState << "\n";
  CDM_DLOG() << "\nEnd";
  CDM_DLOG() << "Close session with info existing key.";
  MediaKeySessionCloseResponse status = platform_->MediaKeySessionClose(m_session_id.session_id, m_session_id.session_id_len);
  CDM_DLOG() << "Close session with info existing key complete.";
  if (status.platform_response ==  PLATFORM_CALL_SUCCESS) {
    m_eState = KEY_SESSION_CLOSED;
    return (true);
  }
  else
    return (false);
}

int OpenCdm::ReleaseMem() {
  for (const auto& p : media_engines_)
      p.second->ReleaseMem();

  // FIXME: Yet another pointless return value..
  return 0;
}

int OpenCdm::Decrypt(unsigned char* encryptedData, uint32_t encryptedDataLength, unsigned char* ivData, uint32_t ivDataLength) {
  uint32_t outSize;
  std::string s(m_session_id.session_id, m_session_id.session_id_len);
  CDM_LOG_LINE("decrypt with session id %s", s.c_str());
  CDM_LOG_LINE("there are %ld bytes of encrypted data", encryptedDataLength);
  CDM_LOG_LINE("the IV data has %ld bytes", ivDataLength);

  // Note, when OpencdmSessionId is refactored (it's very silly, see the comment at the declaration) this won't be needed
  SessionId sessionId(m_session_id.session_id, m_session_id.session_id_len);

  // Note, there's a lot of sublety in the map lookup below due to the non-standard design of the media engine classes,
  // you don't want to call ::insert or ::operator[] because that might leak a media engine if the element already exists
  // Instead be very conservative and only call insert when it's known for a fact the map doesn't contain the key.
  if (!media_engines_.count(sessionId)) {
      CDM_LOG_LINE("created a new media engine");
      media_engines_.insert(std::make_pair(sessionId, std::unique_ptr<OpenCdmMediaengine>(OpenCdmMediaengineFactory::Create(m_key_system, m_session_id))));

  }
  auto& mediaEngine = media_engines_[sessionId];

  CDM_LOG_LINE("calling media engine decrypt");
  DecryptResponse dr = mediaEngine->Decrypt((const uint8_t*)ivData, ivDataLength,
      (const uint8_t*)encryptedData, encryptedDataLength, (uint8_t*)encryptedData, outSize);

  if (dr.platform_response == PLATFORM_CALL_SUCCESS)
      CDM_LOG_LINE("decryption suceeded, decrypted content has %ld bytes", dr.cbResponseData);
  else
      CDM_LOG_LINE("platform failed to decrypt content");

  // FIXME: Here's another place where we return irregardless of what Decrypt happened to do
  //   Until a review of callsites has occurred, all we can do is add some logging.
  return 0;
}

bool OpenCdm::IsTypeSupported(const std::string& keySystem, const std::string& mimeType) {
  MediaKeyTypeResponse ret;

  ret = platform_->IsTypeSupported(keySystem, mimeType);

  return ret.platform_response == PLATFORM_CALL_SUCCESS;
}

void OpenCdm::ReadyCallback(OpenCdmPlatformSessionId platform_session_id) {
  CDM_LOG_LINE("call comes in");
  std::unique_lock<std::mutex> lck(m_mtx);
  m_eState = KEY_SESSION_READY;
  m_cond_var.notify_all();
  CDM_LOG_LINE("call over");
}

void OpenCdm::ErrorCallback(OpenCdmPlatformSessionId,
    uint32_t, std::string err_msg) {
  CDM_LOG_LINE("error message is %s", err_msg.c_str());
  std::unique_lock<std::mutex> lck(m_mtx);
  m_message = err_msg;
  m_eState = KEY_SESSION_ERROR;
  m_cond_var.notify_all();
  CDM_LOG_LINE("call over");
}

void OpenCdm::MessageCallback(OpenCdmPlatformSessionId,
                              std::string& message,
                              std::string destination_url)
{
  CDM_LOG_LINE("call for message");
  //CDMDumpMemory(reinterpret_cast<const uint8_t*>(message.data()), message.size());

  std::unique_lock<std::mutex> lck(m_mtx);
  m_message = message;
  m_dest_url = destination_url;
  m_eState = KEY_SESSION_MESSAGE_RECEIVED;
  m_cond_var.notify_all();
  CDM_LOG_LINE("call over");
}

void OpenCdm::OnKeyStatusUpdateCallback(OpenCdmPlatformSessionId platform_session_id, std::string message) {
  CDM_LOG_LINE("call for key statuses");
  // FIXME: Currently this call back is a little pointless, the information about which keys are usable can't be seen by the application...
  if (message == "KeyUsable")
    m_eState = KEY_SESSION_UPDATE_LICENSE;
  else if (message == "KeyReleased")
    m_eState = KEY_SESSION_REMOVED;
  else if (message == "KeyExpired")
    m_eState = KEY_SESSION_EXPIRED;
  else
    m_eState = KEY_SESSION_ERROR;

  m_message = message;
  m_cond_var.notify_all();
  CDM_LOG_LINE("call over, state now %s", sessionStateToString(m_eState));
}

OpenCdm::KeyStatus OpenCdm::internalStatusToKeyStatus(InternalSessionState status)
{
  switch(status) {

  case KEY_SESSION_INIT:
  case KEY_SESSION_WAITING_FOR_MESSAGE:
  case KEY_SESSION_MESSAGE_RECEIVED:
  case KEY_SESSION_WAITING_FOR_LICENSE:
  case KEY_SESSION_LOADED:
  case KEY_SESSION_WAITING_FOR_LICENSE_REMOVAL:
  case KEY_SESSION_WAITING_FOR_LOAD_SESSION:
    return OpenCdm::KeyStatus::StatusPending;

  case KEY_SESSION_READY:
  case KEY_SESSION_UPDATE_LICENSE:
    return OpenCdm::KeyStatus::Usable;

  case KEY_SESSION_ERROR:
    return OpenCdm::KeyStatus::InternalError;

  case KEY_SESSION_REMOVED:
  case KEY_SESSION_CLOSED:
    return OpenCdm::KeyStatus::Released;

  case KEY_SESSION_EXPIRED:
    return OpenCdm::KeyStatus::Expired;

  default:
    assert(false);
    return OpenCdm::KeyStatus::InternalError;
  }
}

} // namespace media
