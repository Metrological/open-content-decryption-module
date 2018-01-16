/*
 * Copyright 2014 Fraunhofer FOKUS
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

#ifndef MEDIA_CDM_PPAPI_EXTERNAL_OPEN_CDM_MEDIAENGINE_OPEN_CDM_MEDIAENGINE_FACTORY_H_
#define MEDIA_CDM_PPAPI_EXTERNAL_OPEN_CDM_MEDIAENGINE_OPEN_CDM_MEDIAENGINE_FACTORY_H_

#include <string>

#include "open_cdm_mediaengine.h"
#include "open_cdm_mediaengine_impl.h"

#include "../common/cdm_logging.h"

namespace media {

const std::string open_cdm_key_system = "com.widevine.alpha";

// TODO(ska): outsource the mapping of key system string
// to mediaengine and platform implementations

class OpenCdmMediaengineFactory {
 public:
  static OpenCdmMediaengine *Create(std::string key_system,
                                    OpenCdmPlatformSessionId session_id);
};

/*
 * returns raw pointer to be platform and type independent, can be converted
 * to smart pointer on successful creation.
 */
OpenCdmMediaengine *OpenCdmMediaengineFactory::Create(
    std::string key_system, OpenCdmPlatformSessionId session_id) {
    // not only do we completely ignore the key_system, the mediaengine impl
    // ctor below only has one implementation, making all this abstraction a
    // waste of brain cycles and giving nothing of value
    std::string sessionId(session_id.session_id, session_id.session_id_len);
    CDM_LOG_LINE("creating a media engine for key_system=%s and session_id=%s", key_system.c_str(), sessionId.c_str());
    return new OpenCdmMediaengineImpl(session_id.session_id, session_id.session_id_len);
}
}  // namespace media

#endif  // MEDIA_CDM_PPAPI_EXTERNAL_OPEN_CDM_MEDIAENGINE_OPEN_CDM_MEDIAENGINE_FACTORY_H_
