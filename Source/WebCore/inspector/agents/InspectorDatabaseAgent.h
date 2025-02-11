/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 * Copyright (C) 2015 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "InspectorWebAgentBase.h"
#include <inspector/InspectorBackendDispatchers.h>
#include <inspector/InspectorFrontendDispatchers.h>
#include <wtf/HashMap.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

class Database;
class InspectorDatabaseResource;

typedef String ErrorString;

class InspectorDatabaseAgent final : public InspectorAgentBase, public Inspector::DatabaseBackendDispatcherHandler {
    WTF_MAKE_NONCOPYABLE(InspectorDatabaseAgent);
    WTF_MAKE_FAST_ALLOCATED;
public:
    explicit InspectorDatabaseAgent(WebAgentContext&);
    virtual ~InspectorDatabaseAgent();

    void didCreateFrontendAndBackend(Inspector::FrontendRouter*, Inspector::BackendDispatcher*) override;
    void willDestroyFrontendAndBackend(Inspector::DisconnectReason) override;

    void clearResources();

    // Called from the front-end.
    void enable(ErrorString&) override;
    void disable(ErrorString&) override;
    void getDatabaseTableNames(ErrorString&, const String& databaseId, RefPtr<JSON::ArrayOf<String>>& names) override;
    void executeSQL(ErrorString&, const String& databaseId, const String& query, Ref<ExecuteSQLCallback>&&) override;

    // Called from the injected script.
    String databaseId(Database&);

    void didOpenDatabase(RefPtr<Database>&&, const String& domain, const String& name, const String& version);
private:
    Database* databaseForId(const String& databaseId);
    InspectorDatabaseResource* findByFileName(const String& fileName);

    std::unique_ptr<Inspector::DatabaseFrontendDispatcher> m_frontendDispatcher;
    RefPtr<Inspector::DatabaseBackendDispatcher> m_backendDispatcher;

    typedef HashMap<String, RefPtr<InspectorDatabaseResource>> DatabaseResourcesMap;
    DatabaseResourcesMap m_resources;
    bool m_enabled { false };
};

} // namespace WebCore
