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

#include "ClientOrigin.h"
#include "DocumentIdentifier.h"
#include "RegistrationStore.h"
#include "SWServerWorker.h"
#include "ServiceWorkerClientData.h"
#include "ServiceWorkerIdentifier.h"
#include "ServiceWorkerJob.h"
#include "ServiceWorkerRegistrationData.h"
#include "ServiceWorkerRegistrationKey.h"
#include "ServiceWorkerTypes.h"
#include <wtf/HashMap.h>
#include <wtf/HashSet.h>
#include <wtf/ObjectIdentifier.h>
#include <wtf/RunLoop.h>
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/Threading.h>
#include <wtf/UniqueRef.h>
#include <wtf/WeakPtr.h>

namespace WebCore {

class SWOriginStore;
class SWServerJobQueue;
class SWServerRegistration;
class SWServerToContextConnection;
enum class ServiceWorkerRegistrationState;
enum class ServiceWorkerState;
struct ExceptionData;
struct ServiceWorkerClientQueryOptions;
struct ServiceWorkerContextData;
struct ServiceWorkerFetchResult;
struct ServiceWorkerRegistrationData;
class Timer;

class SWServer {
    WTF_MAKE_FAST_ALLOCATED;
public:
    class Connection {
        WTF_MAKE_FAST_ALLOCATED;
        friend class SWServer;
    public:
        WEBCORE_EXPORT virtual ~Connection();

        using Identifier = SWServerConnectionIdentifier;
        Identifier identifier() const { return m_identifier; }

        auto& weakPtrFactory() { return m_weakFactory; }

        WEBCORE_EXPORT void didResolveRegistrationPromise(const ServiceWorkerRegistrationKey&);
        SWServerRegistration* doRegistrationMatching(const SecurityOriginData& topOrigin, const URL& clientURL) { return m_server.doRegistrationMatching(topOrigin, clientURL); }
        void resolveRegistrationReadyRequests(SWServerRegistration&);

        // Messages to the client WebProcess
        virtual void updateRegistrationStateInClient(ServiceWorkerRegistrationIdentifier, ServiceWorkerRegistrationState, const std::optional<ServiceWorkerData>&) = 0;
        virtual void updateWorkerStateInClient(ServiceWorkerIdentifier, ServiceWorkerState) = 0;
        virtual void fireUpdateFoundEvent(ServiceWorkerRegistrationIdentifier) = 0;
        virtual void setRegistrationLastUpdateTime(ServiceWorkerRegistrationIdentifier, WallTime) = 0;
        virtual void setRegistrationUpdateViaCache(ServiceWorkerRegistrationIdentifier, ServiceWorkerUpdateViaCache) = 0;
        virtual void notifyClientsOfControllerChange(const HashSet<DocumentIdentifier>& contextIdentifiers, const ServiceWorkerData& newController) = 0;
        virtual void registrationReady(uint64_t registrationReadyRequestIdentifier, ServiceWorkerRegistrationData&&) = 0;

    protected:
        WEBCORE_EXPORT explicit Connection(SWServer&);
        SWServer& server() { return m_server; }

        WEBCORE_EXPORT void scheduleJobInServer(ServiceWorkerJobData&&);
        WEBCORE_EXPORT void finishFetchingScriptInServer(const ServiceWorkerFetchResult&);
        WEBCORE_EXPORT void addServiceWorkerRegistrationInServer(ServiceWorkerRegistrationIdentifier);
        WEBCORE_EXPORT void removeServiceWorkerRegistrationInServer(ServiceWorkerRegistrationIdentifier);
        WEBCORE_EXPORT void syncTerminateWorker(ServiceWorkerIdentifier);
        WEBCORE_EXPORT void whenRegistrationReady(uint64_t registrationReadyRequestIdentifier, const SecurityOriginData& topOrigin, const URL& clientURL);

    private:
        // Messages to the client WebProcess
        virtual void rejectJobInClient(ServiceWorkerJobIdentifier, const ExceptionData&) = 0;
        virtual void resolveRegistrationJobInClient(ServiceWorkerJobIdentifier, const ServiceWorkerRegistrationData&, ShouldNotifyWhenResolved) = 0;
        virtual void resolveUnregistrationJobInClient(ServiceWorkerJobIdentifier, const ServiceWorkerRegistrationKey&, bool registrationResult) = 0;
        virtual void startScriptFetchInClient(ServiceWorkerJobIdentifier, const ServiceWorkerRegistrationKey&, FetchOptions::Cache) = 0;

        struct RegistrationReadyRequest {
            SecurityOriginData topOrigin;
            URL clientURL;
            uint64_t identifier;
        };

        SWServer& m_server;
        Identifier m_identifier;
        WeakPtrFactory<Connection> m_weakFactory;
        Vector<RegistrationReadyRequest> m_registrationReadyRequests;
    };

    WEBCORE_EXPORT SWServer(UniqueRef<SWOriginStore>&&, String&& registrationDatabaseDirectory, PAL::SessionID);
    WEBCORE_EXPORT ~SWServer();

    WEBCORE_EXPORT void clearAll(WTF::CompletionHandler<void()>&&);
    WEBCORE_EXPORT void clear(const SecurityOrigin&, WTF::CompletionHandler<void()>&&);

    SWServerRegistration* getRegistration(const ServiceWorkerRegistrationKey&);
    void addRegistration(std::unique_ptr<SWServerRegistration>&&);
    void removeRegistration(const ServiceWorkerRegistrationKey&);
    WEBCORE_EXPORT Vector<ServiceWorkerRegistrationData> getRegistrations(const SecurityOriginData& topOrigin, const URL& clientURL);

    void scheduleJob(ServiceWorkerJobData&&);
    void rejectJob(const ServiceWorkerJobData&, const ExceptionData&);
    void resolveRegistrationJob(const ServiceWorkerJobData&, const ServiceWorkerRegistrationData&, ShouldNotifyWhenResolved);
    void resolveUnregistrationJob(const ServiceWorkerJobData&, const ServiceWorkerRegistrationKey&, bool unregistrationResult);
    void startScriptFetch(const ServiceWorkerJobData&, FetchOptions::Cache);

    void updateWorker(Connection&, const ServiceWorkerJobDataIdentifier&, SWServerRegistration&, const URL&, const String& script, const ContentSecurityPolicyResponseHeaders&, WorkerType);
    void terminateWorker(SWServerWorker&);
    void syncTerminateWorker(SWServerWorker&);
    void fireInstallEvent(SWServerWorker&);
    void fireActivateEvent(SWServerWorker&);

    WEBCORE_EXPORT SWServerWorker* workerByID(ServiceWorkerIdentifier) const;
    std::optional<ServiceWorkerClientData> serviceWorkerClientWithOriginByID(const ClientOrigin&, const ServiceWorkerClientIdentifier&) const;
    WEBCORE_EXPORT SWServerWorker* activeWorkerFromRegistrationID(ServiceWorkerRegistrationIdentifier);

    WEBCORE_EXPORT void markAllWorkersAsTerminated();
    
    Connection* getConnection(SWServerConnectionIdentifier identifier) { return m_connections.get(identifier); }
    SWOriginStore& originStore() { return m_originStore; }

    void scriptContextFailedToStart(const std::optional<ServiceWorkerJobDataIdentifier>&, SWServerWorker&, const String& message);
    void scriptContextStarted(const std::optional<ServiceWorkerJobDataIdentifier>&, SWServerWorker&);
    void didFinishInstall(const std::optional<ServiceWorkerJobDataIdentifier>&, SWServerWorker&, bool wasSuccessful);
    void didFinishActivation(SWServerWorker&);
    void workerContextTerminated(SWServerWorker&);
    void matchAll(SWServerWorker&, const ServiceWorkerClientQueryOptions&, ServiceWorkerClientsMatchAllCallback&&);
    void claim(SWServerWorker&);

    WEBCORE_EXPORT void serverToContextConnectionCreated();
    
    WEBCORE_EXPORT static HashSet<SWServer*>& allServers();

    WEBCORE_EXPORT void registerServiceWorkerClient(ClientOrigin&&, ServiceWorkerClientData&&, const std::optional<ServiceWorkerIdentifier>& controllingServiceWorkerIdentifier);
    WEBCORE_EXPORT void unregisterServiceWorkerClient(const ClientOrigin&, ServiceWorkerClientIdentifier);

    using RunServiceWorkerCallback = WTF::Function<void(bool, SWServerToContextConnection&)>;
    WEBCORE_EXPORT void runServiceWorkerIfNecessary(ServiceWorkerIdentifier, RunServiceWorkerCallback&&);

    void setClientActiveWorker(ServiceWorkerClientIdentifier, ServiceWorkerIdentifier);
    void resolveRegistrationReadyRequests(SWServerRegistration&);

    void addRegistrationFromStore(ServiceWorkerContextData&&);
    void registrationStoreImportComplete();
    void registrationStoreDatabaseFailedToOpen();

    WEBCORE_EXPORT void getOriginsWithRegistrations(WTF::Function<void(const HashSet<SecurityOriginData>&)>);

private:
    void registerConnection(Connection&);
    void unregisterConnection(Connection&);

    void scriptFetchFinished(Connection&, const ServiceWorkerFetchResult&);

    void didResolveRegistrationPromise(Connection&, const ServiceWorkerRegistrationKey&);

    void addClientServiceWorkerRegistration(Connection&, ServiceWorkerRegistrationIdentifier);
    void removeClientServiceWorkerRegistration(Connection&, ServiceWorkerRegistrationIdentifier);

    WEBCORE_EXPORT SWServerRegistration* doRegistrationMatching(const SecurityOriginData& topOrigin, const URL& clientURL);
    bool runServiceWorker(ServiceWorkerIdentifier);

    void tryInstallContextData(ServiceWorkerContextData&&);
    void installContextData(const ServiceWorkerContextData&);

    SWServerRegistration* registrationFromServiceWorkerIdentifier(ServiceWorkerIdentifier);
    void forEachClientForOrigin(const ClientOrigin&, const WTF::Function<void(ServiceWorkerClientData&)>&);

    void performGetOriginsWithRegistrationsCallbacks();

    enum TerminationMode {
        Synchronous,
        Asynchronous,
    };
    void terminateWorkerInternal(SWServerWorker&, TerminationMode);

    HashMap<SWServerConnectionIdentifier, Connection*> m_connections;
    HashMap<ServiceWorkerRegistrationKey, std::unique_ptr<SWServerRegistration>> m_registrations;
    HashMap<ServiceWorkerRegistrationIdentifier, SWServerRegistration*> m_registrationsByID;
    HashMap<ServiceWorkerRegistrationKey, std::unique_ptr<SWServerJobQueue>> m_jobQueues;

    HashMap<ServiceWorkerIdentifier, Ref<SWServerWorker>> m_runningOrTerminatingWorkers;

    struct Clients {
        Vector<ServiceWorkerClientIdentifier> identifiers;
        std::unique_ptr<Timer> terminateServiceWorkersTimer;
    };
    HashMap<ClientOrigin, Clients> m_clientIdentifiersPerOrigin;
    HashMap<ServiceWorkerClientIdentifier, ServiceWorkerClientData> m_clientsById;
    HashMap<ServiceWorkerClientIdentifier, ServiceWorkerIdentifier> m_clientToControllingWorker;

    UniqueRef<SWOriginStore> m_originStore;
    RegistrationStore m_registrationStore;
    Vector<ServiceWorkerContextData> m_pendingContextDatas;
    Vector<ServiceWorkerJobData> m_pendingJobs;
    HashMap<ServiceWorkerIdentifier, Vector<RunServiceWorkerCallback>> m_serviceWorkerRunRequests;
    PAL::SessionID m_sessionID;
    bool m_importCompleted { false };
    Vector<WTF::Function<void(const HashSet<SecurityOriginData>&)>> m_getOriginsWithRegistrationsCallbacks;
};

} // namespace WebCore

#endif // ENABLE(SERVICE_WORKER)
