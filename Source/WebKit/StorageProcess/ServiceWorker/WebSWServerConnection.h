/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(SERVICE_WORKER)

#include "MessageReceiver.h"
#include "MessageSender.h"
#include <WebCore/SWServer.h>
#include <pal/SessionID.h>
#include <wtf/HashMap.h>

namespace IPC {
class FormDataReference;
}

namespace WebCore {
class ServiceWorkerRegistrationKey;
struct ClientOrigin;
struct ExceptionData;
struct MessageWithMessagePorts;
struct ServiceWorkerClientData;
}

namespace WebKit {

class WebSWServerConnection : public WebCore::SWServer::Connection, public IPC::MessageSender, public IPC::MessageReceiver {
public:
    WebSWServerConnection(WebCore::SWServer&, IPC::Connection&, PAL::SessionID);
    WebSWServerConnection(const WebSWServerConnection&) = delete;
    ~WebSWServerConnection() final;

    IPC::Connection& ipcConnection() const { return m_contentConnection.get(); }

    void disconnectedFromWebProcess();
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) final;
    void didReceiveSyncMessage(IPC::Connection&, IPC::Decoder&, std::unique_ptr<IPC::Encoder>&);

    PAL::SessionID sessionID() const { return m_sessionID; }

    void didReceiveFetchResponse(uint64_t fetchIdentifier, const WebCore::ResourceResponse&);
    void didReceiveFetchData(uint64_t fetchIdentifier, const IPC::DataReference&, int64_t encodedDataLength);
    void didReceiveFetchFormData(uint64_t fetchIdentifier, const IPC::FormDataReference&);
    void didFinishFetch(uint64_t fetchIdentifier);
    void didFailFetch(uint64_t fetchIdentifier);
    void didNotHandleFetch(uint64_t fetchIdentifier);

    void postMessageToServiceWorkerClient(WebCore::DocumentIdentifier destinationContextIdentifier, WebCore::MessageWithMessagePorts&&, WebCore::ServiceWorkerIdentifier sourceServiceWorkerIdentifier, const String& sourceOrigin);

private:
    // Implement SWServer::Connection (Messages to the client WebProcess)
    void rejectJobInClient(WebCore::ServiceWorkerJobIdentifier, const WebCore::ExceptionData&) final;
    void resolveRegistrationJobInClient(WebCore::ServiceWorkerJobIdentifier, const WebCore::ServiceWorkerRegistrationData&, WebCore::ShouldNotifyWhenResolved) final;
    void resolveUnregistrationJobInClient(WebCore::ServiceWorkerJobIdentifier, const WebCore::ServiceWorkerRegistrationKey&, bool unregistrationResult) final;
    void startScriptFetchInClient(WebCore::ServiceWorkerJobIdentifier, const WebCore::ServiceWorkerRegistrationKey&, WebCore::FetchOptions::Cache) final;
    void updateRegistrationStateInClient(WebCore::ServiceWorkerRegistrationIdentifier, WebCore::ServiceWorkerRegistrationState, const std::optional<WebCore::ServiceWorkerData>&) final;
    void updateWorkerStateInClient(WebCore::ServiceWorkerIdentifier, WebCore::ServiceWorkerState) final;
    void fireUpdateFoundEvent(WebCore::ServiceWorkerRegistrationIdentifier) final;
    void setRegistrationLastUpdateTime(WebCore::ServiceWorkerRegistrationIdentifier, WallTime) final;
    void setRegistrationUpdateViaCache(WebCore::ServiceWorkerRegistrationIdentifier, WebCore::ServiceWorkerUpdateViaCache) final;
    void notifyClientsOfControllerChange(const HashSet<WebCore::DocumentIdentifier>& contextIdentifiers, const WebCore::ServiceWorkerData& newController);
    void registrationReady(uint64_t registrationReadyRequestIdentifier, WebCore::ServiceWorkerRegistrationData&&) final;

    void startFetch(uint64_t fetchIdentifier, WebCore::ServiceWorkerRegistrationIdentifier, WebCore::ResourceRequest&&, WebCore::FetchOptions&&, IPC::FormDataReference&&, String&& referrer);

    void postMessageToServiceWorker(WebCore::ServiceWorkerIdentifier destination, WebCore::MessageWithMessagePorts&&, const WebCore::ServiceWorkerOrClientIdentifier& source);

    void matchRegistration(uint64_t registrationMatchRequestIdentifier, const WebCore::SecurityOriginData& topOrigin, const WebCore::URL& clientURL);
    void getRegistrations(uint64_t registrationMatchRequestIdentifier, const WebCore::SecurityOriginData& topOrigin, const WebCore::URL& clientURL);

    void registerServiceWorkerClient(WebCore::SecurityOriginData&& topOrigin, WebCore::ServiceWorkerClientData&&, const std::optional<WebCore::ServiceWorkerIdentifier>&);
    void unregisterServiceWorkerClient(const WebCore::ServiceWorkerClientIdentifier&);

    IPC::Connection* messageSenderConnection() final { return m_contentConnection.ptr(); }
    uint64_t messageSenderDestinationID() final { return identifier().toUInt64(); }
    
    template<typename U> void sendToContextProcess(U&& message);
    template<typename U> static void sendToContextProcess(WebCore::SWServerToContextConnection&, U&& message);

    PAL::SessionID m_sessionID;
    Ref<IPC::Connection> m_contentConnection;
    HashMap<WebCore::ServiceWorkerClientIdentifier, WebCore::ClientOrigin> m_clientOrigins;
};

} // namespace WebKit

#endif // ENABLE(SERVICE_WORKER)
